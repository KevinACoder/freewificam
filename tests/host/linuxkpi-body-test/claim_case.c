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
#include "usb_errno.h"

#include "linuxkpi_core.h"
#include "linuxkpi_usb.h"
#include "lkpi_usb_backend.h"
#include "lkpi_usbh_class.h"
#include "lkpi_usbh_modeswitch.h"
#include <linux/usb.h>

#include "mock_hcd.h"
#include "wlan_driver_stub.h"

/* the hook's exported class tables (defined with the section macro in
 * lkpi_usbh_class.c; the extern declaration reuses the same type) */
extern const struct usbh_class_info lkpi_wlan_class_info;
extern const struct usbh_class_info lkpi_modeswitch_class_info;

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

/* ------------------------------------------------------------------ */
/* the CD-ROM modeswitch hook: the eject message and the never-claim   */
/*                                                                     */
/* The hook owns a wireless dongle's fake-disk persona: on enumeration */
/* it sends one Bulk-Only eject (a 31-byte CBW carrying SCSI START/    */
/* STOP UNIT with LoEj, then the 13-byte CSW read back) and returns a  */
/* negative connect value so the device is left unclaimed and the hub  */
/* re-enumerates it as the network personality. This case drives the   */
/* real hook against a scripted controller and asserts the exact bytes */
/* the platform's Linux lane sends for the same id.                    */
/* ------------------------------------------------------------------ */

/* the eject message as the device's own mode-switch configuration carries
 * it; the hook's constant must equal this byte for byte */
