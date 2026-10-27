/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * lkpi_usbh_class.c - the CherryUSB class hook that hands a claimed
 * interface to the Linux usb_driver registered through usb_register().
 *
 * Generalized from the prior line's usbh_rtw88_class.c (board-proven in the
 * rtw88 L1 round) per the class-hook rule "one claim hook per class" -
 * the build_device/claim plumbing below is class-agnostic, each consumer
 * instantiates its own usbh_class_info (the mass-storage hook, the CD-ROM
 * modeswitch hook and the wireless hook are this tree's three consumers).
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

#include "lkpi_usb_backend.h"	/* struct lkpi_usb_be_hubport: the engine-level port dev->_hport names */
#include "linuxkpi_core.h"
#include "linuxkpi_usb.h"
#include <linux/usb.h>

#include "lkpi_usbh_modeswitch.h"

/*
 * Guard: the mass-storage hook must never grab a wireless dongle's
 * 0bda:1a2b CD-ROM persona - the modeswitch helper owns it.  The guard hands
 * the device to that helper instead of refusing it, because the order in which
 * the class_info objects land in the section is the compiler's to choose
 * (measured: gcc emits them in reverse declaration order), so the walk may
 * reach this hook before the modeswitch hook.  Handing it over is also what
 * the device needs: the flip re-enumerates it as its network personality,
 * which the wireless hook then claims.
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
static struct usb_device *
lkpi_usbh_build_device(struct usbh_hubport *hport, uint8_t intf_idx,
    struct usb_interface **intf_out)
{
	struct usbh_interface_altsetting *alt =
	    &hport->config.intf[intf_idx].altsetting[0];
	struct usb_device *dev;
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	struct usb_host_endpoint *eps;
	struct lkpi_usb_be_hubport *be;
	struct lkpi_usb_be_ep *be_eps;
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
	/*
	 * The engine-level port object the backend contract names
	 * (lkpi_usb_backend.h:93-98): dev->_hport must be THIS type, not the
	 * raw CherryUSB hubport - the backend reads port->hcpriv as its own
	 * handle, so binding the raw struct made the backend interpret its
	 * first bytes as a pointer (measured on the board: the control
	 * transfer read hport->connected through garbage hcpriv and data-
	 * aborted). hcpriv here is the CherryUSB hubport, which is what this
	 * backend expects to find.
	 */
	/*
	 * The engine-level port and endpoint objects in one allocation: the
	 * engine stores usb_host_endpoint.hcpriv as a struct lkpi_usb_be_ep *
	 * and hands it to the backend, which reads ITS hcpriv as the backend's
	 * own handle (lkpi_usb_backend.h:86-91, 93-98). Binding the raw
	 * descriptors (the previous shape) made the backend interpret
	 * descriptor bytes as pointers - measured on the board as a data abort
	 * in the first bulk RX submission, after the control-only probe path
	 * had succeeded. The double indirection is the host mock's own shape
	 * (mock_hcd.c:653-657): ep array hangs off the port so one free
	 * releases both.
	 */
	be = kzalloc(sizeof(*be) +
	    sizeof(*be_eps) * (n_ep != 0 ? n_ep : 1), GFP_KERNEL);
	if (dev == NULL || intf == NULL || hostif == NULL || eps == NULL ||
	    be == NULL)
		goto fail;
	be_eps = (struct lkpi_usb_be_ep *)(be + 1);
	be->hcpriv = hport;
	be->port = hport->port;

	hostif->endpoint = eps;
	hostif->num_endpoints = (int)n_ep;
	memcpy(&hostif->desc, &alt->intf_desc, sizeof(hostif->desc));

	n_ep = 0;
	for (i = 0; i < CONFIG_USBHOST_MAX_ENDPOINTS; i++) {
		if (alt->ep[i].ep_desc.bLength == 0)
			continue;
		memcpy(&eps[n_ep].desc, &alt->ep[i].ep_desc,
		    sizeof(eps[n_ep].desc));
		be_eps[n_ep].hcpriv = &alt->ep[i].ep_desc;
		eps[n_ep].hcpriv = &be_eps[n_ep];
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

	lkpi_usb_device_bind(dev, intf, be);
	*intf_out = intf;
	return dev;

fail:
	kfree(dev);
	kfree(intf);
	kfree(hostif);
	kfree(eps);
	kfree(be);
	return NULL;
}

static void
lkpi_usbh_free_device(struct usb_device *dev)
{
	struct usb_interface *intf;

	if (dev == NULL)
		return;
	intf = (struct usb_interface *)dev->_intf;
	kfree(dev->_hport);		/* the engine-level port built with the device */
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
		/* Not this hook's device: hand it to the modeswitch helper,
		 * which sends the eject and does not claim it either.  The
		 * walk's first match is not this file's to order, so the eject
		 * is reached from whichever hook the walk picks. */
		return lkpi_modeswitch_connect(hport, intf);
	}

	/* The id alone does not say which root/port carried the device, and
	 * the bench has several Realtek dongles: name the controller, the hub
	 * and the port in the same line so a board log answers "which bus
	 * claimed it" without a second discovery run. */
	lkpi_printf(ULOG_INFO_LEVEL,
	    "usbh: lkpi claim attempt %04x:%04x bus=%u hub=%u port=%u intf=%u spd=%u\n",
	    hport->device_desc.idVendor, hport->device_desc.idProduct,
	    (unsigned)hport->bus->busid, (unsigned)hport->parent->index,
	    (unsigned)hport->port, (unsigned)intf, (unsigned)hport->speed);

	dev = lkpi_usbh_build_device(hport, intf, &intfp);
	if (dev == NULL)
		return -USB_ERR_NOMEM;

	/* Remember the Linux view on the interface: the disconnect hook
	 * runs before the hubport is torn down and reads it back. */
	hport->config.intf[intf].priv = dev;

	ret = lkpi_usb_probe_drivers(intfp);
	if (ret != 0) {
		lkpi_printf(ULOG_ERROR_LEVEL, "usbh: usb_driver probe failed (ret %d) for "
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

static int
lkpi_msc_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usb_device *dev = hport->config.intf[intf].priv;

	if (lkpi_claimed_hport == hport) {
		lkpi_claimed_hport = NULL;
		lkpi_claimed_intf = 0;
	}
	if (dev == NULL)
		return 0;
	hport->config.intf[intf].priv = NULL;
	lkpi_usb_disconnect_drivers((struct usb_interface *)dev->_intf);
	lkpi_usbh_free_device(dev);
	return 0;
}

/*
 * ------------------------------------------------------------------ */
/* the CD-ROM modeswitch hook: a wireless dongle's fake-disk persona   */
/*                                                                     */
/* Matched by VID/PID alone, so it catches the persona whatever its     */
/* interface class is; the mass-storage hook below matches the same     */
/* device by class, and its id guard hands the device here, so the      */
/* eject runs from whichever hook the section walk reaches first (the   */
/* order of the class_info objects is the compiler's to choose).  The   */
/* connect sends the eject and returns negative - it never claims; the  */
/* device re-enumerates as the network personality, which the wireless  */
/* hook owns.  Behaviour and message: lkpi_usbh_modeswitch.c.           */
/* ------------------------------------------------------------------ */

static const struct usbh_class_driver lkpi_modeswitch_class_driver = {
	.driver_name = "lkpimodeswitch",
	.driver_desc = "flips a wireless dongle's CD-ROM persona to its "
	    "network personality",
	.connect = lkpi_modeswitch_connect,
	.disconnect = lkpi_modeswitch_disconnect,
};

CLASS_INFO_DEFINE const struct usbh_class_info lkpi_modeswitch_class_info = {
	.match_flags = USB_CLASS_MATCH_VID_PID,
	.id_table = lkpi_modeswitch_id_table,
	.class_driver = &lkpi_modeswitch_class_driver,
};

static const struct usbh_class_driver lkpi_msc_class_driver = {
	.driver_name = "lkpimsc",
	.driver_desc = "Linux usb_driver probes on the linuxkpi usb layer",
	.connect = lkpi_msc_connect,
	.disconnect = lkpi_msc_disconnect,
};

/*
 * Class match 08/06/50 = Mass Storage / SCSI transparent command set /
 * Bulk-Only transport (the most reliable of the class triples; the driver's
 * own id_table re-checks the same triple).  No VID/PID table - the negative
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

/*
 * ------------------------------------------------------------------ */
/* the wireless hook: vendor-specific interfaces (ff/ff/ff)            */
/*                                                                     */
/* The 8188eu dongle presents one vendor-specific interface (the       */
/* driver's own id_table line asks for exactly 0bda:8179 with class    */
/* 0xff/0xff/0xff, third-party/rtl8xxxu/core.c:8022), so the class     */
/* match is the triple - no VID/PID list lives here: the real chip     */
/* matching is the driver's id_table, run by lkpi_usb_probe_drivers()  */
/* on the Linux view this hook builds.  The VID list would have to be  */
/* duplicated for every supported Realtek part (the board carries two  */
/* more of them) and would drift; the negative guard below covers the  */
/* one device this hook must never grab.                               */
/*                                                                     */
/* The guard: 0bda:1a2b is the same dongle's CD-ROM personality while  */
/* the modeswitch hook is mid-flip. Its interface is not the vendor-   */
/* specific triple, so the class match already misses it - the VID/PID */
/* backstop is kept for the same reason the mass-storage hook keeps    */
/* it: a device whose personality flips while the walk is in flight    */
/* must never be probed by a hook that had already matched it.         */
/* ------------------------------------------------------------------ */

static const struct usbh_class_driver lkpi_wlan_class_driver = {
	.driver_name = "lkpiwlan",
	.driver_desc = "Linux usb_driver probes on the linuxkpi usb layer",
	/* the shared claim path: build the Linux view and run the real id_table.
	 * One assembly point for the view, which is why the connect/disconnect
	 * pair is not written a second time here - a second path would be the
	 * second place a claim can be lost. */
	.connect = lkpi_msc_connect,
	.disconnect = lkpi_msc_disconnect,
};

CLASS_INFO_DEFINE const struct usbh_class_info lkpi_wlan_class_info = {
	.match_flags = USB_CLASS_MATCH_INTF_CLASS |
	    USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
	.bInterfaceClass = 0xff,
	.bInterfaceSubClass = 0xff,
	.bInterfaceProtocol = 0xff,
	.class_driver = &lkpi_wlan_class_driver,
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
/* linuxkpi-hub H2: the claim-less hub attachment */
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
		lkpi_printf(ULOG_INFO_LEVEL, "usbh: lkpi_hub already attached\n");
		return -1;
	}
	hport = lkpi_hub_find_hport();
	if (hport == NULL) {
		lkpi_printf(ULOG_INFO_LEVEL, "usbh: lkpi_hub attach: no hub device in the "
		    "tree\n");
		return -1;
	}
	lkpi_printf(ULOG_INFO_LEVEL, "usbh: lkpi_hub attach %04x:%04x addr %u roothub port "
	    "%u\n", hport->device_desc.idVendor,
	    hport->device_desc.idProduct, hport->dev_addr, hport->port);

	dev = lkpi_usbh_build_device(hport, 0, &intfp);
	if (dev == NULL)
		return -1;
	ret = lkpi_usb_probe_drivers(intfp);
	if (ret != 0) {
		lkpi_printf(ULOG_ERROR_LEVEL, "usbh: lkpi_hub probe failed (ret %d)\n", ret);
		lkpi_usbh_free_device(dev);
		return ret;
	}
	lkpi_hub_hport = hport;
	lkpi_hub_dev = dev;
	return 0;
}
