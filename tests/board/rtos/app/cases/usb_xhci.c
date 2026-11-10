/*
 * @file   usb_xhci.c
 * @brief  Case usb_xhci: the U-disk behind one of the board's two xHCI
 *         (DWC3) instances.
 *
 * The USB3 ports are High-Speed only on this board (the SuperSpeed lanes are
 * routed to SATA), so a USB2 U-disk enumerates here at 480 Mbit/s - the same
 * device speed as the EHCI arm, on the other controller family.  The body is
 * shared with the EHCI case (`usb_msc.c`).
 */

#include "cases.h"

int usb_msc_case(unsigned int busid, const char *hcd);

int
case_usb_xhci_run(void)
{
	/* RK3568: busid 3 = xHCI1 at 0xFCC00000 (the upper Type-A port).  The
	 * second xHCI root (busid 2) is deliberately not tried: it carries a
	 * different mass-storage device on the bench (a 7 MiB CD-ROM persona),
	 * and a fallback after a *measurement* failure would report that
	 * device's numbers under this arm's name.  A disk on the other root
	 * means this profile's bus changes, not that the case guesses.
	 *
	 * firefly v2: busid 0 = usb3_0 xHCI at 0x31a08000 - the same root and
	 * port the AIC8800 dongle rode in W7-01/02 (the stick now sits there).
	 * Root 1 stays out for the same one-disk-one-bus reason. */
#ifdef CONFIG_BOARD_FIREFLY_V2
	unsigned int rc = usb_msc_case(0U, "xhci");

	if (rc == 0)
		return 0;
	CASE_ANCHOR("usbh: msc FAIL xHCI root 0: %s\n",
		    rc == 2 ? "no mass-storage device" : "measurement failed");
	return 1;
#else
	unsigned int rc = usb_msc_case(3U, "xhci");

	if (rc == 0)
		return 0;
	CASE_ANCHOR("usbh: msc FAIL xHCI root 3: %s\n",
		    rc == 2 ? "no mass-storage device" : "measurement failed");
	return 1;
#endif
}
