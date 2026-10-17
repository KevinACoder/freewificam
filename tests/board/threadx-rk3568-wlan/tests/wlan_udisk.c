/**
 * @file    wlan_udisk.c
 * @brief   Case wlan-udisk: the linuxkpi BOT driver on the U-disks behind the
 *          xHCI and EHCI controllers, at the native row's sizes plus a
 *          command-size sweep.
 * @author  zhugengyu
 * @date    17.10.2026
 * @design  the comparison this case exists for: one U-disk, one controller,
 *          two stacks - the linuxkpi USB engine with the clean-room BOT driver
 *          (components-side consumer, `common/udisk/lkpi_udisk_core.c`) here,
 *          and CherryUSB's own `usbh_msc` class in the regression image's
 *          `usb-xhci` / `usb-ehci` rows.  Both read 32 MiB and write 16 MiB in
 *          64 KiB commands, so the two numbers differ only by the stack between
 *          the driver and the controller.  Same shape as the native row
 *          (tests/board/common/cases/usb_msc.c): register the driver before
 *          enumeration, bring the controller up, wait for the claim, then run
 *          the bandwidth entries.  Speed is recorded, not judged.
 *
 * Two buses, deliberately: bus 3 is the xHCI instance the native xhci row uses,
 * and bus 1 is the EHCI instance the WiFi dongle itself sits on - the bus whose
 * per-transfer cost the wireless line pays.  The xHCI half is the case's
 * judgement (its disk must be there); the EHCI half records what it finds,
 * because "no device on this bus" is an answer, not a failure.
 *
 * The command-size sweep (both stacks run it) is the part that separates a
 * stack's per-transfer fixed cost from its per-byte cost; see
 * `lkpi_udisk_sweep` for why the whole-run wall clock is the only clock used.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "usb_glue.h"

#include "lkpi_udisk.h"

/* the xHCI instance the native xhci row uses, and the EHCI instance the WiFi
 * dongle sits on (wlan_probe.c tries bus 1 first for the same reason) */
#define UDISK_BUS_XHCI		3U
#define UDISK_BUS_EHCI		1U
/* the native row's sizes, so the two stacks are read at one workload */
#define UDISK_MIB_READ		32U
#define UDISK_MIB_WRITE		16U
#define UDISK_WAIT_SPINS	600U	/* 100 ms per spin -> 60 s */

/* the driver has one claimed instance per disk in this image; the driver's own
 * post-probe hook hands the interface over so the case can trigger it */
static struct usb_interface *s_udisk_intf;
static int s_udisk_registered;

void
ud_post_probe(struct usb_interface *intf)
{

	s_udisk_intf = intf;
	LOG_ANCHOR("wlan: udisk claimed\n");
}

static void
wlan_udisk_bus(unsigned int busid, const char *hcd, int required)
{
	unsigned int spin;

	if (!s_udisk_registered) {
		if (lkpi_udisk_register() != 0) {
			board_case_fail("the udisk driver did not register");
			return;
		}
		s_udisk_registered = 1;
	}
	/* wait for THIS bus's device, not the one the previous bus handed over */
	s_udisk_intf = NULL;
	if (usb_board_init(busid) != 0) {
		board_case_fail("the USB board layer did not come up");
		return;
	}
	LOG_INFO("wlan: udisk core init bus=%u hcd=%s base=0x%lx\n", busid, hcd,
	    (unsigned long)usb_board_reg_base(busid));
	if (usbh_initialize(busid, usb_board_reg_base(busid),
	    usb_board_event_handler) != 0) {
		board_case_fail("usbh_initialize failed on the bus");
		return;
	}
	usb_board_after_start(busid);

	for (spin = 0; spin < UDISK_WAIT_SPINS; spin++) {
		if (s_udisk_intf != NULL)
			break;
		(void)osDelay(100U);
	}
	if (s_udisk_intf == NULL) {
		if (required) {
			board_case_fail("no mass-storage device on the bus");
			return;
		}
		LOG_ANCHOR("wlan: udisk bus=%u absent\n", busid);
		return;
	}

	/* The two halves differ in what a failure means. The xHCI row is this
	 * case's judgement - its disk is the one the native row measures, and a
	 * failure there is a failure of the stack. The EHCI row is attribution:
	 * a device this driver cannot read is an answer about the device, and it
	 * must not turn the suite red (the reader on that bus STALLs its first
	 * data phase; see the round's ledger). */
	/* The sweep runs before the fixed-size runs.  Its smallest size is the
	 * one most likely to succeed, so a path that only breaks at large
	 * transfers still leaves the sizes that did work on the record - which
	 * is exactly the shape the EHCI half has (a 64 KiB bulk read times out
	 * there while INQUIRY and READ CAPACITY are fine). */
	if (lkpi_udisk_inq(s_udisk_intf) != 0 ||
	    lkpi_udisk_sweep(s_udisk_intf, false) != 0 ||
	    lkpi_udisk_sweep(s_udisk_intf, true) != 0 ||
	    lkpi_udisk_bw(s_udisk_intf, UDISK_MIB_READ, false) != 0 ||
	    lkpi_udisk_bw(s_udisk_intf, UDISK_MIB_WRITE, true) != 0) {
		if (required) {
			board_case_fail("the udisk measurement runs failed");
			return;
		}
		LOG_ANCHOR("wlan: udisk bus=%u measurement failed\n", busid);
		return;
	}
	LOG_ANCHOR("wlan: udisk bw done bus=%u\n", busid);
}

static void
wlan_udisk(void)
{

	wlan_udisk_bus(UDISK_BUS_XHCI, "xhci", 1);
	wlan_udisk_bus(UDISK_BUS_EHCI, "ehci", 0);
}

int
lkpi_udisk_register(void)
{

	return usb_register(&lkpi_udisk_driver);
}

BOARD_TEST("wlan-udisk", wlan_udisk);
