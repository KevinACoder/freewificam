/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * lkpi_usbh_class.c - the CherryUSB class hook that hands a claimed
 * interface to the Linux usb_driver registered through usb_register().
 *
 * Generalized from the prior line's usbh_rtw88_class.c ([EXPL], board-proven
 * in the rtw88 L1 round) per SPEC S6/linuxkpi R3: "每类设备一个认领钩子" -
 * the build_device/claim plumbing below is class-agnostic, each consumer
 * instantiates its own usbh_class_info (the MSC hook is this round's
 * consumer; the wlan hook joins with its own line).
 *
 * This is the CherryUSB-side replacement for the Linux USB core's
 * enumerate-and-match loop: CherryUSB enumerates (its own hub thread),
 * and when the interface walk matches this class_info, the hook builds the
 * Linux struct usb_device/usb_interface wrappers over the hubport and runs
 * lkpi_usb_probe_drivers() - the driver's own id_table does the real
 * matching, not a second copy of it.
 *
 * Running probe from the CherryUSB hub thread mirrors both ancestors: the
 * Linux USB core probes from its hub event thread, and [EXPL]'s hook
 * attached from this same thread.
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
 * R3 guard: the modeswitch helper owns the 0bda:1a2b CD-ROM persona (the
 * class match 08/06/50 does not hit its 08/05/50 ISO9660 personality, but
 * the VID-based backstop is cheap and spec-mandated - the MSC hook must
 * never grab a device the eject assistant is mid-flip on).
 */

/* the landed claim (set in connect, cleared in disconnect): the rawbulk
 * diagnostic (usbkpi_cmds.c) submits against the same hubport/interface
 * the linux driver did (P6-b root-cause experiments) */
static struct usbh_hubport *lkpi_claimed_hport;
static uint8_t lkpi_claimed_intf;

static int
lkpi_msc_id_refused(uint16_t vendor, uint16_t product)
{

	return vendor == 0x0bda && product == 0x1a2b;
}

/*
 * Build the Linux view over the CherryUSB interface: one usb_device, one
 * usb_interface with a single altsetting whose endpoint array mirrors the
 * hubport's descriptors.  Everything the driver later resolves through
 * pipes (bulk IN/OUT, control ep0) hangs off this.
 */
struct usb_device *
lkpi_usbh_build_device(struct usbh_hubport *hport, uint8_t intf_idx,
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
	dev->descriptor = hport->device_desc;
	dev->_vendor = hport->device_desc.idVendor;
	dev->_product = hport->device_desc.idProduct;

	intf->altsetting = hostif;
	intf->cur_altsetting = hostif;
	intf->num_altsetting = 1;
	intf->dev.name = "lkpiu";	/* one claimed device per image */

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

void
lkpi_usbh_free_device(struct usb_device *dev)
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
lkpi_msc_connect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usb_device *dev;
	struct usb_interface *intfp = NULL;
	int ret;

	if (!hport->connected) {
		/* a personality flip bounces the device: one connect fires
		 * against the hubport whose teardown is already in flight
		 * (prior-line M3 lesson) - probing it only ever fails */
		return -USB_ERR_NODEV;
	}
	if (lkpi_msc_id_refused(hport->device_desc.idVendor,
	    hport->device_desc.idProduct)) {
		return -USB_ERR_NODEV;
	}

	lkpi_printf("usbh: lkpi msc claiming %04x:%04x\n",
	    hport->device_desc.idVendor, hport->device_desc.idProduct);

	dev = lkpi_usbh_build_device(hport, intf, &intfp);
	if (dev == NULL)
		return -USB_ERR_NOMEM;

	/* Remember the Linux view on the interface: the disconnect hook
	 * runs before the hubport is torn down and reads it back. */
	hport->config.intf[intf].priv = dev;

	ret = lkpi_usb_probe_drivers(intfp);
	if (ret != 0) {
		lkpi_printf("usbh: usb_driver probe failed (ret %d) for "
		    "%04x:%04x\n", ret, dev->_vendor, dev->_product);
		hport->config.intf[intf].priv = NULL;
		lkpi_usbh_free_device(dev);
		return -USB_ERR_INVAL;
	}
	/* the landed claim: the rawbulk diagnostic submits against this
	 * same hubport/interface (P6-b root-cause experiments) */
	lkpi_claimed_hport = hport;
	lkpi_claimed_intf = intf;
	return 0;
}

