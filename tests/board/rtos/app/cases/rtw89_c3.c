/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw89_c3.c
 * @brief  Case rtw89_c3: the RTL8851BU (0bda:b851) enumerates on a USB host
 *         controller, the vendored rtw89 driver probes it over the linuxkpi USB
 *         engine, downloads its firmware and registers its wiphy with the real
 *         mac80211; opening the interface reaches the driver's start.
 *
 * C3 is the USB counterpart of iwl7260_c3: the same stack that C2 registered now
 * gets its wiphy from a driver's own probe path, but the bus is CherryUSB and
 * the transfers are urbs.  The order is the previous line's board-proven one,
 * with the stack in place of the net80211 bridge:
 *
 *   1. the kernel-API services the stack and the driver both use;
 *   2. the CherryUSB class hook (the enumeration-to-usb_driver path);
 *   3. the stack's and driver's `.lkpi_init` entries (cfg80211 first, then
 *      mac80211, then the driver: the link order decides it);
 *   4. the URB engine: `.lkpi_module` walk (this is what calls
 *      usb_register(&rtw_8851bu_driver)) and the drain thread;
 *   5. the asynchronous transfer model.  The engine's default is the blocking
 *      lane the storage bring-up used; rtw89 resubmits its receive urbs from
 *      their completion callbacks, so the blocking lane transmits and
 *      receives nothing;
 *   6. the firmware blob (compiled in; the target has no filesystem);
 *   7. the board's USB domain and one `usbh_initialize` per controller in the
 *      profile's family list, then the pre-plugged-device kick.
 *
 * What a PASS means: the driver's `Firmware version` line was printed by its
 * own probe (the H2C/C2H handshake completed), `wlan0` exists (mac80211's
 * register), and `dev_open()` returned 0 (mac80211's drv_start -> rtw89 start).
 * No frame has moved; C4/C5/C6 are separate gates.
 *
 * Anchors:
 *   rtw89_c3: core ready
 *   rtw89_c3: usb backend ready
 *   rtw89_c3: fw registered=<0|err> name=rtw89/rtw8851b_fw-1.bin
 *   rtw89_c3: initcalls run
 *   rtw89_c3: usb drivers=<n>
 *   rtw89_c3: usb buses=1,0
 *   rtw89_c3: usb bus=<n> hcd=<ehci|xhci> base=0x... started
 *   rtw89_c3: firmware <driver line>
 *   rtw89_c3: wlan0 present=<0|1> after <ms> ms
 *   rtw89_c3: start rc=<n>
 *   rtw89_c3: USB ledger on failure (submits/completes/refused/stage_overflow/
 *             slots_exhausted) - "the submit was refused" and "it left and
 *             nothing came back" read the same in a timeout, not here
 *   rtw89_c3: PASS
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

/* CherryUSB first.  Its headers include <stdlib.h>, and the Linux face's
 * compiler-attributes header defines __alloc_size__ - a name newlib's
 * <sys/cdefs.h> expands into an attribute.  Parsing stdlib.h after the face
 * turns that into a syntax error at the stdlib declarations, so the stack's
 * headers are read before the face is. */
#include "usbh_core.h"
#include "usb_glue.h"
#include "usb_domain.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linuxkpi_usb.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#define C3_WAIT_MS		60000U
#define C3_POLL_MS		100U

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);
void usb_osal_timer_service_start(void);
int lkpi_fw_rtw8851b_fw_register(void);

/* The profile's controller family: both EHCI roots (USB2 panel ports) or both
 * xHCI roots (the USB3 sockets).  The 8851BU is a USB2 part; the EHCI list is
 * the default and the xHCI profile is the fallback arm, never a runtime guess. */
#if CONFIG_RTW89_USB_BUS == 0
static const unsigned int c3_buses[] = { 3U, 2U };
#define C3_BUS_LIST_TXT	"3,2"
#else
static const unsigned int c3_buses[] = { 1U, 0U };
#define C3_BUS_LIST_TXT	"1,0"
#endif

/*
 * Console discipline.  The driver's HCI lines ("read swsi busy", C2H timeout)
 * can print dozens of times per boot; at 115200 baud each line costs about a
 * millisecond and the ring that carries the judged anchors is small.  They are
 * counted and summarised, while the firmware's own version line is promoted to
 * an anchor (it is the C3 evidence).
 */
static unsigned int c3_swsi_noise;
static unsigned int c3_c2h_noise;
static char c3_fw_line[160];

