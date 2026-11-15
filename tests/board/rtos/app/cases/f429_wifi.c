/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   f429_wifi.c
 * @brief  Case f429_wifi: the RTL8188EUS on STM32F429, driven by the real
 *         mac80211/cfg80211 stack (components/liblinux80211) over the DWC2
 *         host and the CherryUSB URB engine.
 *
 * The case is one boot of the W0-W6 ladder, stopped at CONFIG_F429_WIFI_STAGE:
 *
 *   3  attach:   probe, compiled-in firmware, wlan0, wiphy row, dev_open
 *   4  scan:     the real NL80211_CMD_TRIGGER_SCAN stream -> mac80211's SW scan
 *                -> the bench AP in cfg80211's BSS table
 *   5  join:     NL80211_CMD_CONNECT (open) + lwIP netif + ICMP (later stage)
 *   6  data:     the throughput phases (later stage)
 *
 * The bring-up order is the RK3568 8188EU cases': component core, USB backend
 * registration, module_init walk, driver registration, firmware registration,
 * then the board's controller.  What is F429-specific:
 *   - one host bus (OTG_HS, bus 0) instead of the RK3568 bus map;
 *   - the kernel heap is in FMC SDRAM (CONFIG_F429_HEAP_SDRAM), so the very
 *     first driver allocation is also the SDRAM-DMA experiment;
 *   - the runner stack is the image's, not a per-case allocation.
 *
 * Anchors (the round runner greps these; the layer names are the evidence):
 *   f429_wifi: stage=<n>
 *   f429_wifi: core ready
 *   f429_wifi: usb backend ready
 *   f429_wifi: initcalls run
 *   f429_wifi: usb drivers=<n>
 *   f429_wifi: fw registered=0 name=...
 *   f429_wifi: usb bus=0 hcd=dwc2 base=0x40040000 started
 *   f429_wifi: wlan0 present=1 ifindex=.. mac=20:f4:1b:52:8f:01
 *   f429_wifi: wiphy bands=1 managed=1 ht=1 vht=0 ch2432 max_power=.. flags=..
 *   f429_wifi: firmware <driver line>
 *   f429_wifi: start rc=0
 *   f429_wifi: scan ssid=... freq=2432 rc=0
 *   f429_wifi: bss ssid=... bssid=.. freq=2432 signal=..
 *   f429_wifi: PASS W3 | PASS W4 | FAIL <reason>
 *
 * What this is not: WPA2, the AP or monitor role, DHCP, or a throughput
 * verdict.  The F429 numbers do not transfer to the RK3568 line.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "rtl8xxxu_fw.h"
#include "os/cmsis2/cmsis2.h"
#include "FreeRTOS.h"
#include "task.h"

/* CherryUSB first (the RK cases' include order): its <stdlib.h> must be parsed
 * before the Linux face's compiler-attributes header is. */
#include "usbh_core.h"
#include "usb_glue.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/hostap_ap.h"	/* lkpi_tx_health_get */
#include "linux80211/net.h"	/* W6: lwIP netif, ICMP, phases */
#include "linuxkpi_usb.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/etherdevice.h>
#include <linux/workqueue.h>
#include <linux/usb.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#ifndef CONFIG_F429_WIFI_STAGE
#define CONFIG_F429_WIFI_STAGE 3
#endif

/* The component's two start-up entries the RK cases also declare by hand: the
 * log sink takes a complete line, the URB engine's adapter is registered once
 * before usbh_initialize(). */
void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);

#define F429_WIFI_SSID		"labwifi-ar-ht20s"
#define F429_WIFI_FREQ		2432u
#define F429_WIFI_EXPECT_MAC	"20:f4:1b:52:8f:01"

#define F429_WIFI_WAIT_MS	60000U
#define F429_WIFI_POLL_MS	100U
#define F429_WIFI_SCAN_WAIT_MS	10000U
#define F429_WIFI_CONNECT_WAIT_MS 20000U

static char fw_line[160];

/* System-workqueue probe: cfg80211 drives the post-authentication association
 * from a plain schedule_work() (wireless/sme.c: cfg80211_conn_work).  A work
 * queued here and never run is the difference between "the stack did not ask"
 * and "the port could not deliver the ask". */
static volatile unsigned int syswq_ran;
static void
syswq_probe_fn(struct work_struct *w)
{
	(void)w;
	syswq_ran++;
}
static struct work_struct syswq_probe;

/*
 * W6 watchdog: a second, higher-priority thread that keeps printing after the
 * data path.  A hard wedge (an ISR storm or a fault that never prints) stops
 * it; a thread-level deadlock does not - and in that case the watchdog prints
 * the task/workqueue census once, which names the stuck thread and the held
 * lock instead of leaving "the console stopped" as the whole observation.
 */
