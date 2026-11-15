/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8xxxu_c5.c
 * @brief  Case rtl8xxxu_c5: the RTL8188EUS joins the open bench AP (C5-open)
 *         and moves ICMP through the lwIP netif (C6-open).
 *
 * The bring-up is C4's (USB domain, engine, firmware, wlan0, dev_open) and the
 * scan is C4's helper.  What is new is the connection: NL80211_CMD_CONNECT is
 * built as the real attribute stream `iw connect` would send and dispatched
 * through nl80211's own handler chain; mac80211 runs auth/assoc asynchronously,
 * and the case waits (bounded) for cfg80211 to mark the station connected.
 * Then the lwIP netif is assembled over wlan0 with the lab's static address and
 * two ICMP rounds (56 B and 1400 B, matching the GT baseline) are sent.
 *
 * Two reads are this card's own: the power-save state is turned off through the
 * real nl80211 handler (mac80211 enables PS on association, and an RTT measured
 * with PS on carries the beacon wake-up), and `lkpi_tx_health` is read around
 * each round, because a blind rate control shows up as tx_status << dequeue
 * even when the USB engine completed every URB.
 *
 * What this is not: the WPA2 round (no supplicant, no EAPOL, no key install),
 * the AP role, the monitor role, or the throughput arm.
 *
 * Anchors:
 *   rtl8xxxu_c5: core ready
 *   rtl8xxxu_c5: usb backend ready
 *   rtl8xxxu_c5: initcalls run
 *   rtl8xxxu_c5: usb drivers=<n>
 *   rtl8xxxu_c5: usb buses=3,2
 *   rtl8xxxu_c5: usb bus=<n> hcd=<ehci|xhci> base=0x... started
 *   rtl8xxxu_c5: firmware <driver line>
 *   rtl8xxxu_c5: wlan0 present=1 ifindex=<n> mac=<mac>
 *   rtl8xxxu_c5: start rc=<n>
 *   rtl8xxxu_c5: scan ssid=<ssid> freq=<mhz> rc=<n>
 *   rtl8xxxu_c5: bss ssid=<ssid> bssid=<mac> freq=<mhz> signal=<dbm>
 *   rtl8xxxu_c5: connect ssid=<ssid> bssid=<mac> freq=<mhz> rc=<n>
 *   rtl8xxxu_c5: connected bssid=<mac> waited_ms=<n>
 *   rtl8xxxu_c5: PASS C5
 *   rtl8xxxu_c5: netif up name=<n> ip=<ip>/<mask> mac=<mac>
 *   rtl8xxxu_c5: power save off rc=<n>
 *   rtl8xxxu_c5: warmup tries=<n> rx=<n> rc=<n>
 *   rtl8xxxu_c5: peer window ms=<n>
 *   rtl8xxxu_c5: ping tx=<n> rx=<n> payload=<n> rtt_ms=<min>/<avg>/<max>
 *   rtl8xxxu_c5: txhealth dequeue=<n> tx_status=<n> mgmt=<n> data=<n> stop=<n>
 *              wake=<n> ps_sleep=<n> ps_wake=<n> ps_buffered=<n>
 *   rtl8xxxu_c5: diag rx_frames=<n> tx_frames=<n> rx_bytes=<n> tx_bytes=<n> tx_fail=<n>
 *   rtl8xxxu_c5: PASS C6
 *   rtl8xxxu_c5: PASS
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "rtl8xxxu_fw.h"
#include "os/cmsis2/cmsis2.h"

/* CherryUSB first (see rtl8xxxu_c3.c): its <stdlib.h> must be parsed before the
 * Linux face's compiler-attributes header is. */
