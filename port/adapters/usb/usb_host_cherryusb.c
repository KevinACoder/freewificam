/*
 * @file
 * @brief USB host abstraction backend over the CherryUSB host stack.
 *
 * The second implementation of include/usb_host.h - same calls, the other
 * stack.  Nothing above this file changes when the backend switches: that is
 * the point of the abstraction (D-C1③, compile-time backend selection; the
 * two backends are never linked into one image because both would define
 * Driver_USB_HOST_* and the platform bring-ups would collide).
 *
 * WHAT THIS BACKEND KNOWS THAT THE INTERFACE DOES NOT
 *
 *  - The device table.  CherryUSB has no flat device array: devices live in
 *    the hub tree (bus->hcd.roothub.child[] and, behind the panel's CH334P,
 *    hub->child[] of each enumerated hub).  The interface's flat index is
 *    this file's invention: a depth-first walk of that tree, roothub first,
 *    numbering every connected port.  The walk is stable as long as nothing
 *    attaches or detaches between calls (the interface documents snapshot
 *    semantics).
 *
 *  - The transfer path.  usbh_control_transfer() drives the device's ep0
 *    urb and copies in/out of the caller's buffer; it owns the per-device
 *    mutex, so it is the same "task context only" primitive the NetBSD
 *    backend's usbd_do_request_len() is, and it must not be called from a
 *    completion callback.
 *
 *  - The timeout.  UsbHost's ControlTransfer carries a timeout_ms, but
 *    CherryUSB's control path has one timeout for all control traffic
 *    (CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT, 500 ms in the shadow
 *    usb_config.h).  A caller asking for the interface default gets that
 *    value; anything else cannot be honoured and is reported as such rather
 *    than silently ignored.
 *
 *  - The error mapping.  CherryUSB's -USB_ERR_* space maps onto the
 *    interface's six conditions; "it returned nonzero" is not actionable
 *    during bring-up.
 *
 * @date 27.09.2026
 * @author zhugengyu
 */

#include <stdint.h>
#include <string.h>

#include "usbh_core.h"
#include "usbh_hub.h"
#include "usbh_platform.h"

#include "usb_host.h"

/* ------------------------------------------------------------------
 * device table: depth-first walk of the hub tree
 */

#define USBH_TREE_MAX_DEPTH	(CONFIG_USBHOST_MAX_EXTHUBS + 2U)

/* Finds the index-th connected port in the tree rooted at hub, descending
 * depth first.  Returns NULL when the index is past the last device. */
static struct usbh_hubport *usbh_dev_at(struct usbh_hub *hub, uint32_t index,
					uint32_t *seen)
{
	uint8_t port;

	for (port = 0; port < hub->nports; port++) {
		struct usbh_hubport *hport = &hub->child[port];

		if (!hport->connected) {
			continue;
		}
		if (*seen == index) {
			return hport;
		}
		(*seen)++;
		if (hport->self != NULL) {
			struct usbh_hubport *found =
				usbh_dev_at(hport->self, index, seen);

			if (found != NULL) {
				return found;
			}
		}
	}
	return NULL;
}

static struct usbh_hubport *usbh_dev_find(uint32_t index)
{
	uint32_t seen = 0;
	uint8_t busid;

	for (busid = 0U; busid < CONFIG_USBHOST_MAX_BUS; busid++) {
		struct usbh_bus *bus = &g_usbhost_bus[busid];
		struct usbh_hubport *hport;

		if (bus->hub_thread == NULL) {
			continue;
		}
		hport = usbh_dev_at(&bus->hcd.roothub, index, &seen);
		if (hport != NULL) {
			return hport;
		}
	}
	return NULL;
}

static void usbh_dev_count(struct usbh_hub *hub, uint32_t *count)
{
	uint8_t port;

	for (port = 0; port < hub->nports; port++) {
		struct usbh_hubport *hport = &hub->child[port];

		if (!hport->connected) {
			continue;
		}
		(*count)++;
		if (hport->self != NULL) {
			usbh_dev_count(hport->self, count);
		}
	}
}

static int32_t usbh_cherryusb_GetDeviceCount(uint32_t *count)
{
	uint32_t seen = 0;
	uint8_t busid;

	if (count == NULL) {
		return USB_HOST_ERROR_PARAMETER;
	}
	for (busid = 0U; busid < CONFIG_USBHOST_MAX_BUS; busid++) {
		struct usbh_bus *bus = &g_usbhost_bus[busid];

		if (bus->hub_thread == NULL) {
			continue;
		}
		usbh_dev_count(&bus->hcd.roothub, &seen);
	}
	*count = seen;
	return USB_HOST_OK;
}

