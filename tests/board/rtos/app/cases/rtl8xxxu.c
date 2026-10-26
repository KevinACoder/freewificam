/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8xxxu.c
 * @brief  Case rtl8xxxu: the RTL8188EUS (0bda:8179) enumerates on a USB host
 *         controller, the vendored rtl8xxxu driver probes it over the linuxkpi
 *         USB engine, downloads its firmware and registers its hw; the bridge
 *         then runs the driver's start chain.
 *
 * This is the L3-08 bring-up gate.  The chain is the carrier image's, in the
 * order that matters:
 *
 *   1. the linuxkpi core services (the system workqueue every deferred driver
 *      path rides on; with no queue the work is silently dropped);
 *   2. the linuxkpi-side bridge (the driver's probe registers an hw, and the
 *      seam has to exist before that call lands);
 *   3. the CherryUSB backend, then the URB engine's registry walk and drain
 *      thread;
 *   4. the asynchronous transfer model - `lkpi_usb_force_sync_set(false)`.
 *      The engine's default is the blocking lane the storage bring-up used;
 *      rtl8xxxu resubmits its 32 receive urbs from their completion callbacks,
 *      so with the blocking lane the chip receives nothing;
 *   5. the firmware blob, registered under the name the driver asks for
 *      (`rtlwifi/rtl8188eufw.bin`);
 *   6. the board USB domain and the controller start (one `usbh_initialize`
 *      per controller in the profile's list; the attach wait is shared);
 *   7. the driver's own start (`wlan_bridge_up()`), which is what submits the
 *      receive urbs - without it the chip only transmits.
 *
 * Anchors:
 *   rtl8xxxu: usb buses=3,2
 *   rtl8xxxu: usb bus=3 hcd=xhci base=0x... started
 *   rtl8xxxu: claim 0bda:8179                (the class hook's line, echoed)
 *   rtl8xxxu: fw Firmware revision 28.0 (signature 0x88e1)
 *   rtl8xxxu: attach ok after <ms> ms mac=...
 *   rtl8xxxu: start ok attached=1 rx=<n>
 *   rtl8xxxu: usb submits=... completes=... refused=... stage_overflow=...
 *           slots_exhausted=... rx=... rx_err=... rx_last=...
 *   rtl8xxxu: PASS
 *
 * No attach on any listed controller is `rtl8xxxu: FAIL <reason>` with the USB
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

#if defined(RTL8XXXU_HB)
#include "uart.h"
/*
 * A heartbeat for the ThreadX hang: after the claim the runner sometimes never
 * prints again, and the question is whether the whole kernel/console is dead
 * or only this thread.  The heartbeat writes through the *polled* early UART,
 * so it bypasses the log lock and the OS locks entirely, and it reports the
 * bridge and workqueue counters from a higher-priority thread.  Diagnostic
 * only (RTL8XXXU_HB), never in a delivery profile.
 */
static volatile unsigned int rtl8xxxu_hb_stop;
static osThreadId_t rtl8xxxu_hb_runner;

static void
rtl8xxxu_hb_thread(void *arg)
{
	unsigned int n = 0;

	(void)arg;
	while (rtl8xxxu_hb_stop == 0u) {
		struct wlan_bridge_counters c;
		struct lkpi_wq_stats wq;
		char buf[96];
		int len;

		wlan_bridge_stats(&c);
		lkpi_wq_stats_get(&wq);
		len = snprintf(buf, sizeof(buf),
		    "\nHB %u attached=%u rx=%u wq=%u st=%d\n", n,
		    (unsigned)c.attached, c.rx_frames, (unsigned)wq.runs,
		    (int)osThreadGetState(rtl8xxxu_hb_runner));
		if (len > 0 && (size_t)len < sizeof(buf))
			uart_early_puts(buf);
		/*
		 * The second half of the discriminator: the same heartbeat
		 * through the *log* path.  If the raw line above appears and this
		 * one does not, the console/log lock is held by somebody; if this
		 * line appears too, the log is fine and the runner is blocked on
		 * something else.
		 */
		CASE_INFO("hb-log %u\n", n);
		n++;
		osDelay(1000u);
	}
	rtl8xxxu_hb_stop = 2u;
}
#endif

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);
void lkpi_usb_init(void);
void lkpi_usb_force_sync_set(bool on);
int lkpi_fw_rtl8188eufw_register(void);

#define RTL8XXXU_WAIT_MS	60000U
#define RTL8XXXU_POLL_MS	100U

/* The dongle this image is for (the profile's arm fact).  The bench carries a
 * second rtl8xxxu device (an RTL8723BU on the USB2 root); if the profile ever
 * starts that root, "attached" would be true for the wrong chip.  The address
 * is the identity check board-test-ops asks for. */
#ifndef RTL8XXXU_EXPECT_MAC
#define RTL8XXXU_EXPECT_MAC { 0x20, 0xf4, 0x1b, 0x52, 0x8f, 0x01 }
#endif
static const uint8_t rtl8xxxu_expect_mac[6] = RTL8XXXU_EXPECT_MAC;

/*
 * The profile's controller list.  The dongle is a USB2 device: on this bench it
 * has been seen on an xHCI root at 480 M (the lab arm) and on an OHCI root at
 * 12 M (the clean GT arm, which this image cannot drive - the RTOS USB domain
 * carries EHCI and xHCI only).  The list is therefore a bench fact, not a
 * driver fact: the discovery profile starts all four roots once, then the bus
 * that claimed the dongle is pinned here.
 */
#if RTL8XXXU_USB_BUS == 1
static const unsigned int rtl8xxxu_buses[] = { 1U, 0U };
#define RTL8XXXU_BUS_LIST_TXT	"1,0"
#elif RTL8XXXU_USB_BUS == 4
static const unsigned int rtl8xxxu_buses[] = { 3U, 2U, 1U, 0U };
#define RTL8XXXU_BUS_LIST_TXT	"3,2,1,0"
#else
static const unsigned int rtl8xxxu_buses[] = { 3U, 2U };
#define RTL8XXXU_BUS_LIST_TXT	"3,2"
#endif

/*
 * Console discipline.  The driver's mailbox path can print a busy line per
 * poll when its debug mask is on, and at 115200 baud every line is about a
 * millisecond of the ring that carries the judged anchors.  They are counted
 * and summarised; the firmware revision and the MAC are the lines this case
 * promotes, because they are the M2/M3 evidence.
 */
static unsigned int rtl8xxxu_busy_noise;
static unsigned int rtl8xxxu_h2c_noise;
static char rtl8xxxu_fw_line[160];
static unsigned int rtl8xxxu_claim_seen;

static void
rtl8xxxu_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (strstr(line, "0bda:8179") != NULL) {
		rtl8xxxu_claim_seen++;
		CASE_ANCHOR("rtl8xxxu: claim %s\n", line);
		return;
	}
	if (strstr(line, "Firmware revision") != NULL) {
		strncpy(rtl8xxxu_fw_line, line, sizeof(rtl8xxxu_fw_line) - 1U);
		rtl8xxxu_fw_line[sizeof(rtl8xxxu_fw_line) - 1U] = '\0';
		CASE_ANCHOR("rtl8xxxu: fw %s\n", line);
		return;
	}
	if (strstr(line, "Loading firmware") != NULL) {
		CASE_ANCHOR("rtl8xxxu: fw image %s\n", line);
		return;
	}
	if (strstr(line, "MAC:") != NULL) {
		/* The driver's %pM is not rendered by this port's printf (the MAC
		 * arrives as a pointer-shaped token), so the address is taken from
		 * wlan_bridge_hwaddr() and printed by the attach line; this line is
		 * kept as INFO for the record, not as an anchor. */
		CASE_INFO("lkpi: %%pM-line %s\n", line);
		return;
	}
	if (strstr(line, "Mailbox busy") != NULL) {
		rtl8xxxu_busy_noise++;
		if (rtl8xxxu_busy_noise <= 4U || (rtl8xxxu_busy_noise % 200U) == 0U)
			CASE_INFO("lkpi: mailbox-busy %u lines\n",
			    rtl8xxxu_busy_noise);
		return;
	}
	if (strstr(line, "H2C") != NULL) {
		rtl8xxxu_h2c_noise++;
		if (rtl8xxxu_h2c_noise <= 4U || (rtl8xxxu_h2c_noise % 200U) == 0U)
			CASE_INFO("lkpi: h2c %u lines\n", rtl8xxxu_h2c_noise);
		return;
	}
	CASE_INFO("lkpi: %s\n", line);
}