#include "usbh_core.h"
#include "usb_glue.h"
#include "usb_domain.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"
#include "linux80211/hostap_ap.h"	/* lkpi_tx_health_get */
#include "linuxkpi_usb.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#define C5_SSID		"labwifi-ar-ht20s"
#define C5_FREQ_MHZ	2432u
#define C5_BOARD_IP	"192.168.99.2"
#define C5_BOARD_MASK	"255.255.255.0"
#define C5_AP_IP	"192.168.99.1"
#define C5_WAIT_MS		60000U
#define C5_POLL_MS		100U
#define C5_SCAN_WAIT_MS		10000U
#define C5_CONNECT_WAIT_MS	20000U
#define C5_PING_COUNT		20U
#define C5_PING_TIMEOUT_MS	300U
#define C5_PING_SMALL		56U
#define C5_PING_LARGE		1400U
#define C5_LOG_MAX		120U

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);
void usb_osal_timer_service_start(void);

/*
 * The F429 data plane control arm (CONFIG_RTL8XXXU_TX_CENSUS): this bench drives the
 * same RTL8188EUS with the same driver, so it prints the same chip-state lines
 * and the two boots can be diffed field by field.  Compiled out of every
 * delivery profile; the unit itself lives in the driver world.
 */
#if CONFIG_RTL8XXXU_TX_CENSUS
extern void rtl8xxxu_diag_chip_state(void *wiphy, const char *tag);

static void
c5_chip_state(struct net_device *dev, const char *tag)
{
	struct wireless_dev *wdev;

	if (dev == NULL)
		return;
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL)
		return;
	rtl8xxxu_diag_chip_state(wdev->wiphy, tag);
}
#endif

#if CONFIG_RTL8XXXU_USB_BUS == 0
static const unsigned int c5_buses[] = { 3U, 2U };
#define C5_BUS_LIST_TXT	"3,2"
#else
static const unsigned int c5_buses[] = { 1U, 0U };
#define C5_BUS_LIST_TXT	"1,0"
#endif

static unsigned int c5_log_lines;

static void
c5_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL || c5_log_lines >= C5_LOG_MAX)
		return;
	if (strstr(line, "Firmware revision") != NULL) {
		CASE_ANCHOR("rtl8xxxu_c5: firmware %s\n", line);
		return;
	}
	if (strstr(line, "Loading firmware") != NULL) {
		CASE_ANCHOR("rtl8xxxu_c5: fw image %s\n", line);
		return;
	}
	/* The driver's own info/warn lines at association time and the USB
	 * engine's device/claim events; bounded by C5_LOG_MAX so a chatty probe
	 * cannot eat the anchor ring. */
	if (strstr(line, "rtl8xxxu") != NULL || strstr(line, "usbh") != NULL ||
	    strstr(line, "usbkpi") != NULL) {
		CASE_INFO("rtl8xxxu_c5: drv %s\n", line);
		c5_log_lines++;
		return;
	}
	/* mac80211's own MLME lines are the join's witness when the driver is
	 * silent. */
	if (strstr(line, "authenticate") != NULL ||
	    strstr(line, "associate") != NULL ||
	    strstr(line, "deauthenticate") != NULL ||
	    strstr(line, "disconnect") != NULL ||
	    strstr(line, "add key") != NULL ||
	    strstr(line, "remove key") != NULL) {
		CASE_INFO("rtl8xxxu_c5: drv %s\n", line);
		c5_log_lines++;
	}
}

/* Did the driver ever hand a frame back to mac80211?  tx_status is
 * ieee80211_tx_status_irqsafe - the call rate control's per-frame feedback
 * comes through.  A blind minstrel reads as tx_status << dequeue_frames even
 * when the USB engine completed every URB (tx_fail=0 is not that answer). */
static void
c5_tx_feedback(const struct lkpi_tx_health *before)
{
	struct lkpi_tx_health now;

	(void)memset(&now, 0, sizeof(now));
	lkpi_tx_health_get(&now);
	CASE_ANCHOR("rtl8xxxu_c5: txhealth dequeue=%lu tx_status=%lu mgmt=%lu "
		    "data=%lu stop=%lu wake=%lu ps_sleep=%lu ps_wake=%lu "
		    "ps_buffered=%lu\n",
		    now.dequeue_frames - before->dequeue_frames,
		    now.tx_status - before->tx_status,
		    now.status_mgmt - before->status_mgmt,
		    now.status_data - before->status_data,
		    now.stop_queue - before->stop_queue,
		    now.wake_queue - before->wake_queue,
		    now.ps_sleep - before->ps_sleep,
		    now.ps_wake - before->ps_wake,
		    now.ps_buffered - before->ps_buffered);
}

