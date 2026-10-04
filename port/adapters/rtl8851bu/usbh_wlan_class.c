/*
 * @file   usbh_wlan_class.c
 * @brief  The CherryUSB class hook that hands the RTL8851BU WiFi persona
 *         to the linuxkpi probe chain (S5b W1, board round W2).
 *
 * Generalized from the rtl8821cu sibling (S5a W2, board-proven shape):
 * the hook does NOT implement a second match rule for the driver - it
 * checks the personality is worth building a Linux view for, builds the
 * struct usb_device/usb_interface over the hubport with the shared lkpi
 * builder, and lets the vendor's OWN id_table (usb_intf.c) decide through
 * lkpi_usb_probe_drivers() - byte-identical to the Linux match path.
 *
 * V5 shape vs the sibling: NO modeswitch entry - the RTL8851BU stick
 * enumerates directly as 0bda:b851 (W0-B board fact, runs/20261004-
 * rtl8851bu-w0b-linux/); the fake CD-ROM persona (0bda:1a2b) belongs to
 * the 8821 line only.  The composite-card BT-interface guard stays: the
 * dongle is a three-interface composite device (W0-B, spec 开放问题 5
 * material) and only the vendor-data class (0xff) personality with both
 * bulk directions is ours.
 *
 * Running probe from the CherryUSB hub thread mirrors the sibling.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <string.h>

#include "usbh_core.h"
#include "usbh_hub.h"
#include "usb_errno.h"

#include "linuxkpi_core.h"
#include "linuxkpi_usb.h"
#include <linux/usb.h>

/* drv_entry.c (the adapter seam) */
extern void rtl8851bu_adapter_bind(struct usb_interface *intf);
extern void rtl8851bu_adapter_unbind(void);

/* 0bda:b851 - the stick's own WiFi persona; direct enumeration, no
 * modeswitch (W0-B ground truth).  The vendor id_table may carry more
 * rows (8852-family shares), but only this one is in evidence on the
 * bench - the table stays fact-scoped. */
static const uint16_t usbh_wlan_ids[][2] = {
	{ 0x0bda, 0xb851 },
	{ 0, 0 },
};

#define	RTL8851BU_WIFI_INTF_CLASS	0xff
#define	RTL8851BU_MIN_BULK_EPS		2

static int usbh_wlan_id_match(uint16_t vendor, uint16_t product)
{
	unsigned int i;

	for (i = 0; usbh_wlan_ids[i][0] != 0; i++) {
		if (usbh_wlan_ids[i][0] == vendor &&
		    usbh_wlan_ids[i][1] == product)
			return 1;
	}
	return 0;
}

static int usbh_wlan_bulk_eps(const struct usbh_hubport *hport, uint8_t intf,
    int *has_in, int *has_out)
{
	const struct usbh_interface_altsetting *alt =
	    &hport->config.intf[intf].altsetting[0];
	int count = 0;
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
		if ((alt->ep[i].ep_desc.bEndpointAddress & 0x80U) != 0U)
			*has_in = 1;
		else
			*has_out = 1;
	}
	return count;
}

static int usbh_wlan_connect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usb_interface_descriptor *id =
	    &hport->config.intf[intf].altsetting[0].intf_desc;
	struct usb_device *dev;
	struct usb_interface *intfp = NULL;
	int has_in = 0, has_out = 0;
	int ret;

	if (!hport->connected)
		return -USB_ERR_NODEV;
	if (!usbh_wlan_id_match(hport->device_desc.idVendor,
	    hport->device_desc.idProduct))
		return -USB_ERR_NODEV;
	if (id->bInterfaceClass != RTL8851BU_WIFI_INTF_CLASS)
		return -USB_ERR_NODEV;	/* the BT half of the composite card */
	if (usbh_wlan_bulk_eps(hport, intf, &has_in, &has_out) <
	    RTL8851BU_MIN_BULK_EPS || !has_in || !has_out)
		return -USB_ERR_NODEV;

	lkpi_printf("usbh: wlan claiming %04x:%04x (intf %u, class ff, "
	    "%d bulk eps)\n", hport->device_desc.idVendor,
	    hport->device_desc.idProduct, intf,
	    usbh_wlan_bulk_eps(hport, intf, &has_in, &has_out));

	dev = lkpi_usbh_build_device(hport, intf, &intfp);
	if (dev == NULL)
		return -USB_ERR_NOMEM;
	hport->config.intf[intf].priv = dev;

	/* the vendor probe (rtw_drv_init) runs here, from its own id_table */
	ret = lkpi_usb_probe_drivers(intfp);
	if (ret != 0) {
		lkpi_printf("usbh: wlan probe failed (ret %d) for "
		    "%04x:%04x\n", ret, dev->_vendor, dev->_product);
		hport->config.intf[intf].priv = NULL;
		lkpi_usbh_free_device(dev);
		return -USB_ERR_INVAL;
	}
	/* bind the landed adapter for the shell/status seam */
	rtl8851bu_adapter_bind(intfp);
	return 0;
}

static void usbh_wlan_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usb_device *dev = hport->config.intf[intf].priv;

	if (dev == NULL)
		return;
	hport->config.intf[intf].priv = NULL;
	lkpi_usb_disconnect_drivers((struct usb_interface *)dev->_intf);
	rtl8851bu_adapter_unbind();
	lkpi_usbh_free_device(dev);
	lkpi_printf("usbh: wlan released %04x:%04x\n",
	    hport->device_desc.idVendor, hport->device_desc.idProduct);
}

static const struct usbh_class_driver usbh_wlan_class_driver = {
	.driver_name = "rtl8851bu-wlan",
	.driver_desc = "RTL8851BU WiFi persona onto the linuxkpi probe chain",
	.connect = usbh_wlan_connect,
	.disconnect = usbh_wlan_disconnect,
};

/* VID/PID match: the WiFi persona only (CLASS_INFO_DEFINE lands in
 * .usbh_class_info) */
CLASS_INFO_DEFINE const struct usbh_class_info usbh_wlan_class_info = {
	.match_flags = USB_CLASS_MATCH_VID_PID,
	.bInterfaceClass = 0xff,
	.bInterfaceSubClass = 0xff,
	.bInterfaceProtocol = 0xff,
	.id_table = usbh_wlan_ids,
	.class_driver = &usbh_wlan_class_driver,
};