static int
rtl8xxxu_attached(void)
{
	struct wlan_bridge_counters c;

	wlan_bridge_stats(&c);
	return c.attached != 0;
}

static void
rtl8xxxu_usb_stats_anchor(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtl8xxxu: usb submits=%u completes=%u refused=%u "
	    "stage_overflow=%u slots_exhausted=%u stage_size=%u slots=%u "
	    "rx=%u rx_err=%u rx_last=%u\n",
	    st.submits, st.completed, st.refused, st.stage_overflow,
	    st.slots_exhausted, st.stage_size, st.stage_slots,
	    st.rx_completes, st.rx_errors, st.rx_last_len);
}

static int
rtl8xxxu_mac_is_zero(const uint8_t mac[6])
{
	uint8_t acc = 0;
	unsigned int i;

	for (i = 0; i < 6u; i++)
		acc |= mac[i];
	return acc == 0u;
}

int
case_rtl8xxxu_run(void)
{
	struct wlan_bridge_counters counters;
	uint8_t mac[6];
	unsigned int waited = 0;
	int started = 0;

	lkpi_log_sink_register(rtl8xxxu_sink);
	lkpi_core_init();
	(void)wlan_seam_adapter_init();
	lkpi_usb_cherryusb_init();
	lkpi_usb_init();
	lkpi_usb_force_sync_set(false);
	if (lkpi_fw_rtl8188eufw_register() != 0) {
		CASE_ANCHOR("rtl8xxxu: FAIL firmware blob registration\n");
		return 1;
	}

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtl8xxxu: usb buses=%s\n", RTL8XXXU_BUS_LIST_TXT);
	for (unsigned int i = 0;
	     i < sizeof(rtl8xxxu_buses) / sizeof(rtl8xxxu_buses[0]); i++) {
		unsigned int bus = rtl8xxxu_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtl8xxxu: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtl8xxxu: bus%u usbh_initialize failed\n", bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtl8xxxu: usb bus=%u hcd=%s base=0x%lx started\n",
		    bus, usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
		    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	if (started == 0) {
		CASE_ANCHOR("rtl8xxxu: FAIL no controller started\n");
		return 1;
	}

#if defined(RTL8XXXU_HB)
	{
		static const osThreadAttr_t hb_attr = {
			.name = "eu-hb",
			.priority = osPriorityHigh,
			.stack_size = 4096U,
		};

		rtl8xxxu_hb_stop = 0u;
		rtl8xxxu_hb_runner = osThreadGetId();
		if (osThreadNew(rtl8xxxu_hb_thread, NULL, &hb_attr) == NULL)
			uart_early_puts("\nHB thread creation failed\n");
	}
#endif
	while (waited < RTL8XXXU_WAIT_MS && !rtl8xxxu_attached()) {
		osDelay(RTL8XXXU_POLL_MS);
		waited += RTL8XXXU_POLL_MS;
	}
#if defined(RTL8XXXU_HB)
	rtl8xxxu_hb_stop = 1u;
	for (unsigned int hb_wait = 0; hb_wait < 300u; hb_wait++) {
		if (rtl8xxxu_hb_stop == 2u)
			break;
		osDelay(10u);
	}
#endif
	if (!rtl8xxxu_attached()) {
		CASE_ANCHOR("rtl8xxxu: FAIL attach timeout after %u ms "
		    "(no dongle claimed, or probe failed - see the lines above)\n",
		    (unsigned)waited);
		rtl8xxxu_usb_stats_anchor();
		return 1;
	}

	if (wlan_bridge_hwaddr(mac) != 0) {
		CASE_ANCHOR("rtl8xxxu: FAIL hardware address unavailable\n");
		return 1;
	}
	CASE_ANCHOR("rtl8xxxu: attach ok after %u ms mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
	    (unsigned)waited, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	if (rtl8xxxu_mac_is_zero(mac)) {
		CASE_ANCHOR("rtl8xxxu: FAIL hardware address is all zero\n");
		return 1;
	}
	if (memcmp(mac, rtl8xxxu_expect_mac, 6) != 0) {
		CASE_ANCHOR("rtl8xxxu: FAIL identity mac=%02x:%02x:%02x:%02x:%02x:%02x "
		    "!= expect=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		    rtl8xxxu_expect_mac[0], rtl8xxxu_expect_mac[1],
		    rtl8xxxu_expect_mac[2], rtl8xxxu_expect_mac[3],
		    rtl8xxxu_expect_mac[4], rtl8xxxu_expect_mac[5]);
		return 1;
	}
	CASE_ANCHOR("rtl8xxxu: identity mac matches the arm\n");
	if (rtl8xxxu_fw_line[0] == '\0') {
		CASE_ANCHOR("rtl8xxxu: FAIL no firmware revision line (probe did "
		    "not reach the firmware report)\n");
		rtl8xxxu_usb_stats_anchor();
		return 1;
	}

	if (wlan_bridge_up() != 0) {
		CASE_ANCHOR("rtl8xxxu: FAIL start chain refused\n");
		return 1;
	}
	wlan_bridge_stats(&counters);
	CASE_ANCHOR("rtl8xxxu: start ok attached=%u rx=%u\n",
	    (unsigned)counters.attached, counters.rx_frames);
	if (counters.attached == 0U) {
		CASE_ANCHOR("rtl8xxxu: FAIL start returned but not attached\n");
		return 1;
	}

	{
		struct lkpi_skb_pool_stats st;

		lkpi_skb_pool_stats(&st);
		CASE_INFO("rtl8xxxu: pool skb=%u/%u data=%u/%u heap_live=%u "
		    "ovf=%u/%u fail=%u\n", st.skb_used, st.skb_high,
		    st.data_used, st.data_high, st.dyalloc_live,
		    st.slot_overflow, st.data_overflow, st.alloc_fail);
	}
	rtl8xxxu_usb_stats_anchor();
	if (rtl8xxxu_busy_noise != 0U || rtl8xxxu_h2c_noise != 0U)
		CASE_INFO("rtl8xxxu: noise busy=%u h2c=%u\n",
		    rtl8xxxu_busy_noise, rtl8xxxu_h2c_noise);
	if (rtl8xxxu_claim_seen == 0U)
		CASE_INFO("rtl8xxxu: no class-hook claim line was echoed "
		    "(the driver still probed: attach succeeded)\n");

	CASE_ANCHOR("rtl8xxxu: PASS\n");
	return 0;
}