static void
c5_usb_ledger(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtl8xxxu_c5: usb submits=%u completes=%u refused=%u "
		    "stage_overflow=%u slots_exhausted=%u rx=%u rx_err=%u\n",
		    st.submits, st.completed, st.refused, st.stage_overflow,
		    st.slots_exhausted, st.rx_completes, st.rx_errors);
}

static int
c5_usb_up(void)
{
	unsigned int i;
	int started = 0;

	lkpi_usb_cherryusb_init();
	CASE_ANCHOR("rtl8xxxu_c5: usb backend ready\n");

	lkpi_module_init_run();
	CASE_ANCHOR("rtl8xxxu_c5: initcalls run\n");

	lkpi_usb_init();
	CASE_ANCHOR("rtl8xxxu_c5: usb drivers=%u\n",
		    (unsigned)lkpi_usb_driver_count_get());

	lkpi_usb_force_sync_set(false);
	if (rtl8xxxu_fw_register() != 0) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL firmware blob registration\n");
		return -1;
	}
	CASE_ANCHOR("rtl8xxxu_c5: fw registered=0 name=%s\n", RTL8XXXU_FW_NAME);

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtl8xxxu_c5: usb buses=%s\n", C5_BUS_LIST_TXT);
	for (i = 0U; i < sizeof(c5_buses) / sizeof(c5_buses[0]); i++) {
		unsigned int bus = c5_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtl8xxxu_c5: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtl8xxxu_c5: bus%u usbh_initialize failed\n", bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtl8xxxu_c5: usb bus=%u hcd=%s base=0x%lx started\n", bus,
			    usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
			    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	return started;
}

static int
c5_wait_connected(struct net_device *dev, unsigned char bssid[6],
		  unsigned int *waited_ms)
{
	unsigned int waited = 0U;

	while (waited < C5_CONNECT_WAIT_MS) {
		int connected = lkpi_nl80211_connected(dev, bssid);

		if (connected == 1) {
			*waited_ms = waited;
			return 0;
		}
		if (connected < 0)
			return connected;
		osDelay(C5_POLL_MS);
		waited += C5_POLL_MS;
	}
	*waited_ms = waited;
	return -1;
}

int
case_rtl8xxxu_c5_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	unsigned char bssid[6];
	unsigned char mac[6];
	char netif_mac[6];
	unsigned int waited = 0U;
	unsigned int connected_ms = 0U;
	unsigned int ping_rx = 0U;
	unsigned int rtt_min = 0U, rtt_avg = 0U, rtt_max = 0U;
	struct lkpi_net_diag diag;
	struct lkpi_tx_health th0;
	int freq = 0;
	int sig = 0;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("rtl8xxxu_c5: core ready\n");
	lkpi_log_sink_register(c5_sink);

	rc = c5_usb_up();
	if (rc <= 0) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL no controller started\n");
		c5_usb_ledger();
		return 1;
	}

	while (waited < C5_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(C5_POLL_MS);
		waited += C5_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL no wlan0 after %u ms\n",
			    (unsigned)waited);
		c5_usb_ledger();
		return 2;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("rtl8xxxu_c5: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	rc = dev_open(dev);
	CASE_ANCHOR("rtl8xxxu_c5: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL start rc=%d\n", rc);
		return 3;
	}

	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL no wireless_dev\n");
		return 4;
	}

	/* --- C5-open: scan, connect, wait for the completion ---------------- */

	rc = lkpi_nl80211_trigger_scan(dev, C5_SSID, C5_FREQ_MHZ);
	CASE_ANCHOR("rtl8xxxu_c5: scan ssid=%s freq=%u rc=%d\n", C5_SSID,
		    C5_FREQ_MHZ, rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL scan rc=%d\n", rc);
		return 5;
	}

	for (waited = 0U; waited < C5_SCAN_WAIT_MS; waited += C5_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, C5_SSID, bssid, &freq,
					  &sig) == 0)
			goto found;
		osDelay(C5_POLL_MS);
	}

	rc = lkpi_nl80211_trigger_scan(dev, NULL, 0);
	CASE_ANCHOR("rtl8xxxu_c5: scan wildcard rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL wildcard scan rc=%d\n", rc);
		return 6;
	}
	for (waited = 0U; waited < C5_SCAN_WAIT_MS; waited += C5_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, C5_SSID, bssid, &freq,
					  &sig) == 0)
			goto found;
		osDelay(C5_POLL_MS);
	}
	CASE_ANCHOR("rtl8xxxu_c5: FAIL bss timeout (scan started, no BSS)\n");
	return 7;