/* CherryUSB's speed codes are 1..6 (USB_SPEED_*); the interface re-numbers
 * them from 0.  Keeping them separate on purpose: a caller's switch should
 * not depend on a vendored header's constants. */
static uint8_t usbh_speed_map(uint8_t speed)
{
	switch (speed) {
	case USB_SPEED_LOW:
		return USB_HOST_SPEED_LOW;
	case USB_SPEED_FULL:
		return USB_HOST_SPEED_FULL;
	case USB_SPEED_HIGH:
		return USB_HOST_SPEED_HIGH;
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		return USB_HOST_SPEED_SUPER;
	default:
		return USB_HOST_SPEED_FULL;
	}
}

static int32_t
usbh_cherryusb_GetDeviceInfo(uint32_t index, USB_HOST_DEVICE *info)
{
	struct usbh_hubport *hport;
	uint8_t busid;

	if (info == NULL) {
		return USB_HOST_ERROR_PARAMETER;
	}
	hport = usbh_dev_find(index);
	if (hport == NULL) {
		return USB_HOST_ERROR_PARAMETER;
	}

	memset(info, 0, sizeof(*info));
	info->index = (uint8_t)index;
	info->address = hport->dev_addr;
	info->speed = usbh_speed_map(hport->speed);
	info->device_class = hport->device_desc.bDeviceClass;
	info->vendor = hport->device_desc.idVendor;
	info->product = hport->device_desc.idProduct;
	info->bcd_device = hport->device_desc.bcdDevice;
	info->num_configurations = hport->device_desc.bNumConfigurations;
	/* CherryUSB configures a device as part of enumeration; a port whose
	 * descriptor was read but whose SET_CONFIGURATION has not landed yet
	 * is reported unconfigured. */
	info->configured = (hport->config.config_desc.bNumInterfaces != 0);
	return USB_HOST_OK;
}

/* ------------------------------------------------------------------
 * control transfers
 */

/* DMA staging.  The interface's CONTRACT says the caller's buffer is ordinary
 * memory (cacheable, possibly stack) and the backend owns the DMA-visible
 * staging; CherryUSB's EHCI path requires the opposite - it asserts that both
 * the setup packet and the transfer buffer are 64-byte aligned and DMAs
 * straight out of them (usb_hc_ehci.c:1245 under CONFIG_USB_DCACHE_ENABLE).
 * A caller buffer straight from the shell's argv or a stack frame trips the
 * assert (found on the board: `usbreq desc 3 18` under feat/cherryusb_ehci).
 * So this backend keeps one aligned staging block, sized like
 * CONFIG_USBHOST_REQUEST_BUFFER_LEN, and copies in/out around the transfer -
 * the same service usbdi.c performs for the NetBSD backend, done here because
 * this stack leaves it to the caller.
 *
 * One block, no lock: the interface is task-context-only and the callers in
 * tree (the `usbreq` command, a driver's probe path) are serialized by their
 * own context. */
#define USBH_BOUNCE_LEN	CONFIG_USBHOST_REQUEST_BUFFER_LEN

/* Two separate objects, each aligned on its own: the setup packet is 8 bytes,
 * so a payload placed after it inside one struct would start at offset 8 and
 * still fail the 64-byte check (the first board round did exactly that). */
static struct usb_setup_packet usbh_ctrl_setup
	__attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static uint8_t usbh_ctrl_data[USBH_BOUNCE_LEN]
	__attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));

static int32_t usbh_status_map(int ret)
{
	switch (ret) {
	case 0:
		return USB_HOST_OK;
	case -USB_ERR_STALL:
		return USB_HOST_ERROR_STALL;
	case -USB_ERR_TIMEOUT:
		return USB_HOST_ERROR_TIMEOUT;
	case -USB_ERR_NODEV:
	case -USB_ERR_INVAL:
		return USB_HOST_ERROR_PARAMETER;
	case -USB_ERR_NOMEM:
		return USB_HOST_ERROR;
	default:
		/* BUSY / IO / BABBLE / the HCD-internal codes: the bus
		 * reported something the USB spec has no name for. */
		return USB_HOST_ERROR_IO;
	}
}

