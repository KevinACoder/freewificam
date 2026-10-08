/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    claim_case.c
 * @brief   The class-hook claim case: it drives lkpi_usbh_class.c's own
 *          connect/disconnect entries against a scripted CherryUSB hubport and
 *          then moves a transfer through the device the hook built - the
 *          chain whose type defects (port and endpoint identity) were found on
 *          the board in the WLAN bring-up rounds.
 *
 * @design claim-then-transfer case. The hook's connect path had two defects
 *         that only a board round exposed: dev->_hport was bound the raw
 *         usbh_hubport instead of the engine-level port, and endpoint hcpriv
 *         was the raw descriptor instead of a lkpi_usb_be_ep. Both made the
 *         backend read descriptor bytes as pointers (two data aborts, ELR at
 *         the control and bulk submissions). Those fixes are in the hook, so
 *         this case exercises the fixed chain end to end on the host: the
 *         real hook builds the Linux view, the real engine resolves the
 *         endpoint, the scripted controller moves the bytes. A regression in
 *         the double indirection faults or refuses here, not on the board.
 *
 * This TU is deliberately its own translation unit: it includes the CherryUSB
 * headers (the hook's first-include rule), and the suite's main file must not.
 *
 * @date    09.10.2026
 */

#include <stdint.h>
#include <string.h>

#include "usbh_core.h"
#include "usbh_hub.h"

#include "linuxkpi_core.h"
#include "linuxkpi_usb.h"
#include "lkpi_usb_backend.h"
#include "lkpi_usbh_class.h"
#include <linux/usb.h>

#include "mock_hcd.h"
#include "wlan_driver_stub.h"

/* the hook's exported class tables (defined with the section macro in
 * lkpi_usbh_class.c; the extern declaration reuses the same type) */
extern const struct usbh_class_info lkpi_wlan_class_info;

#define CLAIM_VID 0x0bda
#define CLAIM_PID 0x8179

/* the scripted hubport: a vendor-specific interface with two bulk endpoints,
 * the shape the 8188eu dongle presents after enumeration */
static struct usbh_hubport g_hp;

static void
build_hubport(void)
{
	struct usbh_interface_altsetting *alt;

	memset(&g_hp, 0, sizeof(g_hp));
	g_hp.connected = true;
	g_hp.port = 1;
	g_hp.dev_addr = 2;
	g_hp.speed = USB_SPEED_HIGH;
	g_hp.device_desc.bLength = 18;
	g_hp.device_desc.bDescriptorType = 0x01;
	g_hp.device_desc.bcdUSB = 0x0200;
	g_hp.device_desc.idVendor = CLAIM_VID;
	g_hp.device_desc.idProduct = CLAIM_PID;

	alt = &g_hp.config.intf[0].altsetting[0];
	alt->intf_desc.bLength = 9;
	alt->intf_desc.bDescriptorType = 0x04;
	alt->intf_desc.bInterfaceNumber = 0;
	alt->intf_desc.bNumEndpoints = 2;
	alt->intf_desc.bInterfaceClass = 0xff;
	alt->intf_desc.bInterfaceSubClass = 0xff;
	alt->intf_desc.bInterfaceProtocol = 0xff;

	alt->ep[0].ep_desc.bLength = 7;
	alt->ep[0].ep_desc.bDescriptorType = 0x05;
	alt->ep[0].ep_desc.bEndpointAddress = 0x81;	/* bulk IN */
	alt->ep[0].ep_desc.bmAttributes = 0x02;
	alt->ep[0].ep_desc.wMaxPacketSize = 512;

	alt->ep[1].ep_desc.bLength = 7;
	alt->ep[1].ep_desc.bDescriptorType = 0x05;
	alt->ep[1].ep_desc.bEndpointAddress = 0x01;	/* bulk OUT */
	alt->ep[1].ep_desc.bmAttributes = 0x02;
	alt->ep[1].ep_desc.wMaxPacketSize = 512;
}

struct claim_watch {
	volatile unsigned int n;
	volatile int status;
	volatile unsigned int actual;
};

static void
claim_urb_done(struct urb *urb)
{
	struct claim_watch *w = urb->context;

	w->n++;
	w->status = urb->status;
	w->actual = (unsigned int)urb->actual_length;
}

/*
 * Returns 0 and fills the reference counts the case asserts on. `b` is the
 * suite's scripted controller (its link must be up).
 */
int
lkpi_body_case_claim(struct mock_backend *b, unsigned int *probe_before,
    unsigned int *probe_8188, unsigned int *disconnects_out,
    unsigned int *xfer_actual)
{
	struct usbh_class_driver *cd;
	struct usb_device *dev;
	struct usb_interface *intf;
	struct usb_host_endpoint *hep;
	struct urb *urb;
	struct claim_watch w;
	uint8_t buf[32];
	unsigned int i;
	int rc;

	(void)b;
	*probe_8188 = 0;
	*disconnects_out = 0;
	*xfer_actual = 0;

	build_hubport();
	cd = lkpi_wlan_class_info.class_driver;
	if (cd == NULL || cd->connect == NULL || cd->disconnect == NULL) {
		return -1;
	}
	*probe_before = wlan_stub_probe_calls();

	/* the claim: the hook builds the Linux view and runs the real id_table */
	rc = cd->connect(&g_hp, 0);
	if (rc != 0) {
		return -2;
	}
	if (lkpi_usbh_claimed_hport() != &g_hp || lkpi_usbh_claimed_intf() != 0) {
		return -3;
	}
	dev = (struct usb_device *)g_hp.config.intf[0].priv;
	if (dev == NULL || dev->_vendor != CLAIM_VID ||
	    dev->_product != CLAIM_PID) {
		return -4;
	}
	if (wlan_stub_probe_calls() != *probe_before + 1) {
		return -5;
	}
	*probe_8188 = wlan_stub_probe_8188_calls();

	/* the endpoint chain the board round fixed: the hook's eps array hangs
	 * off the same allocation as the engine-level port, and each
	 * usb_host_endpoint carries the lkpi_usb_be_ep, not the descriptor */
	intf = (struct usb_interface *)dev->_intf;
	if (intf == NULL || intf->cur_altsetting == NULL ||
	    intf->cur_altsetting->num_endpoints < 2) {
		return -6;
	}
	hep = &intf->cur_altsetting->endpoint[1];	/* the OUT endpoint */
	if (hep->hcpriv == NULL || (void *)hep->hcpriv == (void *)&hep->desc) {
		return -7;
	}

	/* the transfer: through the engine, out the claimed endpoint, moved by
	 * the scripted controller */
	for (i = 0; i < sizeof(buf); i++) {
		buf[i] = (uint8_t)(0x40 + i);
	}
	memset(&w, 0, sizeof(w));
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (urb == NULL) {
		return -8;
	}
	usb_fill_bulk_urb(urb, dev, usb_sndbulkpipe(dev, 1), buf,
	    (int)sizeof(buf), claim_urb_done, &w);
	urb->hep = hep;
	rc = usb_submit_urb(urb, GFP_KERNEL);
	if (rc != 0) {
		usb_free_urb(urb);
		return -9;
	}
	for (i = 0; i < 2000 && w.n == 0; i++) {
		lkpi_msleep(1);
	}
	usb_free_urb(urb);
	if (w.n != 1 || w.status != 0 || w.actual != sizeof(buf)) {
		return -10;
	}
	*xfer_actual = w.actual;

	/* the release path: disconnect must clear the claim and tell the
	 * driver, and the built view must be gone afterwards */
	*disconnects_out = wlan_stub_disconnect_calls();
	rc = cd->disconnect(&g_hp, 0);
	if (rc != 0) {
		return -11;
	}
	if (lkpi_usbh_claimed_hport() != NULL ||
	    g_hp.config.intf[0].priv != NULL) {
		return -12;
	}
	if (wlan_stub_disconnect_calls() != *disconnects_out + 1) {
		return -13;
	}
	*disconnects_out = wlan_stub_disconnect_calls();
	return 0;
}