found:
	CASE_ANCHOR("rtl8xxxu_c5: bss ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x freq=%d signal=%d\n",
		    C5_SSID, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4],
		    bssid[5], freq, sig / 100);

	rc = lkpi_nl80211_connect(dev, C5_SSID, bssid, (unsigned int)freq);
	CASE_ANCHOR("rtl8xxxu_c5: connect ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x freq=%d rc=%d\n",
		    C5_SSID, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4],
		    bssid[5], freq, rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL connect rc=%d\n", rc);
		return 8;
	}

	rc = c5_wait_connected(dev, bssid, &connected_ms);
	if (rc != 0) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL connect timeout waited_ms=%u\n",
			    connected_ms);
		c5_usb_ledger();
		(void)lkpi_nl80211_disconnect(dev, 3u);
		return 9;
	}
	CASE_ANCHOR("rtl8xxxu_c5: connected bssid=%02x:%02x:%02x:%02x:%02x:%02x waited_ms=%u\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    connected_ms);
	CASE_ANCHOR("rtl8xxxu_c5: PASS C5\n");

	/* The station entry is authorized by the association for an open AP;
	 * a short settle keeps the first ARP out of the completion's tail. */
	osDelay(200u);

	/* --- C6-open: the lwIP netif and two ICMP rounds ------------------- */

	rc = lkpi_net_wlan_up(C5_BOARD_IP, C5_BOARD_MASK, C5_AP_IP, netif_mac);
	if (rc != 0) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL netif up rc=%d\n", rc);
		return 10;
	}
	CASE_ANCHOR("rtl8xxxu_c5: netif up name=wl0 ip=%s/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    C5_BOARD_IP, (unsigned char)netif_mac[0],
		    (unsigned char)netif_mac[1], (unsigned char)netif_mac[2],
		    (unsigned char)netif_mac[3], (unsigned char)netif_mac[4],
		    (unsigned char)netif_mac[5]);

	/* Bench control, same as the GT arm's `iw dev <nd> set power_save off`:
	 * mac80211 enables PS on association, and an RTT measured with PS on
	 * carries the beacon-interval wake-up.  The command is the real nl80211
	 * handler, not a stack field poke. */
	rc = lkpi_nl80211_set_power_save(dev, 0);
	CASE_ANCHOR("rtl8xxxu_c5: power save off rc=%d\n", rc);
#if CONFIG_RTL8XXXU_TX_CENSUS
	/* The control arm's post-association snapshot: on the RK bench this is
	 * the known-good value the F429 boot is diffed against. */
	c5_chip_state(dev, "post-assoc");
#endif

	/* The GT arm waits for its neighbour before its 20-packet round; the
	 * RTOS link needs the same bench step.  The first ~2 s after netif up
	 * carry the board's ARP resolution and the PS-leave/rate settle, so warm
	 * the path with single pings before the counted rounds. */
	{
		unsigned int warm_rx = 0U, warm_i;
		unsigned int wmin = 0U, wavg = 0U, wmax = 0U;

		for (warm_i = 0U; warm_i < 20U; warm_i++) {
			rc = lkpi_net_ping(C5_AP_IP, 1U, C5_PING_SMALL,
					   C5_PING_TIMEOUT_MS, &warm_rx,
					   &wmin, &wavg, &wmax);
			if (rc == 0 && warm_rx == 1U)
				break;
			osDelay(250u);
		}
		CASE_ANCHOR("rtl8xxxu_c5: warmup tries=%u rx=%u rc=%d\n",
			    warm_i + 1U, warm_rx, rc);
	}
	osDelay(3000u);

	/* The peer-side arm (the Pi pings the board) is driven from the host by
	 * this anchor.  The window is the bench's, not the stack's: it must be
	 * long enough for the host's neighbour lookup plus its two 20-packet
	 * rounds, or the last packets land after the case has moved to its own
	 * ping and the arm measures contention instead of the path. */
