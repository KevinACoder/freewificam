/*
 * @file
 * @brief USB mode-switch hook for the RTL8821CU fake CD-ROM (cherryusb
 *        backend; the netbsd backend runs the imported umodeswitch(4)).
 *
 * The dongle boots as 0bda:1a2b, a single-interface mass-storage device
 * holding a Windows driver ("Realtek Driver Storage"); the WiFi function
 * only appears after it is ejected.  The eject is the SCSI START/STOP
 * UNIT (LoEj) command in a Bulk-Only CBW, sent to the mass-storage
 * function's bulk-OUT endpoint.  The status stage has to be read as
 * well: this device acknowledges the command but does not flip
 * personality until the 13-byte CSW comes back.
 *
 * The hook claims nothing - the device leaves the bus and comes back as
 * 0bda:c820, which is what the wlan class hook (usbh_urtwn_class.c)
 * matches for the rtw88 line.
 *
 * @date   28.09.2026
 * @author zhugengyu
 */

#include <stdint.h>
#include <stdio.h>
#include "usb_config.h"
#include <string.h>

#include "usbh_core.h"
#include "usbh_hub.h"

/* SCSI START/STOP UNIT with LoEj, in a 31-byte CBW (usb_modeswitch's
 * message for this device; the tag only has to be unique). */
static const uint8_t rtl8821cu_eject_cbw[31] = {
	0x55, 0x53, 0x42, 0x43,			/* "USBC": CBW signature */
	0x12, 0x34, 0x56, 0x78,			/* CBW tag */
	0x00, 0x00, 0x00, 0x00,			/* transfer length */
	0x00,					/* flags: out */
	0x00,					/* LUN */
	0x06,					/* CBWCB length */
	0x1b, 0x00, 0x00, 0x00, 0x02, 0x00,	/* START/STOP UNIT, LoEj */
	/* the rest of the CBWCB stays zero */
};

#define RTW88U_EJECT_ATTEMPTS	3
#define RTW88U_BULK_TIMEOUT_MS	1000

/* the bounce buffer is handed to the controller: 64-byte aligned, like
 * every other transfer buffer on this port (CONFIG_USB_DCACHE_ENABLE) */
static struct {
	struct modeswitch_xfer {
		usb_osal_sem_t done;
		int nbytes;
	} xfer;
	uint32_t urb_buf[16] __attribute__((aligned(64)));
} modeswitch;

#define MODESWITCH_BUF ((uint8_t *) modeswitch.urb_buf)

static void modeswitch_bulk_complete(void *arg, int nbytes_or_err) {
	struct modeswitch_xfer *x = arg;

	x->nbytes = nbytes_or_err;
	usb_osal_sem_give(x->done);
}

/* one bulk transfer on an endpoint, blocking until it completes */
static int modeswitch_bulk(struct usbh_hubport *hport,
	struct usb_endpoint_descriptor *ep, uint32_t len) {
	struct usbh_urb urb;
	int ret;

	/* drop a completion left over from a timed-out attempt */
	(void) usb_osal_sem_take(modeswitch.xfer.done, 0);
	modeswitch.xfer.nbytes = 0;

	memset(&urb, 0, sizeof(urb));
	usbh_bulk_urb_fill(&urb, hport, ep, MODESWITCH_BUF, len, 0,
	    modeswitch_bulk_complete, &modeswitch.xfer);

	ret = usbh_submit_urb(&urb);
	if (ret < 0) {
		return ret;
	}
	if (usb_osal_sem_take(modeswitch.xfer.done,
		RTW88U_BULK_TIMEOUT_MS) != 0) {
		usbh_kill_urb(&urb);
		usbh_console_printf("usb: RTL8821CU eject transfer timed out\n");
		return -1;
	}
	return modeswitch.xfer.nbytes;
}

