/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw89_c4.c
 * @brief  Case rtw89_c4: the RTL8851BU scans and finds the bench AP.
 *
 * C4 sits on C3's attach (same USB bring-up, same probe and firmware, same
 * `dev_open`).  On top of it this case sends the real NL80211_CMD_TRIGGER_SCAN
 * attribute stream through nl80211's own handler, waits for mac80211's BSS table
 * to carry the bench AP, and prints the BSSID/channel/signal.
 *
 * The bench seam is only the netlink socket: the attributes are built with the
 * real nla_put* and parsed by nlmsg_parse; nl80211's pre_doit/doit/post_doit run
 * for real, and the scan itself is mac80211 -> rtw89 -> the USB bulk endpoints.
 * What this is not: association (C5) or the data plane (C6).
 *
 * Anchors:
 *   rtw89_c4: core ready
 *   rtw89_c4: usb backend ready
 *   rtw89_c4: initcalls run
 *   rtw89_c4: usb drivers=<n>
 *   rtw89_c4: usb buses=1,0
 *   rtw89_c4: usb bus=<n> hcd=<ehci|xhci> base=0x... started
 *   rtw89_c4: firmware <driver line>
 *   rtw89_c4: wlan0 present=1 ifindex=<n>
 *   rtw89_c4: start rc=<n>
 *   rtw89_c4: scan ssid=<ssid> freq=<mhz> rc=<n>
 *   rtw89_c4: scan wildcard rc=<n>
 *   rtw89_c4: bss ssid=<ssid> bssid=<mac> freq=<mhz> signal=<dbm>
 *   rtw89_c4: PASS
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

/* CherryUSB first (see rtw89_c3.c): its <stdlib.h> must be parsed before the
 * Linux face's compiler-attributes header is. */
#include "usbh_core.h"
#include "usb_glue.h"
#include "usb_domain.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linuxkpi_usb.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#define C4_WAIT_MS		60000U
#define C4_POLL_MS		100U
#define C4_SCAN_WAIT_MS		10000U

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);
void usb_osal_timer_service_start(void);
int lkpi_fw_rtw8851b_fw_register(void);

#if CONFIG_RTW89_USB_BUS == 0
static const unsigned int c4_buses[] = { 3U, 2U };
#define C4_BUS_LIST_TXT	"3,2"
#else
static const unsigned int c4_buses[] = { 1U, 0U };
#define C4_BUS_LIST_TXT	"1,0"
#endif

static unsigned int c4_swsi_noise;
static unsigned int c4_c2h_noise;
static char c4_fw_line[160];

static void
c4_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	if (strstr(line, "Firmware version") != NULL) {
		strncpy(c4_fw_line, line, sizeof(c4_fw_line) - 1U);
		c4_fw_line[sizeof(c4_fw_line) - 1U] = '\0';
		CASE_ANCHOR("rtw89_c4: firmware %s\n", line);
		return;
	}
	if (strstr(line, "loaded firmware") != NULL) {
		CASE_ANCHOR("rtw89_c4: fw image %s\n", line);
		return;
	}
	if (strstr(line, "read swsi busy") != NULL) {
		c4_swsi_noise++;
		if (c4_swsi_noise <= 4U || (c4_swsi_noise % 200U) == 0U)
			CASE_INFO("rtw89_c4: swsi-busy %u lines\n", c4_swsi_noise);
		return;
	}
	if (strstr(line, "c2h") != NULL && strstr(line, "timeout") != NULL) {
		c4_c2h_noise++;
		if (c4_c2h_noise <= 4U || (c4_c2h_noise % 200U) == 0U)
			CASE_INFO("rtw89_c4: c2h-timeout %u lines\n", c4_c2h_noise);
		return;
	}
	if (strstr(line, "scan") != NULL || strstr(line, "Scan") != NULL)
		CASE_INFO("rtw89_c4: drv %s\n", line);
}

static void
c4_usb_ledger(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtw89_c4: usb submits=%u completes=%u refused=%u "
		    "stage_overflow=%u slots_exhausted=%u rx=%u rx_err=%u\n",
		    st.submits, st.completed, st.refused, st.stage_overflow,
		    st.slots_exhausted, st.rx_completes, st.rx_errors);
}