#ifndef CONFIG_RTL8XXXU_C5_PEER_WINDOW_MS
#define CONFIG_RTL8XXXU_C5_PEER_WINDOW_MS 6000
#endif
	CASE_ANCHOR("rtl8xxxu_c5: peer window ms=%d\n",
		    CONFIG_RTL8XXXU_C5_PEER_WINDOW_MS);
	osDelay(CONFIG_RTL8XXXU_C5_PEER_WINDOW_MS);

	(void)memset(&th0, 0, sizeof(th0));
	lkpi_tx_health_get(&th0);
	rc = lkpi_net_ping(C5_AP_IP, C5_PING_COUNT, C5_PING_SMALL,
			   C5_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtl8xxxu_c5: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    C5_PING_COUNT, ping_rx, C5_PING_SMALL, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != C5_PING_COUNT) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL ping small rc=%d rx=%u\n", rc,
			    ping_rx);
		/* The failure's ledger: the same counters the PASS path prints,
		 * so a lossy link says whether the frames left the driver
		 * (tx_fail/refused/stage_overflow) before the air is blamed. */
		lkpi_net_diag(&diag);
		CASE_ANCHOR("rtl8xxxu_c5: diag rx_frames=%lu tx_frames=%lu "
			    "rx_bytes=%lu tx_bytes=%lu tx_fail=%lu\n",
			    diag.rx_frames, diag.tx_frames, diag.rx_bytes,
			    diag.tx_bytes, diag.tx_fail);
		CASE_ANCHOR("rtl8xxxu_c5: path rx_alloc_fail=%lu rx_post_fail=%lu "
			    "rx_deliver_us=%lu rx_deliver_max_us=%lu "
			    "tx_ndo_us=%lu tx_ndo_max_us=%lu\n",
			    diag.rx_alloc_fail, diag.rx_post_fail,
			    diag.rx_deliver_ns / 1000u,
			    diag.rx_deliver_max_ns / 1000u,
			    diag.tx_ndo_ns / 1000u,
			    diag.tx_ndo_max_ns / 1000u);
		c5_tx_feedback(&th0);
		c5_usb_ledger();
		return 11;
	}

	rc = lkpi_net_ping(C5_AP_IP, C5_PING_COUNT, C5_PING_LARGE,
			   C5_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtl8xxxu_c5: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    C5_PING_COUNT, ping_rx, C5_PING_LARGE, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != C5_PING_COUNT) {
		CASE_ANCHOR("rtl8xxxu_c5: FAIL ping large rc=%d rx=%u\n", rc,
			    ping_rx);
		return 12;
	}

	lkpi_net_diag(&diag);
	CASE_ANCHOR("rtl8xxxu_c5: diag rx_frames=%lu tx_frames=%lu rx_bytes=%lu tx_bytes=%lu tx_fail=%lu\n",
		    diag.rx_frames, diag.tx_frames, diag.rx_bytes, diag.tx_bytes,
		    diag.tx_fail);
	c5_tx_feedback(&th0);
#if CONFIG_RTL8XXXU_TX_CENSUS
	/* After both counted rounds: the same registers the F429 arm prints at
	 * its "end" point, so the two transcripts line up. */
	c5_chip_state(dev, "post-ping");
#endif
	CASE_ANCHOR("rtl8xxxu_c5: PASS C6\n");
	CASE_ANCHOR("rtl8xxxu_c5: PASS\n");
	return 0;
}
