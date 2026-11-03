/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw88_usb.c
 * @brief  Case rtw88_usb: the RTL8821CU (0bda:c820) enumerates on a USB host
 *         controller, the vendored rtw88 driver probes it over the linuxkpi
 *         USB engine, downloads its firmware and registers its hw; the bridge
 *         then runs the driver's start chain.
 *
 * This is the B gate for the USB arm of the rtw88 line.  The chain is the one
 * the RTL8851BU arm proved, in the order that order matters:
 *
 *   1. the linuxkpi core services (the system workqueue every deferred driver
 *      path rides on; with no queue the work is silently dropped);
 *   2. the linuxkpi-side bridge (the driver's probe registers an hw, and the
 *      seam has to exist before that call lands);
 *   3. the CherryUSB backend, then the URB engine's registry walk and drain
 *      thread;
 *   4. the asynchronous transfer model - `lkpi_usb_force_sync_set(false)`.
 *      rtw88 resubmits its receive urbs from their completion callbacks, so
 *      with the blocking lane the sweep transmits and receives nothing;
 *   5. the firmware blob (rtw88/rtw8821c_fw.bin, `lkpi_fw_rtw8821c_fw_register`);
 *   6. the board USB domain and the controller start (one `usbh_initialize`
 *      per controller in the profile's list; the attach wait is shared);
 *   7. the driver's own start (`wlan_bridge_up()`), which is what submits the
 *      receive urbs - without it the chip only transmits.
 *
 * Anchors:
 *   rtw88: usb buses=3,2
 *   rtw88: usb bus=3 hcd=xhci base=0x... started
 *   rtw88: claim 0bda:c820               (the class hook's line, echoed)
 *   rtw88: fw Firmware version ...       (the driver's own line, echoed)
 *   rtw88: attach ok after <ms> ms mac=...
 *   rtw88: start ok attached=1 rx=<n>
 *   rtw88: usb submits=... completes=... refused=... stage_overflow=...
 *           slots_exhausted=...
 *   rtw88: PASS
 *
 * No attach on any listed controller is `rtw88: FAIL <reason>` with the USB
 * ledger beside it: refused submits, stage overflow and slot exhaustion are
 * the adapter's own account of why a transfer never left.  An attach with a
 * hardware address other than the profile's expected one is refused too: the
 * bench carries several Realtek dongles at once (8188EU, 8723BU, 8851BU), and
 * "attached" alone does not say which chip answered.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"
#include "linuxkpi_usb.h"
#include "wlan_bridge.h"
#include "lkpi_usb_cherryusb.h"
#include "skbuff/skbuff_pool.h"
#include "usbh_core.h"
#include "usb_glue.h"
#include "usb_domain.h"

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);
void lkpi_usb_init(void);
void lkpi_usb_force_sync_set(bool on);
int lkpi_fw_rtw8821c_fw_register(void);

#define RTW88_USB_WAIT_MS	60000U
#define RTW88_USB_POLL_MS	100U

/* The profile's controller list: both xHCI roots for the USB3 ports, both
 * EHCI roots for the panel.  `CONFIG_RTW88_USB_BUS` chooses the family; the
 * case prints the list and then the bus that actually attached. */
#if CONFIG_RTW88_USB_BUS == 1
static const unsigned int rtw88_buses[] = { 1U, 0U };
#define RTW88_BUS_LIST_TXT	"1,0"
#else
static const unsigned int rtw88_buses[] = { 3U, 2U };
#define RTW88_BUS_LIST_TXT	"3,2"
#endif

/* The identity gate: the bench's 8821CU answers 54:c9:e0:00:29:66.  The
 * profile names the address (CONFIG_RTW88_EXPECT_MAC, a readable string); the
 * generated header renders it as the six bytes below.  The case refuses an
 * attach that answers another address - the bench carries several Realtek
 * dongles on the same roots. */
static const uint8_t rtw88_expect_mac[6] = RTW88_EXPECT_MAC;

/*
 * Console discipline.  A probe failure can print a line per urbm and the ring
 * that carries the judged anchors is small; the claim, the firmware revision
 * and the adapter's own account are the lines this case promotes.
 */
static unsigned int rtw88_claim_seen;
static unsigned int rtw88_noise;
static char rtw88_fw_line[160];

static void
rtw88_usb_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (strstr(line, "0bda:c820") != NULL) {
		rtw88_claim_seen++;
		CASE_ANCHOR("rtw88: claim %s\n", line);
		return;
	}
	if (strstr(line, "Firmware version") != NULL) {
		strncpy(rtw88_fw_line, line, sizeof(rtw88_fw_line) - 1U);
		rtw88_fw_line[sizeof(rtw88_fw_line) - 1U] = '\0';
		CASE_ANCHOR("rtw88: fw %s\n", line);
		return;
	}
	if (strstr(line, "loaded firmware") != NULL) {
		CASE_ANCHOR("rtw88: fw image %s\n", line);
		return;
	}
	/* A probe failure is the judged event, not noise: the driver explains in
	 * an rtw_err line why it gave up, and boot1 taught us the noise window
	 * had already closed by then.  Promote every error-shaped line. */
	if (strstr(line, "fail") != NULL || strstr(line, "Fail") != NULL ||
	    strstr(line, "error") != NULL || strstr(line, "Error") != NULL ||
	    strstr(line, "timeout") != NULL || strstr(line, "Timeout") != NULL) {
		CASE_ANCHOR("rtw88: err %s\n", line);
		return;
	}
	rtw88_noise++;
	if (rtw88_noise <= 40U || (rtw88_noise % 100U) == 0U)
		CASE_INFO("lkpi: %s\n", line);
}

static int
rtw88_usb_attached(void)
{
	struct wlan_bridge_counters c;

	wlan_bridge_stats(&c);
	return c.attached != 0;
}

static void
rtw88_usb_stats_anchor(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtw88: usb submits=%u completes=%u refused=%u "
	    "stage_overflow=%u slots_exhausted=%u stage_size=%u slots=%u "
	    "rx=%u rx_err=%u rx_last=%u\n",
	    st.submits, st.completed, st.refused, st.stage_overflow,
	    st.slots_exhausted, st.stage_size, st.stage_slots,
	    st.rx_completes, st.rx_errors, st.rx_last_len);
}