static volatile unsigned int hb_ticks;
static void
f429_wifi_hb(void *arg)
{
	unsigned int n = 0U;

	(void)arg;
	for (;;) {
		osDelay(200U);
		n++;
		hb_ticks = n;
		if (n <= 6U)
			CASE_ANCHOR("f429_wifi: hb=%u\n", n);
		if (n > 6U && (n % 5U) == 0U)
			CASE_ANCHOR("f429_wifi: hb alive n=%u\n", n);
		if (usb_board_irq_storm_get() != 0U && hb_ticks != 0xFFFFU) {
			hb_ticks = 0xFFFFU;	/* report once */
			CASE_ANCHOR("f429_wifi: usb irq storm at hb=%u\n", n);
			usb_board_dwc2_dump();
		}
		if ((n % 10U) == 0U) {
			struct lkpi_net_diag nd;
			struct lkpi_tx_health th;

			(void)memset(&nd, 0, sizeof(nd));
			lkpi_net_diag(&nd);
			(void)memset(&th, 0, sizeof(th));
			lkpi_tx_health_get(&th);
			CASE_ANCHOR("f429_wifi: hb stat n=%u net_rx=%lu net_tx=%lu net_allocfail=%lu txdeq=%lu txstat=%lu data=%lu\n",
				    n, nd.rx_frames, nd.tx_frames,
				    nd.rx_alloc_fail, th.dequeue_frames,
				    th.tx_status, th.status_data);
		}
		if ((n % 20U) == 0U) {
			TaskStatus_t tasks[24];
			uint32_t total = 0U;
			unsigned int tn, i;
			struct lkpi_wq_census cen[LKPI_WQ_CENSUS_MAX];
			unsigned int wn;

			CASE_ANCHOR("f429_wifi: hb wedge-check n=%u\n", n);
			tn = (unsigned int)uxTaskGetSystemState(tasks, 24U,
								&total);
			for (i = 0U; i < tn; i++)
				CASE_ANCHOR("f429_wifi: hbn task[%u] name=%s state=%u prio=%u\n",
					    i, tasks[i].pcTaskName,
					    (unsigned)tasks[i].eCurrentState,
					    (unsigned)tasks[i].uxCurrentPriority);
			wn = lkpi_wq_census(cen, LKPI_WQ_CENSUS_MAX);
			for (i = 0U; i < wn; i++)
				CASE_ANCHOR("f429_wifi: hbn wq[%u] name=%s runs=%u depth=%u busy=%u cur=%p st=%u\n",
					    i, cen[i].name, cen[i].runs,
					    cen[i].depth, cen[i].busy, cen[i].cur,
					    cen[i].thread_state);
		}
	}
}

/* wiphy work probe: mac80211's ifmgd timer callback queues sdata->work on the
 * wiphy work list (mlme.c: ieee80211_sta_timer -> wiphy_work_queue), and every
 * MLME step after the authentication runs from that work. */
static struct wiphy_work ww_probe;
static volatile unsigned int ww_probe_ran;
static void
ww_probe_fn(struct wiphy *wiphy, struct wiphy_work *work)
{
	(void)wiphy;
	(void)work;
	ww_probe_ran++;
}

/* Delayed-work probe: mac80211 sends the association request from the ifmgd
 * work armed with a ~200 ms delay (mlme.c: ieee80211_mgd_assoc -> run_again),
 * so the timer -> workqueue path is on the critical path between
 * "authenticated" and "associate with". */
/* timer_list probe: mac80211's ifmgd timer is a struct timer_list (mlme.c:
 * run_again -> mod_timer), and the association request is sent when that
 * timer expires and queues the ifmgd work.  A fresh delayed_work (above) can
 * work while a re-armed timer_list does not, so both paths are probed. */
#include <linux/timer.h>
static struct timer_list tlprobe;
static struct timer_list tlprobe2;
static volatile unsigned int tlprobe_ran;
static volatile unsigned int tlprobe2_ran;
static void
tlprobe_fn(struct timer_list *t)
{
	(void)t;
	tlprobe_ran++;
}
static void
tlprobe2_fn(struct timer_list *t)
{
	(void)t;
	tlprobe2_ran++;
}
static struct delayed_work dwprobe;
static volatile unsigned int dwprobe_ran;
static void
dwprobe_fn(struct work_struct *w)
{
	(void)w;
	dwprobe_ran++;
}

/*
 * W5 diagnostic seams.  After the authentication the cfg80211 SME queues its
 * connection work, which calls cfg80211_mlme_assoc(); the association request
 * never appeared on the wire, and both steps are silent on the ordinary
 * console.  These two -Wl,--wrap shims (the component's seam style, not a
 * stack edit) say whether each step is entered and what the association call
 * returns.  The flags are added for the f429_wifi image only.
 */