static int modeswitch_eject(struct usbh_hubport *hport,
	struct usb_endpoint_descriptor *ep_out,
	struct usb_endpoint_descriptor *ep_in) {
	int attempt, n, csw;

	for (attempt = 1; attempt <= RTW88U_EJECT_ATTEMPTS; attempt++) {
		memcpy(MODESWITCH_BUF, rtl8821cu_eject_cbw,
		    sizeof(rtl8821cu_eject_cbw));
		n = modeswitch_bulk(hport, ep_out,
		    sizeof(rtl8821cu_eject_cbw));
		usbh_console_printf("usb: RTL8821CU eject attempt %d: %d byte(s) sent\n",
		    attempt, n);
		if (n < 0) {
			/* the device left the bus: the switch is under way */
			usbh_console_printf("usb: RTL8821CU left the bus, expecting 0bda:c820\n");
			return 0;
		}
		if (ep_in == NULL) {
			continue;
		}

		memset(MODESWITCH_BUF, 0, 13);
		csw = modeswitch_bulk(hport, ep_in, 13);
		usbh_console_printf("usb: RTL8821CU eject CSW: %d byte(s), signature "
		    "%02x%02x%02x%02x status %02x\n", csw,
		    MODESWITCH_BUF[0], MODESWITCH_BUF[1],
		    MODESWITCH_BUF[2], MODESWITCH_BUF[3],
		    csw >= 13 ? MODESWITCH_BUF[12] : 0);
		if (csw >= 13) {
			/* status stage read: the eject has been accepted */
			usbh_console_printf("usb: RTL8821CU eject accepted, expecting 0bda:c820\n");
			return 0;
		}
	}

	usbh_console_printf("usb: RTL8821CU eject sent; expecting 0bda:c820\n");
	return 0;
}

static int usbh_modeswitch_connect(struct usbh_hubport *hport, uint8_t intf) {
	struct usbh_interface *intf_desc = &hport->config.intf[intf];
	struct usb_interface_descriptor *id =
	    &intf_desc->altsetting[0].intf_desc;
	struct usb_endpoint_descriptor *ep_out = NULL, *ep_in = NULL;
	int i;

	if (id->bInterfaceClass != USB_DEVICE_CLASS_MASS_STORAGE) {
		return -USB_ERR_INVAL;
	}
	for (i = 0; i < id->bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep =
		    &intf_desc->altsetting[0].ep[i].ep_desc;

		if (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) !=
		    USB_ENDPOINT_TYPE_BULK) {
			continue;
		}
		if ((ep->bEndpointAddress & 0x80U) != 0U) {
			if (ep_in == NULL) {
				ep_in = ep;
			}
		} else if (ep_out == NULL) {
			ep_out = ep;
		}
	}
	if (ep_out == NULL) {
		return -USB_ERR_INVAL;
	}

	usbh_console_printf("usb: RTL8821CU fake CD-ROM at bus %u addr %u: ejecting\n",
	    hport->bus->busid, hport->dev_addr);

	modeswitch.xfer.done = usb_osal_sem_create(0);
	if (modeswitch.xfer.done == NULL) {
		return -USB_ERR_NOMEM;
	}
	(void) modeswitch_eject(hport, ep_out, ep_in);
	usb_osal_sem_delete(modeswitch.xfer.done);
	modeswitch.xfer.done = NULL;

	/* never claimed: the device comes back with the WiFi personality */
	return -USB_ERR_INVAL;
}

static const struct usbh_class_driver modeswitch_class_driver = {
	.driver_name = "rtl8821cu-modeswitch",
	.driver_desc = "RTL8821CU fake CD-ROM eject (SCSI START/STOP LoEj)",
	.connect = usbh_modeswitch_connect,
	.disconnect = NULL,
};

static const uint16_t modeswitch_id_table[2][2] = {
	{ 0x0bda, 0x1a2b },	/* RTL8821CU fake CD-ROM */
	{ 0, 0 },
};

/* CLASS_INFO_DEFINE puts this in the .usbh_class_info section, which the
 * linker script KEEPs and usbh_initialize() brackets. */
CLASS_INFO_DEFINE const struct usbh_class_info modeswitch_class_info = {
	.match_flags = USB_CLASS_MATCH_VID_PID,
	.bInterfaceClass = 0,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.bInterfaceNumber = 0,
	.id_table = modeswitch_id_table,
	.class_driver = &modeswitch_class_driver,
};