static const uint8_t modeswitch_truth[31] = {
	0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
	0x1b, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* the scripted controller the hook submits through (the vendored core's
 * usbh_submit_urb dispatch is real; only the controller behind it is
 * scripted) */
static struct usbh_bus g_ms_bus;
static struct usbh_hc_driver g_ms_hcd;

static unsigned int g_ms_out_n;
static unsigned int g_ms_in_n;
static unsigned int g_ms_out_len;
static uint8_t g_ms_out[64];
static int g_ms_out_fail;	/* the CBW submit fails: the device left the bus */

static int
modeswitch_submit(struct usbh_urb *urb)
{
	if ((urb->ep->bEndpointAddress & 0x80u) != 0u) {
		g_ms_in_n++;
		memset(urb->transfer_buffer, 0, urb->transfer_buffer_length);
		urb->transfer_buffer[0] = 0x55;
		urb->transfer_buffer[1] = 0x53;
		urb->transfer_buffer[2] = 0x42;
		urb->transfer_buffer[3] = 0x53;		/* "USBS" CSW signature */
		urb->actual_length = 13;
		return 0;
	}
	g_ms_out_n++;
	if (g_ms_out_fail) {
		return -USB_ERR_NODEV;
	}
	g_ms_out_len = urb->transfer_buffer_length;
	if (g_ms_out_len > sizeof(g_ms_out)) {
		g_ms_out_len = sizeof(g_ms_out);
	}
	memcpy(g_ms_out, urb->transfer_buffer, g_ms_out_len);
	urb->actual_length = urb->transfer_buffer_length;
	return 0;
}

static struct usbh_hubport g_ms_hp;

static void
build_modeswitch_hubport(uint16_t pid, uint8_t cls, uint8_t sub, uint8_t proto)
{
	struct usbh_interface_altsetting *alt;

	memset(&g_ms_hp, 0, sizeof(g_ms_hp));
	g_ms_hp.connected = true;
	g_ms_hp.port = 1;
	g_ms_hp.dev_addr = 2;
	g_ms_hp.speed = USB_SPEED_HIGH;
	g_ms_hp.device_desc.bLength = 18;
	g_ms_hp.device_desc.bDescriptorType = 0x01;
	g_ms_hp.device_desc.bcdUSB = 0x0200;
	g_ms_hp.device_desc.idVendor = 0x0bda;
	g_ms_hp.device_desc.idProduct = pid;
	g_ms_hp.bus = &g_ms_bus;

	alt = &g_ms_hp.config.intf[0].altsetting[0];
	alt->intf_desc.bLength = 9;
	alt->intf_desc.bDescriptorType = 0x04;
	alt->intf_desc.bInterfaceNumber = 0;
	alt->intf_desc.bNumEndpoints = 2;
	alt->intf_desc.bInterfaceClass = cls;
	alt->intf_desc.bInterfaceSubClass = sub;
	alt->intf_desc.bInterfaceProtocol = proto;

	alt->ep[0].ep_desc.bLength = 7;
	alt->ep[0].ep_desc.bDescriptorType = 0x05;
	alt->ep[0].ep_desc.bEndpointAddress = 0x8a;	/* bulk IN */
	alt->ep[0].ep_desc.bmAttributes = 0x02;
	alt->ep[0].ep_desc.wMaxPacketSize = 512;

	alt->ep[1].ep_desc.bLength = 7;
	alt->ep[1].ep_desc.bDescriptorType = 0x05;
	alt->ep[1].ep_desc.bEndpointAddress = 0x0b;	/* bulk OUT */
	alt->ep[1].ep_desc.bmAttributes = 0x02;
	alt->ep[1].ep_desc.wMaxPacketSize = 512;
}

/*
 * The red control for the byte assertion: when armed, the case compares the
 * hook's output against a perturbed message instead of the truth, so a
 * transcription error in the hook's own constant is what this control
 * simulates - the assertion must then fail.
 */
static int g_ms_tamper;

void
lkpi_body_modeswitch_tamper(int on)
{

	g_ms_tamper = on;
}

/*
 * Returns 0 when every assertion holds. *eject_bytes is the length of the
 * message the hook put on the wire (the caller asserts it against the truth),
 * *refused is set when the network personality was left alone.
 */
int
lkpi_body_case_modeswitch(unsigned int *eject_bytes, int *refused)
{
	const struct usbh_class_driver *cd = lkpi_modeswitch_class_info.class_driver;
	int rc;

	*eject_bytes = 0;
	*refused = 0;
	if (cd == NULL || cd->connect == NULL || cd->disconnect == NULL) {
		return -1;
	}

	/* the class table: VID/PID only, and it is this dongle's CD-ROM id */
	if (lkpi_modeswitch_class_info.match_flags != USB_CLASS_MATCH_VID_PID) {
		return -2;
	}
	if (lkpi_modeswitch_id_table[0][0] != 0x0bda ||
	    lkpi_modeswitch_id_table[0][1] != 0x1a2b ||
	    lkpi_modeswitch_id_table[1][0] != 0 ||
	    lkpi_modeswitch_id_table[1][1] != 0) {
		return -3;
	}

	g_ms_hcd.driver_name = "mock-modeswitch";
	g_ms_hcd.driver_desc = "scripted controller for the modeswitch case";
	g_ms_hcd.submit_urb = modeswitch_submit;
	g_ms_bus.hc_driver = &g_ms_hcd;

	/* (1) the CD-ROM persona: one eject, exact bytes, never a claim */
	g_ms_out_n = 0;
	g_ms_in_n = 0;
	g_ms_out_len = 0;
	g_ms_out_fail = 0;
	build_modeswitch_hubport(0x1a2b, 0x08, 0x06, 0x50);
	rc = cd->connect(&g_ms_hp, 0);
	if (rc >= 0) {
		return -4;			/* must never claim */
	}
	if (g_ms_hp.config.intf[0].priv != NULL) {
		return -5;			/* and must leave no view behind */
	}
	if (g_ms_out_n != 1 || g_ms_in_n != 1) {
		return -6;			/* one CBW out, one CSW in */
	}
	if (g_ms_out_len != sizeof(modeswitch_truth)) {
		return -7;
	}
	{
		uint8_t expect[sizeof(modeswitch_truth)];

		memcpy(expect, modeswitch_truth, sizeof(expect));
		if (g_ms_tamper) {
			expect[15] ^= 0x01;	/* red control */
		}
		if (memcmp(g_ms_out, expect, sizeof(expect)) != 0) {
			return -8;		/* the bytes must be the truth */
		}
	}
	*eject_bytes = g_ms_out_len;

	/* the same persona with the other mass-storage subclass must be handled
	 * the same way (the guard keys on the class, not the subclass) */
	g_ms_out_n = 0;
	g_ms_in_n = 0;
	build_modeswitch_hubport(0x1a2b, 0x08, 0x05, 0x50);
	rc = cd->connect(&g_ms_hp, 0);
	if (rc >= 0 || g_ms_out_n != 1 || g_ms_in_n != 1) {
		return -9;
	}

	/* (2) the network personality is not this hook's device: it must be
	 * left alone (no second eject after the flip) */
	g_ms_out_n = 0;
	g_ms_in_n = 0;
	build_modeswitch_hubport(0xc820, 0xff, 0xff, 0xff);
	rc = cd->connect(&g_ms_hp, 0);
	if (rc >= 0) {
		return -10;
	}
	if (g_ms_out_n != 0 || g_ms_in_n != 0) {
		return -11;
	}
	*refused = 1;

	/* a non-mass-storage interface under the same id must be left alone */
	g_ms_out_n = 0;
	g_ms_in_n = 0;
	build_modeswitch_hubport(0x1a2b, 0xff, 0xff, 0xff);
	rc = cd->connect(&g_ms_hp, 0);
	if (rc >= 0 || g_ms_out_n != 0 || g_ms_in_n != 0) {
		return -12;
	}

	/* (3) the device leaving the bus on the CBW is the flip itself, not a
	 * failure: no CSW is expected then */
	g_ms_out_n = 0;
	g_ms_in_n = 0;
	g_ms_out_fail = 1;
	build_modeswitch_hubport(0x1a2b, 0x08, 0x06, 0x50);
	rc = cd->connect(&g_ms_hp, 0);
	g_ms_out_fail = 0;
	if (rc >= 0 || g_ms_out_n != 1 || g_ms_in_n != 0) {
		return -13;
	}

	/* (4) the shared guard: the wireless hook must not claim the CD-ROM
	 * persona - it hands the device to the modeswitch helper, which sends
	 * the eject.  This is the path the walk takes when the mass-storage
	 * hook is matched before the modeswitch one, which is the order the
	 * compiler happened to emit (measured on the board: without the
	 * hand-over the guard's silent refusal left the dongle unflipped and
	 * the whole bring-up stuck at the first enumeration). */
	g_ms_out_n = 0;
	g_ms_in_n = 0;
	build_modeswitch_hubport(0x1a2b, 0x08, 0x06, 0x50);
	rc = lkpi_wlan_class_info.class_driver->connect(&g_ms_hp, 0);
	if (rc >= 0 || g_ms_hp.config.intf[0].priv != NULL) {
		return -14;
	}
	if (g_ms_out_n != 1 || g_ms_in_n != 1) {
		return -15;		/* the hand-over must reach the eject */
	}

	return 0;
}