struct cfg80211_registered_device;
struct netlink_ext_ack;

void __real_cfg80211_sme_rx_auth(struct wireless_dev *wdev, const u8 *buf,
				 size_t len);
void
__wrap_cfg80211_sme_rx_auth(struct wireless_dev *wdev, const u8 *buf, size_t len)
{
	CASE_ANCHOR("f429_wifi: sme_rx_auth conn=%p\n", (void *)(wdev != NULL ?
		    wdev->conn : NULL));
	__real_cfg80211_sme_rx_auth(wdev, buf, len);
}

/*
 * W6 A-MPDU-off arm.  After the first QoS data frame the driver starts a TX
 * Block-Ack session and the chip's aggregation engine then swallows the
 * per-frame URBs (the AP sees nothing on air while the USB transfers all
 * complete).  The MCU image does not start TX BA sessions: the wrap refuses
 * the session, the driver keeps AGG_ENABLE off and sends every frame as its
 * own USB transfer.  Report-only consequence: no A-MPDU, so these numbers are
 * not the RK line's.  Linked only into the f429_wifi image.
 */
struct ieee80211_sta;
int __real_ieee80211_start_tx_ba_session(struct ieee80211_sta *pubsta,
					 u16 tid, u16 timeout);
int
__wrap_ieee80211_start_tx_ba_session(struct ieee80211_sta *pubsta, u16 tid,
				     u16 timeout)
{
	(void)pubsta;
	(void)tid;
	(void)timeout;
	return -EINVAL;
}

struct ieee80211_hw;
struct ieee80211_sub_if_data;
void __real_ieee80211_scan_completed(struct ieee80211_hw *hw,
				     struct cfg80211_scan_info *info);
void
__wrap_ieee80211_scan_completed(struct ieee80211_hw *hw,
				struct cfg80211_scan_info *info)
{
	CASE_ANCHOR("f429_wifi: scan_completed aborted=%d\n",
		    info != NULL ? (int)info->aborted : -1);
	__real_ieee80211_scan_completed(hw, info);
}

void __real_ieee80211_sta_work(struct ieee80211_sub_if_data *sdata);
void
__wrap_ieee80211_sta_work(struct ieee80211_sub_if_data *sdata)
{
	CASE_ANCHOR("f429_wifi: sta_work enter\n");
	__real_ieee80211_sta_work(sdata);
}

int __real_cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			       struct net_device *dev,
			       struct cfg80211_assoc_request *req,
			       struct netlink_ext_ack *extack);
int
__wrap_cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			   struct net_device *dev,
			   struct cfg80211_assoc_request *req,
			   struct netlink_ext_ack *extack)
{
	int rc;

	CASE_ANCHOR("f429_wifi: mlme_assoc enter bss=%p\n",
		    (void *)(req != NULL ? req->bss : NULL));
	rc = __real_cfg80211_mlme_assoc(rdev, dev, req, extack);
	CASE_ANCHOR("f429_wifi: mlme_assoc rc=%d\n", rc);
	return rc;
}

/* The driver's own probe lines are the W3 evidence.  The firmware revision is
 * promoted to an anchor; the rest is filtered to the driver/USB prefix so a
 * 115200 console is not flooded. */
static void
fw_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	if (strstr(line, "Firmware revision") != NULL) {
		strncpy(fw_line, line, sizeof(fw_line) - 1U);
		fw_line[sizeof(fw_line) - 1U] = '\0';
		CASE_ANCHOR("f429_wifi: firmware %s\n", line);
		return;
	}
	if (strstr(line, "rtl8xxxu") != NULL || strstr(line, "usbh") != NULL ||
	    strstr(line, "wlan0") != NULL || strstr(line, "cfg80211") != NULL)
		CASE_INFO("f429_wifi: %s\n", line);
}

static void
usb_ledger(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("f429_wifi: usb submits=%u completes=%u refused=%u "
		    "stage_overflow=%u slots_exhausted=%u stage_size=%u slots=%u "
		    "rx=%u rx_err=%u tx=%u tx_err=%u tx_last_err=%d tx_len=%u\n",
		    st.submits, st.completed, st.refused, st.stage_overflow,
		    st.slots_exhausted, st.stage_size, st.stage_slots,
		    st.rx_completes, st.rx_errors, st.tx_completes, st.tx_errors,
		    st.tx_last_err, st.tx_last_len);
}

