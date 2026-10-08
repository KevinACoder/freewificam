/**
 * @file    usb_ehci.c
 * @brief   Case usb-ehci: the U-disk behind the panel hub.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case row of the regression table (tests/board/common/board_suite.h);
 *          the body is shared with the xHCI row (tests/usb_msc.c).
 */

#include "board_suite.h"

void usb_msc_case(unsigned busid, const char *hcd);

static void usb_ehci(void)
{
	usb_msc_case(1U, "ehci");
}

BOARD_TEST("usb-ehci", usb_ehci);
