/**
 * @file    usb_ehci.c
 * @brief   Case usb-ehci: the U-disk behind the panel hub.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case row of the regression table (tests/board/common/board_suite.h);
 *          the body is shared with the xHCI row (tests/usb_msc.c).
 */

#include "board_suite.h"

int usb_msc_case(unsigned busid, const char *hcd);

static void usb_ehci(void)
{
	/* Two EHCI roots carry the panel ports; the U-disk was recorded behind
	 * one of them and the other carries a non-storage device, so the row
	 * takes whichever root answers. */
	if (usb_msc_case(1U, "ehci") != 0 && usb_msc_case(0U, "ehci") != 0) {
		board_case_fail("no mass-storage device on either EHCI root");
	}
}

BOARD_TEST("usb-ehci", usb_ehci);