int
case_f429_wifi_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	unsigned char bssid[6] = { 0 };
	unsigned int waited = 0U;
	int freq = 0;
	int sig = 0;
	int rc;

	CASE_ANCHOR("f429_wifi: stage=%u\n", (unsigned)CONFIG_F429_WIFI_STAGE);

	/* The driver's own debug knobs (module params, no driver bytes): channel
	 * changes and the per-frame TX descriptor rate.  The STA bring-up's first
	 * open failure was "mac80211 queued and statused two management frames,
	 * the AP saw none": these two bits name the channel the config op
	 * applied and the rate the TX descriptor got. */
	{
		extern int rtl8xxxu_debug;

		rtl8xxxu_debug = 0x10 | 0x20 | 0x200 | 0x4000;
					/* CHANNEL | TX | USB | INTERRUPT */
		log_level_set(ULOG_DEBUG_LEVEL);
	}
	CASE_ANCHOR("f429_wifi: heap=%s\n",
#if defined(CONFIG_F429_HEAP_SDRAM)
		    "sdram"
#else
		    "sram"
#endif
	    );

	lkpi_core_init();
	CASE_ANCHOR("f429_wifi: core ready\n");
	{
		unsigned int waited_wq = 0U;

		INIT_WORK(&syswq_probe, syswq_probe_fn);
		schedule_work(&syswq_probe);
		while (waited_wq < 200U && syswq_ran == 0U) {
			osDelay(10U);
			waited_wq += 10U;
		}
		CASE_ANCHOR("f429_wifi: syswq ran=%u waited_ms=%u\n",
			    syswq_ran, waited_wq);
	}
	{
		unsigned int waited_dw = 0U;

		INIT_DELAYED_WORK(&dwprobe, dwprobe_fn);
		schedule_delayed_work(&dwprobe, 200U);
		while (waited_dw < 2000U && dwprobe_ran == 0U) {
			osDelay(10U);
			waited_dw += 10U;
		}
		CASE_ANCHOR("f429_wifi: dwprobe ran=%u waited_ms=%u\n",
			    dwprobe_ran, waited_dw);
	}
	{
		unsigned int waited_tl = 0U;

		timer_setup(&tlprobe, tlprobe_fn, 0);
		mod_timer(&tlprobe, jiffies + msecs_to_jiffies(200));
		while (waited_tl < 2000U && tlprobe_ran == 0U) {
			osDelay(10U);
			waited_tl += 10U;
		}
		CASE_ANCHOR("f429_wifi: tlprobe ran=%u waited_ms=%u\n",
			    tlprobe_ran, waited_tl);
	}
	{
		/* Re-arm: arm, let it fire, arm again.  mac80211's ifmgd timer
		 * is mod_timer()'d repeatedly (auth timeout then assoc wait). */
		unsigned int w2 = 0U;

		timer_setup(&tlprobe2, tlprobe2_fn, 0);
		mod_timer(&tlprobe2, jiffies + msecs_to_jiffies(100));
		while (w2 < 500U && tlprobe2_ran == 0U) {
			osDelay(10U);
			w2 += 10U;
		}
		CASE_ANCHOR("f429_wifi: tlprobe2 first=%u waited_ms=%u\n",
			    tlprobe2_ran, w2);
		mod_timer(&tlprobe2, jiffies + msecs_to_jiffies(100));
		w2 = 0U;
		while (w2 < 500U && tlprobe2_ran < 2U) {
			osDelay(10U);
			w2 += 10U;
		}
		CASE_ANCHOR("f429_wifi: tlprobe2 rearm=%u waited_ms=%u\n",
			    tlprobe2_ran, w2);
	}
	lkpi_log_sink_register(fw_sink);

	lkpi_usb_cherryusb_init();
	CASE_ANCHOR("f429_wifi: usb backend ready\n");

	lkpi_module_init_run();
	CASE_ANCHOR("f429_wifi: initcalls run\n");

	lkpi_usb_init();
	CASE_ANCHOR("f429_wifi: usb drivers=%u\n",
		    (unsigned)lkpi_usb_driver_count_get());

	lkpi_usb_force_sync_set(false);
	if (rtl8xxxu_fw_register() != 0) {
		CASE_ANCHOR("f429_wifi: FAIL firmware blob registration\n");
		return 1;
	}
	CASE_ANCHOR("f429_wifi: fw registered=0 name=%s\n", RTL8XXXU_FW_NAME);

	usb_osal_timer_service_start();
	if (usb_board_init(0) != 0) {
		CASE_ANCHOR("f429_wifi: FAIL usb_board_init bus0\n");
		return 1;
	}
	if (usbh_initialize(0, usb_board_reg_base(0),
			    usb_board_event_handler) != 0) {
		CASE_ANCHOR("f429_wifi: FAIL usbh_initialize bus0\n");
		return 1;
	}
	usb_board_after_start(0);
	CASE_ANCHOR("f429_wifi: usb bus=0 hcd=dwc2 base=0x%lx started\n",
		    (unsigned long)usb_board_reg_base(0));

	/* The driver's probe runs from CherryUSB's hub thread once the vendor
	 * interface is claimed; it is slow by design (efuse, 8051 reset,
	 * firmware download, PHY init).  wlan0 appearing is the register + attach
	 * evidence (W2/W3 in one line). */
	for (waited = 0U; waited < F429_WIFI_WAIT_MS; waited += F429_WIFI_POLL_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(F429_WIFI_POLL_MS);
	}
	CASE_ANCHOR("f429_wifi: wlan0 present=%d after=%u ms\n",
		    dev != NULL, (unsigned)waited);
	if (dev == NULL) {
		CASE_ANCHOR("f429_wifi: FAIL attach timeout (no dongle claimed or "
			    "probe failed; see the lines above)\n");
		usb_ledger();
		return 2;
	}

	CASE_ANCHOR("f429_wifi: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    dev->ifindex, dev->dev_addr[0], dev->dev_addr[1],
		    dev->dev_addr[2], dev->dev_addr[3], dev->dev_addr[4],
		    dev->dev_addr[5]);

	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("f429_wifi: FAIL no wireless_dev\n");
		return 3;
	}
	{
		struct wiphy *wiphy = wdev->wiphy;
		int bands = 0, managed = 0, ht = 0, vht = 0;

		if (wiphy->bands[NL80211_BAND_2GHZ] != NULL) {
			bands++;
			if (wiphy->bands[NL80211_BAND_2GHZ]->ht_cap.ht_supported)
				ht = 1;
			if (wiphy->bands[NL80211_BAND_2GHZ]->vht_cap.vht_supported)
				vht = 1;
		}
		managed = !!(wiphy->interface_modes & BIT(NL80211_IFTYPE_STATION));
		CASE_ANCHOR("f429_wifi: wiphy bands=%d managed=%d ht=%d vht=%d\n",
			    bands, managed, ht, vht);
	}

	if (fw_line[0] == '\0') {
		CASE_ANCHOR("f429_wifi: FAIL no firmware revision line\n");
		usb_ledger();
		return 4;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("f429_wifi: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("f429_wifi: FAIL start rc=%d\n", rc);
		usb_ledger();
		return 5;
	}

#if CONFIG_F429_WIFI_STAGE >= 4
	{
		unsigned int waited_ww = 0U;

		wiphy_work_init(&ww_probe, ww_probe_fn);
		wiphy_work_queue(wdev->wiphy, &ww_probe);
		while (waited_ww < 1000U && ww_probe_ran == 0U) {
			osDelay(10U);
			waited_ww += 10U;
		}
		CASE_ANCHOR("f429_wifi: wiphy_work ran=%u waited_ms=%u\n",
			    ww_probe_ran, waited_ww);
	}

	/* W4: the directed scan on the bench AP's channel, then the wildcard
	 * retry.  The scan is mac80211's SW scan (rtl8xxxu has no hw_scan). */
	rc = lkpi_nl80211_trigger_scan(dev, F429_WIFI_SSID, F429_WIFI_FREQ);
	CASE_ANCHOR("f429_wifi: scan ssid=%s freq=%u rc=%d\n",
		    F429_WIFI_SSID, (unsigned)F429_WIFI_FREQ, rc);
	if (rc != 0) {
		CASE_ANCHOR("f429_wifi: FAIL scan rc=%d\n", rc);
		usb_ledger();
		return 6;
	}
	for (waited = 0U; waited < F429_WIFI_SCAN_WAIT_MS;
	     waited += F429_WIFI_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, F429_WIFI_SSID,
					  bssid, &freq, &sig) == 0)
			goto found;
		osDelay(F429_WIFI_POLL_MS);
	}
	rc = lkpi_nl80211_trigger_scan(dev, NULL, 0);
	CASE_ANCHOR("f429_wifi: scan wildcard rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("f429_wifi: FAIL wildcard scan rc=%d\n", rc);
		usb_ledger();
		return 7;
	}
	for (waited = 0U; waited < F429_WIFI_SCAN_WAIT_MS;
	     waited += F429_WIFI_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, F429_WIFI_SSID,
					  bssid, &freq, &sig) == 0)
			goto found;
		osDelay(F429_WIFI_POLL_MS);
	}
	CASE_ANCHOR("f429_wifi: FAIL bss timeout (scan started, no BSS)\n");
	usb_ledger();
	return 8;

