/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8xxxu_c3.c
 * @brief  Case rtl8xxxu_c3: the RTL8188EUS (0bda:8179) enumerates on a USB host
 *         controller - this round's bench has it behind the external hub on the
 *         bench's "xhci0" (the GT kernel's xhci-hcd.0.auto, the DWC3 at
 *         0xFCC00000, this board map's busid 3) - the vendored driver probes it
 *         over the linuxkpi USB engine, downloads rtlwifi/rtl8188eufw.bin and
 *         registers its wiphy with the real mac80211; opening the interface
 *         reaches the driver's start.
 *
 * C3 is the USB counterpart of iwl7260_c3: the same stack that C2 registered now
 * gets its wiphy from a driver's own probe path, but the bus is CherryUSB and
 * the transfers are urbs.  The order is the rtw88/rtw89 arms' (and the previous
 * net80211 line's before them), with this line's stack:
 *
 *   1. the kernel-API services the stack and the driver both use;
 *   2. the CherryUSB class hook (the enumeration-to-usb_driver path);
 *   3. the stack's and driver's `.lkpi_init` entries (cfg80211 first, then
 *      mac80211, then the driver: the link order decides it);
 *   4. the URB engine: `.lkpi_module` walk (this is what calls
 *      usb_register(&rtl8xxxu_driver)) and the drain thread;
 *   5. the asynchronous transfer model.  The engine's default is the blocking
 *      lane the storage bring-up used; rtl8xxxu resubmits its receive urbs from
 *      their completion callbacks (32 of them, plus the TX urb pool), so the
 *      blocking lane transmits and receives nothing;
 *   6. the firmware blob (compiled in; the target has no filesystem);
 *   7. the board's USB domain and one `usbh_initialize` per controller in the
 *      profile's family list, then the pre-plugged-device kick.
 *
 * What a PASS means: the driver's `Firmware revision 28.x (signature 0x88e1)`
 * line was printed by its own probe, `wlan0` exists (mac80211's register), and
 * `dev_open()` returned 0 (mac80211's drv_start -> rtl8xxxu start).  No frame
 * has moved; C4/C5 are separate gates.
 *
 * The hub is part of the evidence, not an assumption: the class hook's claim
 * line carries `bus=<n> hub=<n> port=<n> spd=<n>`, so this case forwards it and
 * the judge can require the topology the round pre-registered (bus 2, hub 2).
 *
 * Anchors:
 *   rtl8xxxu_c3: core ready
 *   rtl8xxxu_c3: usb backend ready
 *   rtl8xxxu_c3: fw registered=<0|err> name=<profile blob>
 *   rtl8xxxu_c3: initcalls run
 *   rtl8xxxu_c3: usb drivers=<n>
 *   rtl8xxxu_c3: usb buses=3,2
 *   rtl8xxxu_c3: usb bus=<n> hcd=<ehci|xhci> base=0x... started
 *   rtl8xxxu_c3: usbh: lkpi claim attempt 0bda:8179 bus=<n> hub=<n> port=<n> ...
 *   rtl8xxxu_c3: firmware <driver line>
 *   rtl8xxxu_c3: wlan0 present=<0|1> after <ms> ms
 *   rtl8xxxu_c3: start rc=<n>
 *   rtl8xxxu_c3: USB ledger on failure (submits/completes/refused/stage_overflow/
 *             slots_exhausted) - "the submit was refused" and "it left and
 *             nothing came back" read the same in a timeout, not here
 *   rtl8xxxu_c3: PASS
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "rtl8xxxu_fw.h"
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

/* The profile's controller family: the two DWC3 xHCI instances (busids 3,2) or
 * the two EHCI roots (busids 1,0).  The 8188EUS is a USB2 device, but this
 * round's bench has it behind the external hub on the bench's "xhci0" - the GT
 * kernel's xhci-hcd.0.auto, the DWC3 at 0xFCC00000, this board map's busid 3 -
 * so the xHCI list is the default and EHCI stays the fallback arm.  A profile
 * states which one, never a runtime guess.  The claim hook prints bus/hub/port,
 * so a moved dongle reads as evidence, not as a silent timeout. */
#if CONFIG_RTL8XXXU_USB_BUS == 0
static const unsigned int c3_buses[] = { 3U, 2U };
#define C3_BUS_LIST_TXT	"3,2"
#else
static const unsigned int c3_buses[] = { 1U, 0U };
#define C3_BUS_LIST_TXT	"1,0"
#endif

/*
 * Console discipline.  rtl8xxxu's probe prints a handful of info lines (the
 * efuse/MAC read, the firmware load, the init_device stages); at 115200 baud a
 * flood would cost the ring that carries the judged anchors.  Lines of this
 * driver and of the USB engine are forwarded as observations, and the
 * firmware's own revision line is promoted to an anchor (it is the C3
 * evidence).  The `%pM` MAC line is deliberately not matched: the port's printk
 * has no `%pM` (it prints the raw words), so the netdev's `mac=` anchor below is
 * the MAC evidence.
 */
static char c3_fw_line[160];

static void
c3_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	if (strstr(line, "Firmware revision") != NULL) {
		strncpy(c3_fw_line, line, sizeof(c3_fw_line) - 1U);
		c3_fw_line[sizeof(c3_fw_line) - 1U] = '\0';
		CASE_ANCHOR("rtl8xxxu_c3: firmware %s\n", line);
		return;
	}
	if (strstr(line, "Loading firmware") != NULL) {
		CASE_ANCHOR("rtl8xxxu_c3: fw image %s\n", line);
		return;
	}
	if (strstr(line, "rtl8xxxu") != NULL || strstr(line, "usbkpi") != NULL ||
	    strstr(line, "usbh") != NULL)
		CASE_INFO("rtl8xxxu_c3: %s\n", line);
}

static void
c3_usb_ledger(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtl8xxxu_c3: usb submits=%u completes=%u refused=%u "
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
	CASE_ANCHOR("rtl8xxxu_c3: usb backend ready\n");

	lkpi_module_init_run();
	CASE_ANCHOR("rtl8xxxu_c3: initcalls run\n");

	lkpi_usb_init();
	CASE_ANCHOR("rtl8xxxu_c3: usb drivers=%u\n",
		    (unsigned)lkpi_usb_driver_count_get());

	lkpi_usb_force_sync_set(false);
	if (rtl8xxxu_fw_register() != 0) {
		CASE_ANCHOR("rtl8xxxu_c3: FAIL firmware blob registration\n");
		return -1;
	}
	CASE_ANCHOR("rtl8xxxu_c3: fw registered=0 name=%s\n", RTL8XXXU_FW_NAME);

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtl8xxxu_c3: usb buses=%s\n", C3_BUS_LIST_TXT);
	for (i = 0U; i < sizeof(c3_buses) / sizeof(c3_buses[0]); i++) {
		unsigned int bus = c3_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtl8xxxu_c3: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtl8xxxu_c3: bus%u usbh_initialize failed\n", bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtl8xxxu_c3: usb bus=%u hcd=%s base=0x%lx started\n", bus,
			    usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
			    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	return started;
}

int
case_rtl8xxxu_c3_run(void)
{
	struct net_device *dev = NULL;
	unsigned int waited = 0U;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("rtl8xxxu_c3: core ready\n");
	lkpi_log_sink_register(c3_sink);

	rc = c3_usb_up();
	if (rc <= 0) {
		CASE_ANCHOR("rtl8xxxu_c3: FAIL no controller started\n");
		c3_usb_ledger();
		return 1;
	}

	/* The default interface appears when the driver's probe reaches
	 * ieee80211_register_hw(); the probe runs from CherryUSB's hub thread
	 * once the interface is claimed.  The 8188EUS's probe is slow by design
	 * (efuse map, 8051 reset, firmware download, PHY/BB init: ~2 s on the
	 * previous line), so the wait is the case's whole budget. */
	while (waited < C3_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(C3_POLL_MS);
		waited += C3_POLL_MS;
	}
	CASE_ANCHOR("rtl8xxxu_c3: wlan0 present=%d after %u ms\n",
		    dev != NULL, (unsigned)waited);
	if (dev == NULL) {
		CASE_ANCHOR("rtl8xxxu_c3: FAIL attach timeout (no dongle claimed, "
			    "or probe failed - see the lines above)\n");
		c3_usb_ledger();
		return 2;
	}
	CASE_ANCHOR("rtl8xxxu_c3: mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		    dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	/* M0 of the mode matrix: the wiphy the driver registered, read from the
	 * registered stack rather than from the driver's own claim.  The 8188EU
	 * is a 2.4 GHz 1T1R 11n part, so the expected row is bands=1 managed=1
	 * ht=1 vht=0. */
	{
		struct wireless_dev *wdev = dev->ieee80211_ptr;
		struct wiphy *wiphy = (wdev != NULL) ? wdev->wiphy : NULL;
		int bands = 0, managed = 0, ht = 0, vht = 0;

		if (wiphy != NULL) {
			if (wiphy->bands[NL80211_BAND_2GHZ] != NULL) {
				bands++;
				if (wiphy->bands[NL80211_BAND_2GHZ]->ht_cap.ht_supported)
					ht = 1;
				if (wiphy->bands[NL80211_BAND_2GHZ]->vht_cap.vht_supported)
					vht = 1;
			}
			if (wiphy->bands[NL80211_BAND_5GHZ] != NULL) {
				bands++;
				if (wiphy->bands[NL80211_BAND_5GHZ]->ht_cap.ht_supported)
					ht = 1;
				if (wiphy->bands[NL80211_BAND_5GHZ]->vht_cap.vht_supported)
					vht = 1;
			}
			managed = !!(wiphy->interface_modes &
				     BIT(NL80211_IFTYPE_STATION));
		}
		CASE_ANCHOR("rtl8xxxu_c3: wiphy bands=%d managed=%d ht=%d vht=%d\n",
			    bands, managed, ht, vht);

		/* The channel's regulatory limit, the same row the GT's
		 * `iw phy` prints ("2432.0 MHz [5] (20.0 dBm)").  A value far
		 * below the GT's would make a low TX power a regdomain fact; a
		 * matching value puts it in the driver's power table instead. */
		if (wiphy != NULL && wiphy->bands[NL80211_BAND_2GHZ] != NULL) {
			struct ieee80211_supported_band *b2 =
				wiphy->bands[NL80211_BAND_2GHZ];
			int ci, maxp = -1;
			u32 chflags = 0;

			for (ci = 0; ci < b2->n_channels; ci++) {
				if (b2->channels[ci].center_freq == 2432) {
					maxp = b2->channels[ci].max_power;
					chflags = b2->channels[ci].flags;
					break;
				}
			}
			CASE_ANCHOR("rtl8xxxu_c3: wiphy ch2432 max_power=%d "
				    "flags=0x%x\n", maxp, chflags);
		}
	}

	if (c3_fw_line[0] == '\0') {
		CASE_ANCHOR("rtl8xxxu_c3: FAIL no firmware revision line (probe did "
			    "not reach the firmware start)\n");
		c3_usb_ledger();
		return 3;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("rtl8xxxu_c3: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8xxxu_c3: FAIL start rc=%d\n", rc);
		c3_usb_ledger();
		return 4;
	}
	c3_usb_ledger();

	CASE_ANCHOR("rtl8xxxu_c3: PASS\n");
	return 0;
}