static void
c3_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	if (strstr(line, "Firmware version") != NULL) {
		strncpy(c3_fw_line, line, sizeof(c3_fw_line) - 1U);
		c3_fw_line[sizeof(c3_fw_line) - 1U] = '\0';
		CASE_ANCHOR("rtw89_c3: firmware %s\n", line);
		return;
	}
	if (strstr(line, "loaded firmware") != NULL) {
		CASE_ANCHOR("rtw89_c3: fw image %s\n", line);
		return;
	}
	if (strstr(line, "read swsi busy") != NULL) {
		c3_swsi_noise++;
		if (c3_swsi_noise <= 4U || (c3_swsi_noise % 200U) == 0U)
			CASE_INFO("rtw89_c3: swsi-busy %u lines\n", c3_swsi_noise);
		return;
	}
	if (strstr(line, "c2h") != NULL && strstr(line, "timeout") != NULL) {
		c3_c2h_noise++;
		if (c3_c2h_noise <= 4U || (c3_c2h_noise % 200U) == 0U)
			CASE_INFO("rtw89_c3: c2h-timeout %u lines\n", c3_c2h_noise);
		return;
	}
	if (strstr(line, "rtw89") != NULL || strstr(line, "usbkpi") != NULL ||
	    strstr(line, "usbh") != NULL)
		CASE_INFO("rtw89_c3: %s\n", line);
}

static void
c3_usb_ledger(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtw89_c3: usb submits=%u completes=%u refused=%u "
		    "stage_overflow=%u slots_exhausted=%u stage_size=%u slots=%u "
		    "rx=%u rx_err=%u\n",
		    st.submits, st.completed, st.refused, st.stage_overflow,
		    st.slots_exhausted, st.stage_size, st.stage_slots,
		    st.rx_completes, st.rx_errors);
}

/* The shared bring-up: everything before the `wlan0` wait.  Returns 0 when at
 * least one controller started; the caller reports what failed. */
static int
c3_usb_up(void)
{
	unsigned int i;
	int started = 0;

	lkpi_usb_cherryusb_init();
	CASE_ANCHOR("rtw89_c3: usb backend ready\n");

	lkpi_module_init_run();
	CASE_ANCHOR("rtw89_c3: initcalls run\n");

	lkpi_usb_init();
	CASE_ANCHOR("rtw89_c3: usb drivers=%u\n",
		    (unsigned)lkpi_usb_driver_count_get());

	lkpi_usb_force_sync_set(false);
	if (lkpi_fw_rtw8851b_fw_register() != 0) {
		CASE_ANCHOR("rtw89_c3: FAIL firmware blob registration\n");
		return -1;
	}
	CASE_ANCHOR("rtw89_c3: fw registered=0 name=rtw89/rtw8851b_fw-1.bin\n");

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtw89_c3: usb buses=%s\n", C3_BUS_LIST_TXT);
	for (i = 0U; i < sizeof(c3_buses) / sizeof(c3_buses[0]); i++) {
		unsigned int bus = c3_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtw89_c3: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtw89_c3: bus%u usbh_initialize failed\n", bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtw89_c3: usb bus=%u hcd=%s base=0x%lx started\n", bus,
			    usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
			    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	return started;
}

int
case_rtw89_c3_run(void)
{
	struct net_device *dev = NULL;
	unsigned int waited = 0U;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("rtw89_c3: core ready\n");
	lkpi_log_sink_register(c3_sink);

	rc = c3_usb_up();
	if (rc <= 0) {
		CASE_ANCHOR("rtw89_c3: FAIL no controller started\n");
		c3_usb_ledger();
		return 1;
	}

	/* The default interface appears when the driver's probe reaches
	 * ieee80211_register_hw(); the probe runs from CherryUSB's hub thread
	 * once the interface is claimed. */
	while (waited < C3_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(C3_POLL_MS);
		waited += C3_POLL_MS;
	}
	CASE_ANCHOR("rtw89_c3: wlan0 present=%d after %u ms\n",
		    dev != NULL, (unsigned)waited);
	if (dev == NULL) {
		CASE_ANCHOR("rtw89_c3: FAIL attach timeout (no dongle claimed, or "
			    "probe failed - see the lines above)\n");
		c3_usb_ledger();
		return 2;
	}
	CASE_ANCHOR("rtw89_c3: mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		    dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
	if (c3_fw_line[0] == '\0') {
		CASE_ANCHOR("rtw89_c3: FAIL no firmware version line (probe did "
			    "not reach the firmware report)\n");
		c3_usb_ledger();
		return 3;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("rtw89_c3: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtw89_c3: FAIL start rc=%d\n", rc);
		c3_usb_ledger();
		return 4;
	}
	c3_usb_ledger();
	if (c3_swsi_noise != 0U || c3_c2h_noise != 0U)
		CASE_INFO("rtw89_c3: noise swsi=%u c2h=%u\n", c3_swsi_noise,
			  c3_c2h_noise);

	CASE_ANCHOR("rtw89_c3: PASS\n");
	return 0;
}