found:
	CASE_ANCHOR("f429_wifi: bss ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x "
		    "freq=%d signal=%d\n",
		    F429_WIFI_SSID, bssid[0], bssid[1], bssid[2], bssid[3],
		    bssid[4], bssid[5], freq, sig / 100);
	CASE_ANCHOR("f429_wifi: PASS W4\n");

#if CONFIG_F429_WIFI_STAGE >= 5
	/* W5: NL80211_CMD_CONNECT (open AP) through cfg80211/mac80211's own
	 * SME, then wait until cfg80211 marks the station connected.  The AP-side
	 * witness (iw station dump) is the external half of this gate. */
	{
		unsigned char cbssid[6] = { 0 };
		unsigned int connect_ms = 0U;
		int connected = 0;

		/* W5: let the scan finish before connecting.  mac80211 drops every
		 * iface work item (including the association the MLME timer queues)
		 * while SCAN_SW_SCANNING is set; the MCU's scan is far slower than the
		 * RK line's because every channel write is a USB full-speed control
		 * transfer, so the connect must not race it. */
		osDelay(2000U);
		CASE_ANCHOR("f429_wifi: scan settle done\n");

		rc = lkpi_nl80211_connect(dev, F429_WIFI_SSID, bssid,
					  (unsigned int)freq);
		CASE_ANCHOR("f429_wifi: connect ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x freq=%d rc=%d\n",
			    F429_WIFI_SSID, bssid[0], bssid[1], bssid[2],
			    bssid[3], bssid[4], bssid[5], freq, rc);
		if (rc != 0) {
			CASE_ANCHOR("f429_wifi: FAIL connect rc=%d\n", rc);
			usb_ledger();
			return 9;
		}
		for (connect_ms = 0U; connect_ms < F429_WIFI_CONNECT_WAIT_MS;
		     connect_ms += F429_WIFI_POLL_MS) {
			connected = lkpi_nl80211_connected(dev, cbssid);
			if (connected == 1)
				break;
			if (connect_ms == 2000U)
				CASE_ANCHOR("f429_wifi: assoc_progress=%d at_ms=%u\n",
					    lkpi_nl80211_assoc_progress(dev,
						F429_WIFI_SSID,
						(unsigned int)strlen(F429_WIFI_SSID)),
					    connect_ms);
			osDelay(F429_WIFI_POLL_MS);
		}
		if (connected != 1) {
			struct lkpi_tx_health th;
			unsigned int alive_ms = 0U;

			/* Is the system worker still schedulable, or is the pending
			 * item wedged?  A probe that runs says the queue is alive and
			 * the pending entry is what is stuck. */
			syswq_ran = 0U;
			schedule_work(&syswq_probe);
			while (alive_ms < 1000U && syswq_ran == 0U) {
				osDelay(10U);
				alive_ms += 10U;
			}
			CASE_ANCHOR("f429_wifi: syswq alive=%u waited_ms=%u cur_work=%p\n",
				    syswq_ran, alive_ms,
				    lkpi_wq_current_work_func());

			{
				struct lkpi_wq_census cen[LKPI_WQ_CENSUS_MAX];
				unsigned int n, i;

				n = lkpi_wq_census(cen, LKPI_WQ_CENSUS_MAX);
				CASE_ANCHOR("f429_wifi: wq_census n=%u\n", n);
				for (i = 0U; i < n; i++)
					CASE_ANCHOR("f429_wifi: wq[%u] name=%s runs=%u refused=%u depth=%u busy=%u cur=%p st=%u\n",
						    i, cen[i].name,
						    cen[i].runs, cen[i].refused,
						    cen[i].depth, cen[i].busy,
						    cen[i].cur,
						    cen[i].thread_state);
			}
			{
				TaskStatus_t tasks[24];
				uint32_t total = 0U;
				unsigned int n, i;

				n = (unsigned int)uxTaskGetSystemState(tasks, 24U,
								       &total);
				CASE_ANCHOR("f429_wifi: task_census n=%u total=%u\n",
					    n, (unsigned)total);
				for (i = 0U; i < n; i++)
					CASE_ANCHOR("f429_wifi: task[%u] name=%s state=%u prio=%u hwm=%u\n",
						    i, tasks[i].pcTaskName,
						    (unsigned)tasks[i].eCurrentState,
						    (unsigned)tasks[i].uxCurrentPriority,
						    (unsigned)tasks[i].usStackHighWaterMark);
			}
			lkpi_tx_health_get(&th);
			CASE_ANCHOR("f429_wifi: txhealth dequeue=%lu tx_status=%lu "
				    "mgmt=%lu data=%lu stop=%lu wake=%lu\n",
				    th.dequeue_frames, th.tx_status,
				    th.status_mgmt, th.status_data,
				    th.stop_queue, th.wake_queue);
			CASE_ANCHOR("f429_wifi: FAIL connect timeout waited_ms=%u\n",
				    connect_ms);
			usb_ledger();
			(void)lkpi_nl80211_disconnect(dev, 3u);
			return 10;
		}
		CASE_ANCHOR("f429_wifi: connected bssid=%02x:%02x:%02x:%02x:%02x:%02x waited_ms=%u\n",
			    cbssid[0], cbssid[1], cbssid[2], cbssid[3],
			    cbssid[4], cbssid[5], connect_ms);
		{
			struct lkpi_tx_health th;

			{
				struct lkpi_wq_census cen[LKPI_WQ_CENSUS_MAX];
				unsigned int n, i;

				n = lkpi_wq_census(cen, LKPI_WQ_CENSUS_MAX);
				CASE_ANCHOR("f429_wifi: wq_census n=%u\n", n);
				for (i = 0U; i < n; i++)
					CASE_ANCHOR("f429_wifi: wq[%u] name=%s runs=%u refused=%u depth=%u busy=%u cur=%p st=%u\n",
						    i, cen[i].name,
						    cen[i].runs, cen[i].refused,
						    cen[i].depth, cen[i].busy,
						    cen[i].cur,
						    cen[i].thread_state);
			}
			{
				TaskStatus_t tasks[24];
				uint32_t total = 0U;
				unsigned int n, i;

				n = (unsigned int)uxTaskGetSystemState(tasks, 24U,
								       &total);
				CASE_ANCHOR("f429_wifi: task_census n=%u total=%u\n",
					    n, (unsigned)total);
				for (i = 0U; i < n; i++)
					CASE_ANCHOR("f429_wifi: task[%u] name=%s state=%u prio=%u hwm=%u\n",
						    i, tasks[i].pcTaskName,
						    (unsigned)tasks[i].eCurrentState,
						    (unsigned)tasks[i].uxCurrentPriority,
						    (unsigned)tasks[i].usStackHighWaterMark);
			}
			lkpi_tx_health_get(&th);
			CASE_ANCHOR("f429_wifi: txhealth dequeue=%lu tx_status=%lu "
				    "mgmt=%lu data=%lu stop=%lu wake=%lu\n",
				    th.dequeue_frames, th.tx_status,
				    th.status_mgmt, th.status_data,
				    th.stop_queue, th.wake_queue);
		}
		/* Bench control, the same command the RK line and the GT arm run:
		 * mac80211 enables PS on association, and a PS-on link answers on
		 * the beacon wake-up. */
		rc = lkpi_nl80211_set_power_save(dev, 0);
		CASE_ANCHOR("f429_wifi: power save off rc=%d\n", rc);
		CASE_ANCHOR("f429_wifi: PASS W5\n");
	}