static void
lkpi_msc_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usb_device *dev = hport->config.intf[intf].priv;

	if (lkpi_claimed_hport == hport) {
		lkpi_claimed_hport = NULL;
		lkpi_claimed_intf = 0;
	}
	if (dev == NULL)
		return;
	hport->config.intf[intf].priv = NULL;
	lkpi_usb_disconnect_drivers((struct usb_interface *)dev->_intf);
	lkpi_usbh_free_device(dev);
}

static const struct usbh_class_driver lkpi_msc_class_driver = {
	.driver_name = "lkpimsc",
	.driver_desc = "Linux usb_driver probes on the linuxkpi usb layer",
	.connect = lkpi_msc_connect,
	.disconnect = lkpi_msc_disconnect,
};

/*
 * Class match 08/06/50 = Mass Storage / SCSI transparent command set /
 * Bulk-Only transport (spec 开放问题 9: 类匹配最稳; the driver's own
 * id_table re-checks the same triple).  No VID/PID table - the negative
 * guard above handles the 1a2b CD-ROM persona.
 */
CLASS_INFO_DEFINE const struct usbh_class_info lkpi_msc_class_info = {
	.match_flags = USB_CLASS_MATCH_INTF_CLASS |
	    USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
	.bInterfaceClass = 0x08,
	.bInterfaceSubClass = 0x06,
	.bInterfaceProtocol = 0x50,
	.class_driver = &lkpi_msc_class_driver,
};

struct usbh_hubport *
lkpi_usbh_claimed_hport(void)
{

	return lkpi_claimed_hport;
}

uint8_t
lkpi_usbh_claimed_intf(void)
{

	return lkpi_claimed_intf;
}

/* ------------------------------------------------------------------ */
/* linuxkpi-hub H2: the claim-less hub attachment (spec §3.3-1)         */
/*                                                                      */
/* Registering a class-0x09 lkpi hook is a dead end either way (the     */
/* section walk takes the first match, usbh_core.c:97-131, and a        */
/* negative connect does not fall through to the next candidate,        */
/* :599-605), so the hub driver probes through this explicit command    */
/* path instead: build the same Linux view the class hook would, run    */
/* the real lkpi_usb_probe_drivers match chain, but never touch         */
/* intf[].priv - the hub class keeps both the claim and the interface.  */
/* The window protocol ends with a reboot, so there is no detach-side   */
/* teardown; the view lives in these statics.                           */
/* ------------------------------------------------------------------ */

static struct usbh_hubport *lkpi_hub_hport;
static struct usb_device *lkpi_hub_dev;

/* one external hub on this board: the first connected roothub-child
 * port whose device registered as a hub (hubport.self, usbh_core.h:153
 * - the hub class connect sets it) */
static struct usbh_hubport *
lkpi_hub_find_hport(void)
{
	struct usbh_hub *root;
	uint8_t busid;
	unsigned int i;

	for (busid = 0; busid < CONFIG_USBHOST_MAX_BUS; busid++) {
		root = &g_usbhost_bus[busid].hcd.roothub;
		if (!root->connected)
			continue;
		for (i = 0; i < root->nports; i++) {
			struct usbh_hubport *hport = &root->child[i];

			if (hport->connected && hport->self != NULL)
				return hport;
		}
	}
	return NULL;
}

int
lkpi_usbh_hub_attach(void)
{
	struct usbh_hubport *hport;
	struct usb_interface *intfp = NULL;
	struct usb_device *dev;
	int ret;

	if (lkpi_hub_dev != NULL) {
		lkpi_printf("usbh: lkpi_hub already attached\n");
		return -1;
	}
	hport = lkpi_hub_find_hport();
	if (hport == NULL) {
		lkpi_printf("usbh: lkpi_hub attach: no hub device in the "
		    "tree\n");
		return -1;
	}
	lkpi_printf("usbh: lkpi_hub attach %04x:%04x addr %u roothub port "
	    "%u\n", hport->device_desc.idVendor,
	    hport->device_desc.idProduct, hport->dev_addr, hport->port);

	dev = lkpi_usbh_build_device(hport, 0, &intfp);
	if (dev == NULL)
		return -1;
	ret = lkpi_usb_probe_drivers(intfp);
	if (ret != 0) {
		lkpi_printf("usbh: lkpi_hub probe failed (ret %d)\n", ret);
		lkpi_usbh_free_device(dev);
		return ret;
	}
	lkpi_hub_hport = hport;
	lkpi_hub_dev = dev;
	return 0;
}
