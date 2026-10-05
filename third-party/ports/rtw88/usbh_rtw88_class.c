/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * usbh_rtw88_class.c - the CherryUSB class hook that hands a claimed
 * wireless dongle to the Linux usb_driver registered through the
 * linuxkpi module table (S9/rtw88-adapt W2).
 *
 * This is the CherryUSB-side replacement for the Linux USB core's
 * enumerate-and-match loop: CherryUSB enumerates (its own hub thread),
 * and when the interface walk finds a vendor-class interface carrying a
 * bulk IN/OUT pair behind a Realtek VID, this hook builds the Linux
 * struct usb_device/usb_interface wrappers over the hubport and runs
 * lkpi_usb_probe_drivers() - which is the real rtw88 id_table doing the
 * chip-level match, not a second copy of it.
 *
 * Board-proven shape: prior project's [EXPL] usbh_rtw88_class.c (L1
 * round); the modeswitch companion (usbh_modeswitch.c) owns the 1a2b
 * CD-ROM personality that precedes this hook's c820 match.
 */
#include <stdint.h>
#include <string.h>

/* CherryUSB headers first: with their USB_DEF_H set, linux/usb.h skips
 * the descriptor definitions and the whole TU uses one descriptor type. */
#include "usbh_core.h"
#include "usbh_hub.h"
#include "usb_errno.h"

#include "linuxkpi_core.h"
#include "linuxkpi_usb.h"
#include <linux/usb.h>

/*
 * VID/PID pairs this hook may hand to the driver table.  The c820 row is
 * the modeswitched 8821CU (the dongle's pre-modeswitch person 1a2b is a
 * CD-ROM, not a wireless interface, and must not be claimed - the eject
 * helper below is the M3 bring-up's first board task).
 */
static const uint16_t usbh_rtw88_ids[][2] = {
	{ 0x0bda, 0xc820 },	/* RTL8821CU after modeswitch */
	{ 0x0bda, 0xc811 },	/* RTL8811CU after modeswitch */
	{ 0x0bda, 0xb820 },	/* RTL8821CU variant */
	{ 0x0bda, 0xb82b },	/* RTL8822CU variant */
	{ 0x0bda, 0xc82b },	/* RTL8822CU */
	{ 0x0bda, 0xb812 },	/* RTL8822BU */
	{ 0x0bda, 0xd723 },	/* RTL8723DU */
	{ 0, 0 }
};

/* how many bulk endpoints a wireless data interface carries */
#define	USBH_RTW88_MIN_BULK_EPS	2

/* The Realtek firmware keeps switching personalities for a few hundred ms
 * after the eject; probing into a mid-flip device STALLs ep0 and wedges it
 * past recovery (M3 board rounds: every vendor transfer -5/-32, then even
 * get-descriptor stalls until power cycle).  The usbdi route's deferred
 * autoconf attach gave the device this head start implicitly - pay it
 * explicitly here, before the first vendor register access. */
#define	RTW88U_PROBE_SETTLE_MS	500

static int
usbh_rtw88_id_match(uint16_t vendor, uint16_t product)
{
	uint32_t i;

	for (i = 0; usbh_rtw88_ids[i][0] != 0; i++) {
		if (usbh_rtw88_ids[i][0] == vendor &&
		    usbh_rtw88_ids[i][1] == product)
			return 1;
	}
	return 0;
}

static uint32_t
usbh_rtw88_bulk_ep_count(const struct usbh_hubport *hport, uint8_t intf,
    int *has_in, int *has_out)
{
	const struct usbh_interface_altsetting *alt =
	    &hport->config.intf[intf].altsetting[0];
	uint32_t count = 0;
	uint8_t i;

	*has_in = 0;
	*has_out = 0;
	for (i = 0; i < CONFIG_USBHOST_MAX_ENDPOINTS; i++) {
		uint8_t attr = alt->ep[i].ep_desc.bmAttributes;

		if (alt->ep[i].ep_desc.bLength == 0)
			continue;
		if (USB_GET_ENDPOINT_TYPE(attr) != USB_ENDPOINT_TYPE_BULK)
			continue;
		count++;
		if (alt->ep[i].ep_desc.bEndpointAddress & 0x80U)
			*has_in = 1;
		else
			*has_out = 1;
	}
	return count;
}

/*
 * Build the Linux view over the CherryUSB interface: one usb_device, one
 * usb_interface with a single altsetting whose endpoint array mirrors the
 * hubport's descriptors.  Everything the driver later resolves through
 * pipes (bulk IN/OUT, int IN) hangs off this.
 */
static struct usb_device *
usbh_rtw88_build_device(struct usbh_hubport *hport, uint8_t intf_idx,
    struct usb_interface **intf_out)
{
	struct usbh_interface_altsetting *alt =
	    &hport->config.intf[intf_idx].altsetting[0];
	struct usb_device *dev;
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	struct usb_host_endpoint *eps;
	unsigned int n_ep = 0;
	unsigned int i;

