/**
 * @file    usb_xhci.c
 * @brief   Case usb-xhci: the U-disk behind the upper socket's hub.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case row of the regression table (tests/board/common/board_suite.h);
 *          the body is shared with the EHCI row (tests/usb_msc.c).
 */

#include "board_suite.h"

int usb_msc_case(unsigned busid, const char *hcd);

static void usb_xhci(void)
{
	if (usb_msc_case(3U, "xhci") != 0) {
		board_case_fail("no mass-storage device on the xHCI bus");
	}
}

BOARD_TEST("usb-xhci", usb_xhci);
