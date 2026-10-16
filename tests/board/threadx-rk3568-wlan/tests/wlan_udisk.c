/**
 * @file    wlan_udisk.c
 * @brief   Case wlan-udisk: the linuxkpi BOT driver on the U-disk behind the
 *          xHCI controller, measured at the sizes the native MSC row uses.
 * @author  zhugengyu
 * @date    17.10.2026
 * @design  the comparison this case exists for: one U-disk, one controller,
 *          two stacks - the linuxkpi USB engine with the clean-room BOT driver
 *          (components-side consumer, `common/udisk/lkpi_udisk_core.c`) here,
 *          and CherryUSB's own `usbh_msc` class in the regression image's
 *          `usb-xhci` row.  Both read 32 MiB and write 16 MiB in 64 KiB
 *          commands, so the two numbers differ only by the stack between the
 *          driver and the controller.  Same shape as the native row
 *          (tests/board/common/cases/usb_msc.c): register the driver before
 *          enumeration, bring the controller up, wait for the claim, then run
 *          the bandwidth entries.  Speed is recorded, not judged.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "usb_glue.h"

#include "lkpi_udisk.h"

/* the xHCI instance the U-disk sits on (the same bus the native row uses) */
#define UDISK_BUSID		3U
/* the native row's sizes, so the two stacks are read at one workload */
#define UDISK_MIB_READ		32U
#define UDISK_MIB_WRITE		16U
#define UDISK_WAIT_SPINS	600U	/* 100 ms per spin -> 60 s */

/* the driver has one claimed instance in this image; the driver's own
 * post-probe hook hands the interface over so the case can trigger it */
static struct usb_interface *s_udisk_intf;

void
ud_post_probe(struct usb_interface *intf)
{

	s_udisk_intf = intf;
	LOG_ANCHOR("wlan: udisk claimed\n");
}

int
lkpi_udisk_register(void)
{

	return usb_register(&lkpi_udisk_driver);
}

static void
wlan_udisk(void)
{
	unsigned int spin;

	/* the driver first: the class hook probes the moment a device shows up */
	if (lkpi_udisk_register() != 0) {
		board_case_fail("the udisk driver did not register");
		return;
	}
	if (usb_board_init(UDISK_BUSID) != 0) {
		board_case_fail("the xHCI board layer did not come up");
		return;
	}
	LOG_INFO("wlan: udisk core init bus=%u base=0x%lx\n", UDISK_BUSID,
	    (unsigned long)usb_board_reg_base(UDISK_BUSID));
	if (usbh_initialize(UDISK_BUSID, usb_board_reg_base(UDISK_BUSID),
	    usb_board_event_handler) != 0) {
		board_case_fail("usbh_initialize failed on the xHCI bus");
		return;
	}
	usb_board_after_start(UDISK_BUSID);

	for (spin = 0; spin < UDISK_WAIT_SPINS; spin++) {
		if (s_udisk_intf != NULL)
			break;
		(void)osDelay(100U);
	}
	if (s_udisk_intf == NULL) {
		board_case_fail("no mass-storage device on the xHCI bus");
		return;
	}

	if (lkpi_udisk_bw(s_udisk_intf, UDISK_MIB_READ, false) != 0) {
		board_case_fail("the udisk read bandwidth run failed");
		return;
	}
	if (lkpi_udisk_bw(s_udisk_intf, UDISK_MIB_WRITE, true) != 0) {
		board_case_fail("the udisk write bandwidth run failed");
		return;
	}
	LOG_ANCHOR("wlan: udisk bw done\n");
}

BOARD_TEST("wlan-udisk", wlan_udisk);