	/* count the materialized endpoints first */
	for (i = 0; i < CONFIG_USBHOST_MAX_ENDPOINTS; i++) {
		if (alt->ep[i].ep_desc.bLength != 0)
			n_ep++;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	intf = kzalloc(sizeof(*intf), GFP_KERNEL);
	hostif = kzalloc(sizeof(*hostif), GFP_KERNEL);
	eps = kzalloc(sizeof(*eps) * (n_ep != 0 ? n_ep : 1), GFP_KERNEL);
	if (dev == NULL || intf == NULL || hostif == NULL || eps == NULL)
		goto fail;

	hostif->endpoint = eps;
	hostif->num_endpoints = (int)n_ep;
	memcpy(&hostif->desc, &alt->intf_desc, sizeof(hostif->desc));

	n_ep = 0;
	for (i = 0; i < CONFIG_USBHOST_MAX_ENDPOINTS; i++) {
		if (alt->ep[i].ep_desc.bLength == 0)
			continue;
		memcpy(&eps[n_ep].desc, &alt->ep[i].ep_desc,
		    sizeof(eps[n_ep].desc));
		eps[n_ep].hcpriv = &alt->ep[i].ep_desc;
		eps[n_ep].enabled = true;
		n_ep++;
	}

	dev->devnum = hport->dev_addr;
	dev->speed = hport->speed;
	dev->_vendor = hport->device_desc.idVendor;
	dev->_product = hport->device_desc.idProduct;

	intf->altsetting = hostif;
	intf->cur_altsetting = hostif;
	intf->num_altsetting = 1;
	intf->dev.name = "rtw88u";	/* one dongle per image today */

	lkpi_usb_device_bind(dev, intf, hport);
	*intf_out = intf;
	return dev;

fail:
	kfree(dev);
	kfree(intf);
	kfree(hostif);
	kfree(eps);
	return NULL;
}

static void
usbh_rtw88_free_device(struct usb_device *dev)
{
	struct usb_interface *intf;

	if (dev == NULL)
		return;
	intf = (struct usb_interface *)dev->_intf;
	if (intf != NULL) {
		if (intf->cur_altsetting != NULL) {
			kfree(intf->cur_altsetting->endpoint);
			kfree(intf->cur_altsetting);
		}
		kfree(intf);
	}
	kfree(dev);
}

static int
usbh_rtw88_connect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usb_device *dev;
	struct usb_interface *intfp = NULL;
	int has_in = 0, has_out = 0;
	int ret;

	if (!hport->connected) {
		/* the post-eject personality flip bounces the device: one
		 * connect fires against the hubport whose teardown is
		 * already in flight - probing it only ever fails -ENODEV
		 * and doubles the claim noise (M3) */
		return -USB_ERR_NODEV;
	}
	if (!usbh_rtw88_id_match(hport->device_desc.idVendor,
	    hport->device_desc.idProduct))
		return -USB_ERR_NODEV;
	if (usbh_rtw88_bulk_ep_count(hport, intf, &has_in, &has_out) <
	    USBH_RTW88_MIN_BULK_EPS || !has_in || !has_out) {
		/* some other interface of a composite dongle: not ours */
		return -USB_ERR_NODEV;
	}

	lkpi_printf("usbh: rtw88u claiming %04x:%04x, settle %d ms\n",
	    hport->device_desc.idVendor, hport->device_desc.idProduct,
	    (int)RTW88U_PROBE_SETTLE_MS);
	lkpi_msleep(RTW88U_PROBE_SETTLE_MS);

	dev = usbh_rtw88_build_device(hport, intf, &intfp);
	if (dev == NULL)
		return -USB_ERR_NOMEM;

	/* Remember the Linux view on the interface: the disconnect hook runs
	 * before the hubport is torn down and reads it back from here. */
	hport->config.intf[intf].priv = dev;

	ret = lkpi_usb_probe_drivers(intfp);
	if (ret != 0) {
		lkpi_printf("usbh: usb_driver probe failed (ret %d) for "
		    "%04x:%04x\n", ret, dev->_vendor, dev->_product);
		hport->config.intf[intf].priv = NULL;
		usbh_rtw88_free_device(dev);
		return -USB_ERR_INVAL;
	}
	return 0;
}

static void
usbh_rtw88_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usb_device *dev = hport->config.intf[intf].priv;

	if (dev == NULL)
		return;
	hport->config.intf[intf].priv = NULL;
	lkpi_usb_disconnect_drivers((struct usb_interface *)dev->_intf);
	usbh_rtw88_free_device(dev);
}

static const struct usbh_class_driver usbh_rtw88_class_driver = {
	.driver_name = "rtw88lkpi",
	.driver_desc = "rtw88 Linux drivers on the linuxkpi usb layer",
	.connect = usbh_rtw88_connect,
	.disconnect = usbh_rtw88_disconnect,
};

/* CLASS_INFO_DEFINE puts this in the .usbh_class_info section, which the
 * linker script KEEPs and usbh_initialize() brackets - without the KEEP
 * the linker drops the entry and no dongle is claimed. */
CLASS_INFO_DEFINE const struct usbh_class_info usbh_rtw88_class_info = {
	.match_flags = USB_CLASS_MATCH_VID_PID,
	.id_table = usbh_rtw88_ids,
	.bInterfaceClass = 0xff,
	.bInterfaceSubClass = 0xff,
	.bInterfaceProtocol = 0xff,
	.class_driver = &usbh_rtw88_class_driver,
};