#endif /* CONFIG_F429_WIFI_STAGE >= 5 */
#if CONFIG_F429_WIFI_STAGE >= 6
	/* W6: the data plane.  The lwIP netif is assembled over wlan0 with the
	 * lab's static address, two counted ICMP rounds are the functional
	 * gate, and (with CONFIG_F429_WIFI_TP) the four report-only phases run
	 * against the Pi's iperf2 arms.  No phase applies a pass threshold. */
	{
		char netif_mac[6];
		unsigned int rx = 0U, rmin = 0U, ravg = 0U, rmax = 0U;
		unsigned int small_rx = 0U, big_rx = 0U, warm = 0U;

		{
			osThreadAttr_t attr;

			(void)memset(&attr, 0, sizeof(attr));
			attr.name = "f429-wifi-hb";
			attr.priority = osPriorityAboveNormal;
			attr.stack_size = 4096U;
			(void)osThreadNew((osThreadFunc_t)f429_wifi_hb, NULL,
					  &attr);
		}
		osDelay(700U);
		CASE_ANCHOR("f429_wifi: hb up ticks=%u\n", hb_ticks);

		(void)memset(netif_mac, 0, sizeof(netif_mac));
		rc = lkpi_net_wlan_up("192.168.99.2", "255.255.255.0",
				      "192.168.99.1", netif_mac);
		CASE_ANCHOR("f429_wifi: netif up rc=%d ip=192.168.99.2/24 "
			    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
			    rc, (unsigned char)netif_mac[0],
			    (unsigned char)netif_mac[1],
			    (unsigned char)netif_mac[2],
			    (unsigned char)netif_mac[3],
			    (unsigned char)netif_mac[4],
			    (unsigned char)netif_mac[5]);
		if (rc != 0) {
			CASE_ANCHOR("f429_wifi: FAIL netif up rc=%d\n", rc);
			usb_ledger();
			return 11;
		}

		/* Warm-up: the first echo may race the netif's neighbour setup. */
		for (warm = 0U; warm < 20U; warm++) {
			CASE_ANCHOR("f429_wifi: ping warmup call=%u\n",
				    warm + 1U);
			rc = lkpi_net_ping("192.168.99.1", 1U, 56U, 1000U,
					   &rx, &rmin, &ravg, &rmax);
			CASE_ANCHOR("f429_wifi: ping warmup ret=%d rx=%u\n",
				    rc, rx);
			if (rc == 0 && rx == 1U)
				break;
			osDelay(250U);
		}
		CASE_ANCHOR("f429_wifi: ping warmup tries=%u rx=%u rc=%d irq_storm=%u\n",
			    warm + 1U, rx, rc, usb_board_irq_storm_get());

		rc = lkpi_net_ping("192.168.99.1", 20U, 56U, 1000U,
				   &rx, &rmin, &ravg, &rmax);
		small_rx = rx;
		CASE_ANCHOR("f429_wifi: ping small tx=20 rx=%u payload=56 "
			    "rtt_ms=%u/%u/%u rc=%d\n",
			    rx, rmin, ravg, rmax, rc);

		rc = lkpi_net_ping("192.168.99.1", 20U, 1400U, 1000U,
				   &rx, &rmin, &ravg, &rmax);
		big_rx = rx;
		CASE_ANCHOR("f429_wifi: ping big tx=20 rx=%u payload=1400 "
			    "rtt_ms=%u/%u/%u rc=%d\n",
			    rx, rmin, ravg, rmax, rc);

		{
			struct lkpi_net_diag nd;

			(void)memset(&nd, 0, sizeof(nd));
			lkpi_net_diag(&nd);
			CASE_ANCHOR("f429_wifi: net rx_frames=%lu tx_frames=%lu "
				    "rx_bytes=%lu tx_bytes=%lu rx_alloc_fail=%lu "
				    "tx_fail=%lu\n",
				    nd.rx_frames, nd.tx_frames, nd.rx_bytes,
				    nd.tx_bytes, nd.rx_alloc_fail, nd.tx_fail);
		}

		if (small_rx == 20U && big_rx == 20U)
			CASE_ANCHOR("f429_wifi: PASS W6\n");
		else
			CASE_ANCHOR("f429_wifi: FAIL ping small=%u big=%u\n",
				    small_rx, big_rx);

#if CONFIG_F429_WIFI_TP
		/* Report-only throughput.  Each phase waits for its peer inside
		 * its own window; the harness starts the Pi's arms (iperf2
		 * client/server and the UDP counting sink) around this boot. */
		{
			struct lkpi_net_phase ph;
			unsigned int moved = 0U;

			rc = lkpi_net_iperf_server_start(5001U);
			CASE_ANCHOR("f429_wifi: iperf server start rc=%d port=5001\n",
				    rc);

			(void)memset(&ph, 0, sizeof(ph));
			rc = lkpi_net_iperf_down(5001U, 25000U, &ph);
			CASE_ANCHOR("f429_wifi: phase tcpdn rc=%d done=%d "
				    "bytes=%u ms=%u kbit=%u\n",
				    rc, ph.done, ph.bytes, ph.ms, ph.kbit);
			if (ph.bytes)
				moved++;

			(void)memset(&ph, 0, sizeof(ph));
			rc = lkpi_net_iperf_up("192.168.99.1", 5001U, 15000U,
					       &ph);
			CASE_ANCHOR("f429_wifi: phase tcpup rc=%d done=%d "
				    "bytes=%u ms=%u kbit=%u\n",
				    rc, ph.done, ph.bytes, ph.ms, ph.kbit);
			if (ph.bytes)
				moved++;

			rc = lkpi_net_udp_sink_start(5002U);
			(void)memset(&ph, 0, sizeof(ph));
			rc = lkpi_net_udp_sink_window(20000U, &ph);
			CASE_ANCHOR("f429_wifi: phase udpdn rc=%d done=%d "
				    "bytes=%u ms=%u kbit=%u pkts=%u drop=%u\n",
				    rc, ph.done, ph.bytes, ph.ms, ph.kbit,
				    ph.pkts, ph.rx_drop);
			if (ph.bytes)
				moved++;

			(void)memset(&ph, 0, sizeof(ph));
			rc = lkpi_net_udp_blast("192.168.99.1", 5003U, 12000U,
						40000U, &ph);
			CASE_ANCHOR("f429_wifi: phase udpup rc=%d done=%d "
				    "offered=%u bytes=%u ms=%u kbit=%u\n",
				    rc, ph.done, ph.offered, ph.bytes, ph.ms,
				    ph.kbit);
			if (ph.bytes)
				moved++;

			CASE_ANCHOR("f429_wifi: throughput phases moved=%u/4\n",
				    moved);
		}
#endif /* CONFIG_F429_WIFI_TP */
	}
#endif /* CONFIG_F429_WIFI_STAGE >= 6 */
#else
	CASE_ANCHOR("f429_wifi: PASS W3\n");
#endif /* CONFIG_F429_WIFI_STAGE >= 4 */
	usb_ledger();
	return 0;
}
