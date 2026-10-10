/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    lkpi_usbh_modeswitch.c
 * @brief   Flip the RTL8821CU-class dongle from its CD-ROM persona to its
 *          network personality, from inside the image.
 *
 * @design class hook + message table. The device enumerates as a mass-storage
 *         CD-ROM (single interface, class 08/05/50) and only exposes its
 *         network interface after it is told to eject the medium; the eject is
 *         a Bulk-Only transaction - a 31-byte Command Block Wrapper carrying a
 *         SCSI START/STOP UNIT (LoEj) command on the bulk-OUT endpoint, and the
 *         13-byte Command Status Wrapper read back from bulk-IN. The status
 *         stage is not optional: the device acknowledges the command without
 *         the CSW read but does not flip until it is consumed. The hook sends
 *         the exchange and never claims the device - returning a negative
 *         connect value leaves the interface unclaimed and lets the hub
 *         machinery re-enumerate it under the personality the wireless hook
 *         matches (third-party/cherryusb/core/usbh_core.c: the walk takes the
 *         first class_info that matches, and a negative connect is not a
 *         fall-through).
 *
 *         The message bytes are the ones the device's own mode-switch
 *         configuration carries (the same 31 bytes the platform's Linux lane
 *         sends for this id); the host test asserts them byte for byte, so the
 *         constant below is the single source and the test is its anchor.
 * @date    11.10.2026
 */

#include <stdint.h>
#include <string.h>

#include "usbh_core.h"
#include "usb_errno.h"

#include "lkpi_usbh_modeswitch.h"
#include "linuxkpi_core.h"

const uint16_t lkpi_modeswitch_id_table[][2] = {
	{ LKPI_MODESWITCH_VID, LKPI_MODESWITCH_PID },
	{ 0, 0 },
};

/*
 * SCSI START/STOP UNIT with the LoEj bit, inside a 31-byte Bulk-Only CBW:
 * signature "USBC", tag, zero transfer length, OUT flags, LUN 0, six-byte CDB
 * `1b 00 00 00 02 00`. The tail of the CBWCB stays zero.
 */
static const uint8_t lkpi_modeswitch_eject_cbw[31] = {
	0x55, 0x53, 0x42, 0x43,			/* "USBC" CBW signature */
	0x12, 0x34, 0x56, 0x78,			/* CBW tag */
	0x00, 0x00, 0x00, 0x00,			/* transfer length 0 */
	0x00,					/* flags: OUT */
	0x00,					/* LUN 0 */
	0x06,					/* CBWCB length */
	0x1b, 0x00, 0x00, 0x00, 0x02, 0x00,	/* START/STOP UNIT, LoEj */
};

#define	LKPI_MODESWITCH_ATTEMPTS	3
#define	LKPI_MODESWITCH_TIMEOUT_MS	1000
#define	LKPI_MODESWITCH_CSW_LEN		13

/* The class triple the CD-ROM persona presents; the eject only makes sense on
 * the mass-storage interface. */
#define	LKPI_MODESWITCH_INTF_CLASS	0x08

/* bmAttributes bits 1:0 carry the endpoint transfer type; 2 is bulk. Spelled
 * locally because the CherryUSB face and the Linux face name this constant
 * differently. */
#define	LKPI_MODESWITCH_EP_TYPE_MASK	0x03u
#define	LKPI_MODESWITCH_EP_TYPE_BULK	0x02u

/* Handed to the controller for DMA, so it is cache-line aligned and lives for
 * the whole exchange (one buffer serves the CBW out and the CSW in). */
static uint8_t lkpi_modeswitch_buf[64] __attribute__((aligned(64)));

static int
lkpi_modeswitch_bulk(struct usbh_hubport *hport,
    struct usb_endpoint_descriptor *ep, uint32_t len)
{
	struct usbh_urb urb;
	int ret;

	/* A NULL completion makes the submit synchronous, the shape the vendored
	 * mass-storage class and this tree's rawbulk diagnostic both use. */
	usbh_bulk_urb_fill(&urb, hport, ep, lkpi_modeswitch_buf, len,
	    LKPI_MODESWITCH_TIMEOUT_MS, NULL, NULL);
	ret = usbh_submit_urb(&urb);
	if (ret == 0)
		ret = (int)urb.actual_length;
	return ret;
}

