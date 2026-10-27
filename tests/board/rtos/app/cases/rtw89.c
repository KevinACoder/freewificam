/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw89.c
 * @brief  Case rtw89: the RTL8851BU (0bda:b851) enumerates on a USB host
 *         controller, the vendored rtw89 driver probes it over the linuxkpi
 *         USB engine, downloads its firmware and registers its hw; the bridge
 *         then runs the driver's start chain.
 *
 * This is the B gate.  The chain is the reference image's, in the order that
 * order matters:
 *
 *   1. the linuxkpi core services (the system workqueue every deferred driver
 *      path rides on; with no queue the work is silently dropped);
 *   2. the linuxkpi-side bridge (the driver's probe registers an hw, and the
 *      seam has to exist before that call lands);
 *   3. the CherryUSB backend, then the URB engine's registry walk and drain
 *      thread;
 *   4. the asynchronous transfer model - `lkpi_usb_force_sync_set(false)`.
 *      The engine's default is the blocking lane the storage bring-up used;
 *      rtw89 resubmits its receive urbs from their completion callbacks, so
 *      with the blocking lane the sweep transmits and receives nothing;
 *   5. the firmware blob;
 *   6. the board USB domain and the controller start (one `usbh_initialize`
 *      per controller in the profile's list; the attach wait is shared);
 *   7. the driver's own start (`wlan_bridge_up()`), which is what submits the
 *      receive urbs - without it the chip only transmits.
 *
 * Anchors:
 *   rtw89: usb buses=3,2
 *   rtw89: usb bus=3 hcd=xhci base=0x... started
 *   rtw89: usb claim 0bda:b851            (the class hook's line, echoed)
 *   rtw89: fw Firmware version ...        (the driver's own line, echoed)
 *   rtw89: attach ok after <ms> ms mac=...
 *   rtw89: start ok attached=1 rx=<n>
 *   rtw89: usb submits=... completes=... refused=... stage_overflow=...
 *           slots_exhausted=...
 *   rtw89: PASS
 *
 * No attach on any listed controller is `rtw89: FAIL <reason>` with the USB
 * ledger beside it: refused submits, stage overflow and slot exhaustion are
 * the adapter's own account of why a transfer never left.
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
int lkpi_fw_rtw8851b_fw_register(void);

#define RTW89_WAIT_MS		60000U
#define RTW89_POLL_MS		100U

/* The profile's controller list: both xHCI roots for the USB3 ports, both
 * EHCI roots for the panel.  `CONFIG_RTW89_USB_BUS` chooses the family; the
 * case prints the list and then the bus that actually attached. */
#if RTW89_USB_BUS == 1
static const unsigned int rtw89_buses[] = { 1U, 0U };
#define RTW89_BUS_LIST_TXT	"1,0"
#else
static const unsigned int rtw89_buses[] = { 3U, 2U };
#define RTW89_BUS_LIST_TXT	"3,2"
#endif

/*
 * Console discipline.  The driver's `read swsi busy` and C2H timeout lines can
 * print dozens of times per boot; at 115200 baud each line is about a
 * millisecond and the ring that carries the judged anchors is small.  They
 * are counted and summarised, while the firmware's own version line is
 * promoted to an anchor (it is the C2H evidence V2 asks for).
 */
static unsigned int rtw89_swsi_noise;
static unsigned int rtw89_c2h_noise;
static char rtw89_fw_line[160];

static void
rtw89_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (strstr(line, "Firmware version") != NULL) {
		strncpy(rtw89_fw_line, line, sizeof(rtw89_fw_line) - 1U);
		rtw89_fw_line[sizeof(rtw89_fw_line) - 1U] = '\0';
		CASE_ANCHOR("rtw89: fw %s\n", line);
		return;
	}
	if (strstr(line, "loaded firmware") != NULL) {
		CASE_ANCHOR("rtw89: fw image %s\n", line);
		return;
	}
	if (strstr(line, "read swsi busy") != NULL) {
		rtw89_swsi_noise++;
		if (rtw89_swsi_noise <= 4U || (rtw89_swsi_noise % 200U) == 0U)
			CASE_INFO("lkpi: swsi-busy %u lines\n", rtw89_swsi_noise);
		return;
	}
	if (strstr(line, "c2h") != NULL && strstr(line, "timeout") != NULL) {
		rtw89_c2h_noise++;
		if (rtw89_c2h_noise <= 4U || (rtw89_c2h_noise % 200U) == 0U)
			CASE_INFO("lkpi: c2h-timeout %u lines\n", rtw89_c2h_noise);
		return;
	}
	CASE_INFO("lkpi: %s\n", line);
}

static int
rtw89_attached(void)
{
	struct wlan_bridge_counters c;

	wlan_bridge_stats(&c);
	return c.attached != 0;
}

static void
rtw89_usb_stats_anchor(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtw89: usb submits=%u completes=%u refused=%u "
	    "stage_overflow=%u slots_exhausted=%u stage_size=%u slots=%u "
	    "rx=%u rx_err=%u rx_last=%u\n",
	    st.submits, st.completed, st.refused, st.stage_overflow,
	    st.slots_exhausted, st.stage_size, st.stage_slots,
	    st.rx_completes, st.rx_errors, st.rx_last_len);
}

int
case_rtw89_run(void)
{
	struct wlan_bridge_counters counters;
	uint8_t mac[6];
	unsigned int waited = 0;
	int started = 0;

	lkpi_log_sink_register(rtw89_sink);
	lkpi_core_init();
	(void)wlan_seam_adapter_init();
	lkpi_usb_cherryusb_init();
	lkpi_usb_init();
	lkpi_usb_force_sync_set(false);
	if (lkpi_fw_rtw8851b_fw_register() != 0) {
		CASE_ANCHOR("rtw89: FAIL firmware blob registration\n");
		return 1;
	}

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtw89: usb buses=%s\n", RTW89_BUS_LIST_TXT);
	for (unsigned int i = 0; i < sizeof(rtw89_buses) / sizeof(rtw89_buses[0]);
	     i++) {
		unsigned int bus = rtw89_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtw89: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtw89: bus%u usbh_initialize failed\n", bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtw89: usb bus=%u hcd=%s base=0x%lx started\n", bus,
		    usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
		    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	if (started == 0) {
		CASE_ANCHOR("rtw89: FAIL no controller started\n");
		return 1;
	}

	while (waited < RTW89_WAIT_MS && !rtw89_attached()) {
		osDelay(RTW89_POLL_MS);
		waited += RTW89_POLL_MS;
	}
	if (!rtw89_attached()) {
		CASE_ANCHOR("rtw89: FAIL attach timeout after %u ms "
		    "(no dongle claimed, or probe failed - see the lines above)\n",
		    (unsigned)waited);
		rtw89_usb_stats_anchor();
		return 1;
	}

	if (wlan_bridge_hwaddr(mac) != 0) {
		CASE_ANCHOR("rtw89: FAIL hardware address unavailable\n");
		return 1;
	}
	CASE_ANCHOR("rtw89: attach ok after %u ms mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
	    (unsigned)waited, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	if (rtw89_fw_line[0] == '\0') {
		CASE_ANCHOR("rtw89: FAIL no firmware version line (probe did "
		    "not reach the firmware report)\n");
		rtw89_usb_stats_anchor();
		return 1;
	}

	if (wlan_bridge_up() != 0) {
		CASE_ANCHOR("rtw89: FAIL start chain refused\n");
		return 1;
	}
	wlan_bridge_stats(&counters);
	CASE_ANCHOR("rtw89: start ok attached=%u rx=%u\n",
	    (unsigned)counters.attached, counters.rx_frames);
	if (counters.attached == 0U) {
		CASE_ANCHOR("rtw89: FAIL start returned but not attached\n");
		return 1;
	}

	{
		struct lkpi_skb_pool_stats st;

		lkpi_skb_pool_stats(&st);
		CASE_INFO("rtw89: pool skb=%u/%u data=%u/%u heap_live=%u "
		    "ovf=%u/%u fail=%u\n", st.skb_used, st.skb_high,
		    st.data_used, st.data_high, st.dyalloc_live,
		    st.slot_overflow, st.data_overflow, st.alloc_fail);
	}
	rtw89_usb_stats_anchor();
	if (rtw89_swsi_noise != 0U || rtw89_c2h_noise != 0U)
		CASE_INFO("rtw89: noise swsi=%u c2h=%u\n", rtw89_swsi_noise,
		    rtw89_c2h_noise);

	CASE_ANCHOR("rtw89: PASS\n");
	return 0;
}
