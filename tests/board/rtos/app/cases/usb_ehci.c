/*
 * @file   usb_ehci.c
 * @brief  Case usb_ehci: the U-disk behind the panel's USB2 roots.
 *
 * Two EHCI roots (busid 0/1) carry the panel connectors behind on-board hubs;
 * the disk is on one of them.  This is the controller family the 8851BU sits
 * on, so this arm is the target path's throughput ceiling.  The body is shared
 * with the xHCI case (`usb_msc.c`).
 */

#include "cases.h"

int usb_msc_case(unsigned int busid, const char *hcd);

int
case_usb_ehci_run(void)
{
	/* busid 1 = EHCI1 at 0xFD880000, busid 0 = EHCI0 at 0xFD800000.  The
	 * panel's disk is behind one of them; fall back only when the first root
	 * saw no device at all (rc 2), never after a measurement failure. */
	unsigned int rc = usb_msc_case(1U, "ehci");

	if (rc == 0)
		return 0;
	if (rc == 2)
		rc = usb_msc_case(0U, "ehci");
	if (rc == 0)
		return 0;
	CASE_ANCHOR("usbh: msc FAIL EHCI roots: %s\n",
		    rc == 2 ? "no mass-storage device" : "measurement failed");
	return 1;
}