static int
lkpi_modeswitch_eject(struct usbh_hubport *hport,
    struct usb_endpoint_descriptor *epout,
    struct usb_endpoint_descriptor *epin)
{
	unsigned int attempt;
	int n;

	for (attempt = 1; attempt <= LKPI_MODESWITCH_ATTEMPTS; attempt++) {
		memcpy(lkpi_modeswitch_buf, lkpi_modeswitch_eject_cbw,
		    sizeof(lkpi_modeswitch_eject_cbw));

		n = lkpi_modeswitch_bulk(hport, epout,
		    sizeof(lkpi_modeswitch_eject_cbw));
		lkpi_printf("usbh: modeswitch eject attempt %u: %d byte(s) "
		    "sent\n", attempt, n);
		if (n < 0) {
			/* The device left the bus on the eject: that is the
			 * flip itself, not a failure. */
			lkpi_printf("usbh: modeswitch accepted (device left "
			    "the bus)\n");
			return 0;
		}

		n = lkpi_modeswitch_bulk(hport, epin, LKPI_MODESWITCH_CSW_LEN);
		if (n >= LKPI_MODESWITCH_CSW_LEN) {
			lkpi_printf("usbh: modeswitch CSW: %d byte(s), "
			    "signature %02x%02x%02x%02x status %02x\n", n,
			    lkpi_modeswitch_buf[0], lkpi_modeswitch_buf[1],
			    lkpi_modeswitch_buf[2], lkpi_modeswitch_buf[3],
			    lkpi_modeswitch_buf[12]);
			return 0;
		}
		lkpi_printf("usbh: modeswitch CSW read %d byte(s), "
		    "retrying\n", n);
	}
	return -1;
}

int
lkpi_modeswitch_connect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usb_endpoint_descriptor *epout = NULL, *epin = NULL;
	unsigned int i;
	int ret;

	if (!hport->connected) {
		/* A personality flip bounces the device: one connect fires
		 * against the hubport whose teardown is already in flight. */
		return -USB_ERR_NODEV;
	}
	if (hport->device_desc.idVendor != LKPI_MODESWITCH_VID ||
	    hport->device_desc.idProduct != LKPI_MODESWITCH_PID)
		return -USB_ERR_INVAL;
	if (hport->config.intf[intf].altsetting[0].intf_desc.bInterfaceClass !=
	    LKPI_MODESWITCH_INTF_CLASS)
		return -USB_ERR_INVAL;

	for (i = 0; i < CONFIG_USBHOST_MAX_ENDPOINTS; i++) {
		struct usb_endpoint_descriptor *ep =
		    &hport->config.intf[intf].altsetting[0].ep[i].ep_desc;

		if (ep->bLength == 0)
			continue;
		if ((ep->bmAttributes & LKPI_MODESWITCH_EP_TYPE_MASK) ==
		    LKPI_MODESWITCH_EP_TYPE_BULK) {
			if ((ep->bEndpointAddress & 0x80u) != 0u)
				epin = ep;
			else
				epout = ep;
		}
	}
	if (epin == NULL || epout == NULL) {
		lkpi_printf("usbh: modeswitch %04x:%04x: no bulk endpoint "
		    "pair\n", LKPI_MODESWITCH_VID, LKPI_MODESWITCH_PID);
		return -USB_ERR_INVAL;
	}

	lkpi_printf("usbh: modeswitch %04x:%04x CD-ROM persona, ejecting\n",
	    LKPI_MODESWITCH_VID, LKPI_MODESWITCH_PID);
	ret = lkpi_modeswitch_eject(hport, epout, epin);
	lkpi_printf("usbh: modeswitch %s, expecting %04x:%04x\n",
	    ret == 0 ? "sent" : "failed", LKPI_MODESWITCH_VID,
	    LKPI_MODESWITCH_TARGET_PID);

	/* Never claim: the device is about to re-enumerate as the network
	 * personality, which the wireless hook owns. */
	return -USB_ERR_INVAL;
}

int
lkpi_modeswitch_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
	(void)hport;
	(void)intf;

	/* The hook never claimed the interface, so there is nothing to tear
	 * down. */
	return 0;
}