static int32_t
usbh_cherryusb_ControlTransfer(uint32_t index, const USB_HOST_REQUEST *req,
    void *data, uint32_t *actlen, uint32_t timeout_ms)
{
	struct usbh_hubport *hport;
	struct usb_setup_packet *setup = &usbh_ctrl_setup;
	uint8_t *stage = NULL;
	int in;
	int ret;

	if (req == NULL || (req->wLength != 0 && data == NULL)) {
		return USB_HOST_ERROR_PARAMETER;
	}
	if (actlen != NULL) {
		*actlen = 0;
	}
	if (req->wLength > USBH_BOUNCE_LEN) {
		return USB_HOST_ERROR_PARAMETER;
	}
	/* CherryUSB's control path has its own (compile-time) timeout; the
	 * only value this backend can honestly honour is "the default". */
	if (timeout_ms != 0 && timeout_ms != USB_HOST_DEFAULT_TIMEOUT_MS) {
		return USB_HOST_ERROR_UNSUPPORTED;
	}
	hport = usbh_dev_find(index);
	if (hport == NULL) {
		return USB_HOST_ERROR_PARAMETER;
	}

	/* the interface's field set IS the setup packet, field for field */
	setup->bmRequestType = req->bmRequestType;
	setup->bRequest = req->bRequest;
	setup->wValue = req->wValue;
	setup->wIndex = req->wIndex;
	setup->wLength = req->wLength;

	in = (req->bmRequestType & 0x80U) != 0U;
	stage = (req->wLength != 0) ? usbh_ctrl_data : NULL;
	if (stage != NULL && !in) {
		memcpy(stage, data, req->wLength);
	}

	ret = usbh_control_transfer(hport, setup, stage);

	if (ret < 0) {
		return usbh_status_map(ret);
	}
	if (stage != NULL && in && ret > 0) {
		memcpy(data, stage, (uint32_t)ret);
	}
	if (actlen != NULL) {
		*actlen = (uint32_t)ret;
	}
	return ((uint32_t)ret < (uint32_t)req->wLength) ?
	       USB_HOST_ERROR_SHORT : USB_HOST_OK;
}

/* ------------------------------------------------------------------
 * controller-level
 */

static int32_t usbh_cherryusb_Initialize(USB_HOST_SignalEvent_t cb_event)
{
	(void)cb_event;
	return (usbh_platform_start() == 0) ? ARM_DRIVER_OK : ARM_DRIVER_ERROR;
}

static int32_t usbh_cherryusb_Uninitialize(void)
{
	/* teardown is the platform's business (and this image never detaches
	 * a controller); refuse rather than pretend. */
	return ARM_DRIVER_ERROR_UNSUPPORTED;
}

static int32_t usbh_cherryusb_PowerControl(uint32_t index, uint32_t state)
{
	/* VBUS is a fixed GPIO on this board (usb_board.h); the socket group
	 * is always powered once the platform bring-up ran. Accept the call so
	 * a portable caller's power sequence does not have to special-case the
	 * board, but report the truth for an out-of-range index. */
	(void)state;
	if (usbh_dev_find(index) == NULL) {
		return USB_HOST_ERROR_PARAMETER;
	}
	return ARM_DRIVER_OK;
}

static USB_HOST_CAPABILITIES usbh_cherryusb_GetCapabilities(void)
{
	USB_HOST_CAPABILITIES caps;

	memset(&caps, 0, sizeof(caps));
	caps.max_devices = USB_HOST_MAX_DEVICES;
	caps.high_speed = 1;	/* the EHCI roots */
	caps.super_speed = 0;	/* no xHCI in this image (its own feat line) */
	caps.isochronous = 0;	/* not in this interface; see usb_host.h */
	return caps;
}

static ARM_DRIVER_VERSION usbh_cherryusb_GetVersion(void)
{
	ARM_DRIVER_VERSION v = { .api = USB_HOST_API_VERSION, .drv = 0x0100 };

	return v;
}

ARM_DRIVER_USB_HOST Driver_USB_HOST_CherryUSB = {
	.GetVersion = usbh_cherryusb_GetVersion,
	.GetCapabilities = usbh_cherryusb_GetCapabilities,
	.Initialize = usbh_cherryusb_Initialize,
	.Uninitialize = usbh_cherryusb_Uninitialize,
	.PowerControl = usbh_cherryusb_PowerControl,
	.GetDeviceCount = usbh_cherryusb_GetDeviceCount,
	.GetDeviceInfo = usbh_cherryusb_GetDeviceInfo,
	.ControlTransfer = usbh_cherryusb_ControlTransfer,
};