int
case_rtw88_usb_run(void)
{
	struct wlan_bridge_counters counters;
	uint8_t mac[6];
	unsigned int waited = 0;
	int started = 0;

	lkpi_log_sink_register(rtw88_usb_sink);
	lkpi_core_init();
	(void)wlan_seam_adapter_init();
	lkpi_usb_cherryusb_init();
	lkpi_usb_init();
	lkpi_usb_force_sync_set(false);
	if (lkpi_fw_rtw8821c_fw_register() != 0) {
		CASE_ANCHOR("rtw88: FAIL firmware blob registration\n");
		return 1;
	}

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtw88: usb buses=%s\n", RTW88_BUS_LIST_TXT);
	for (unsigned int i = 0;
	     i < sizeof(rtw88_buses) / sizeof(rtw88_buses[0]); i++) {
		unsigned int bus = rtw88_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtw88: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtw88: bus%u usbh_initialize failed\n", bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtw88: usb bus=%u hcd=%s base=0x%lx started\n", bus,
		    usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
		    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	if (started == 0) {
		CASE_ANCHOR("rtw88: FAIL no controller started\n");
		return 1;
	}

	while (waited < RTW88_USB_WAIT_MS && !rtw88_usb_attached()) {
		osDelay(RTW88_USB_POLL_MS);
		waited += RTW88_USB_POLL_MS;
	}
	if (!rtw88_usb_attached()) {
		CASE_ANCHOR("rtw88: FAIL attach timeout after %u ms "
		    "(no dongle claimed, or probe failed - see the lines above)\n",
		    (unsigned)waited);
		rtw88_usb_stats_anchor();
		return 1;
	}

	if (wlan_bridge_hwaddr(mac) != 0) {
		CASE_ANCHOR("rtw88: FAIL hardware address unavailable\n");
		return 1;
	}
	if (memcmp(mac, rtw88_expect_mac, sizeof(mac)) != 0) {
		CASE_ANCHOR("rtw88: FAIL identity mac=%02x:%02x:%02x:%02x:%02x:%02x "
		    "expected=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		    rtw88_expect_mac[0], rtw88_expect_mac[1],
		    rtw88_expect_mac[2], rtw88_expect_mac[3],
		    rtw88_expect_mac[4], rtw88_expect_mac[5]);
		rtw88_usb_stats_anchor();
		return 1;
	}
	CASE_ANCHOR("rtw88: attach ok after %u ms mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
	    (unsigned)waited, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	if (rtw88_fw_line[0] == '\0') {
		CASE_ANCHOR("rtw88: FAIL no firmware version line (probe did "
		    "not reach the firmware report)\n");
		rtw88_usb_stats_anchor();
		return 1;
	}

	if (wlan_bridge_up() != 0) {
		CASE_ANCHOR("rtw88: FAIL start chain refused\n");
		return 1;
	}
	wlan_bridge_stats(&counters);
	CASE_ANCHOR("rtw88: start ok attached=%u rx=%u\n",
	    (unsigned)counters.attached, counters.rx_frames);
	if (counters.attached == 0U) {
		CASE_ANCHOR("rtw88: FAIL start returned but not attached\n");
		return 1;
	}

	{
		struct lkpi_skb_pool_stats st;

		lkpi_skb_pool_stats(&st);
		CASE_INFO("rtw88: pool skb=%u/%u data=%u/%u heap_live=%u "
		    "ovf=%u/%u fail=%u\n", st.skb_used, st.skb_high,
		    st.data_used, st.data_high, st.dyalloc_live,
		    st.slot_overflow, st.data_overflow, st.alloc_fail);
	}
	rtw88_usb_stats_anchor();
	if (rtw88_claim_seen == 0U)
		CASE_INFO("rtw88: claim line not seen\n");

	CASE_ANCHOR("rtw88: PASS\n");
	return 0;
}