static int
c4_usb_up(void)
{
	unsigned int i;
	int started = 0;

	lkpi_usb_cherryusb_init();
	CASE_ANCHOR("rtw89_c4: usb backend ready\n");

	lkpi_module_init_run();
	CASE_ANCHOR("rtw89_c4: initcalls run\n");

	lkpi_usb_init();
	CASE_ANCHOR("rtw89_c4: usb drivers=%u\n",
		    (unsigned)lkpi_usb_driver_count_get());

	lkpi_usb_force_sync_set(false);
	if (lkpi_fw_rtw8851b_fw_register() != 0) {
		CASE_ANCHOR("rtw89_c4: FAIL firmware blob registration\n");
		return -1;
	}
	CASE_ANCHOR("rtw89_c4: fw registered=0 name=rtw89/rtw8851b_fw-1.bin\n");

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtw89_c4: usb buses=%s\n", C4_BUS_LIST_TXT);
	for (i = 0U; i < sizeof(c4_buses) / sizeof(c4_buses[0]); i++) {
		unsigned int bus = c4_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtw89_c4: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtw89_c4: bus%u usbh_initialize failed\n", bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtw89_c4: usb bus=%u hcd=%s base=0x%lx started\n", bus,
			    usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
			    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	return started;
}

int
case_rtw89_c4_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	unsigned char bssid[6];
	unsigned int waited = 0U;
	int freq = 0;
	int sig = 0;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("rtw89_c4: core ready\n");
	lkpi_log_sink_register(c4_sink);

	rc = c4_usb_up();
	if (rc <= 0) {
		CASE_ANCHOR("rtw89_c4: FAIL no controller started\n");
		c4_usb_ledger();
		return 1;
	}

	while (waited < C4_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(C4_POLL_MS);
		waited += C4_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("rtw89_c4: FAIL no wlan0 after %u ms\n",
			    (unsigned)waited);
		c4_usb_ledger();
		return 2;
	}
	CASE_ANCHOR("rtw89_c4: wlan0 present=1 ifindex=%d\n", dev->ifindex);

	rc = dev_open(dev);
	CASE_ANCHOR("rtw89_c4: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtw89_c4: FAIL start rc=%d\n", rc);
		return 3;
	}

	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("rtw89_c4: FAIL no wireless_dev\n");
		return 4;
	}

	/*
	 * Directed scan: the bench AP is on ch5 (2432 MHz) and its SSID is fixed
	 * by the lab.  A wildcard retry follows only if the directed pass produces
	 * no BSS within the window.
	 */
	rc = lkpi_nl80211_trigger_scan(dev, "labwifi-ar-ht20s", 2432);
	CASE_ANCHOR("rtw89_c4: scan ssid=labwifi-ar-ht20s freq=2432 rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtw89_c4: FAIL scan rc=%d\n", rc);
		return 5;
	}

	for (waited = 0U; waited < C4_SCAN_WAIT_MS; waited += C4_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, "labwifi-ar-ht20s",
					  bssid, &freq, &sig) == 0)
			goto found;
		osDelay(C4_POLL_MS);
	}

	/* The directed pass saw nothing; try every supported channel once. */
	rc = lkpi_nl80211_trigger_scan(dev, NULL, 0);
	CASE_ANCHOR("rtw89_c4: scan wildcard rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtw89_c4: FAIL wildcard scan rc=%d\n", rc);
		return 6;
	}
	for (waited = 0U; waited < C4_SCAN_WAIT_MS; waited += C4_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, "labwifi-ar-ht20s",
					  bssid, &freq, &sig) == 0)
			goto found;
		osDelay(C4_POLL_MS);
	}

	CASE_ANCHOR("rtw89_c4: FAIL bss timeout (scan started, no BSS)\n");
	return 7;

found:
	CASE_ANCHOR("rtw89_c4: bss ssid=labwifi-ar-ht20s bssid=%02x:%02x:%02x:%02x:%02x:%02x freq=%d signal=%d\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    freq, sig / 100);
	if (c4_swsi_noise != 0U || c4_c2h_noise != 0U)
		CASE_INFO("rtw89_c4: noise swsi=%u c2h=%u\n", c4_swsi_noise,
			  c4_c2h_noise);
	CASE_ANCHOR("rtw89_c4: PASS\n");
	return 0;
}
