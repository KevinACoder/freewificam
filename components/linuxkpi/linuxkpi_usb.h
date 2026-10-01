/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linuxkpi_usb.h - the in-tree face of the linuxkpi USB layer: the
 * usbkpi capture ring (the byte-comparison exit) and the claim-time
 * plumbing the CherryUSB class hook drives.
 */

#ifndef LINUXKPI_USB_H
#define LINUXKPI_USB_H

#include "linuxkpi_core.h"

struct usbkpi_record {
	u64		seq;
	u32		tick;
	u8		dir;		/* 1 = IN, 0 = OUT */
	u8		type;		/* USB_ENDPOINT_XFER_* | 0x80 = submit */
	u8		ep;		/* endpoint | 0x80 for IN */
	u8		status;		/* 0 ok, else -errno */
	u32		len;		/* payload length */
	u32		crc;		/* payload crc32 */
	u8		head[8];	/* first payload bytes */
};

void		usbkpi_set_enabled(bool on);
/* capture filter over USB_ENDPOINT_XFER_* bits; 0 resets to all-types */
void		usbkpi_set_type_mask(unsigned int mask);
unsigned int	usbkpi_type_mask(void);
/* R-LK3 control experiment: hold bulk-IN submits back from the HC */
void		usbkpi_rxhold_set(bool on);
bool		usbkpi_rxhold(void);
/* R-LK3 wedge forensics: call from a live shell after the 60s timeout */
void		usbkpi_wedge_report(void);
unsigned int	usbkpi_get_records(struct usbkpi_record *out, unsigned int n,
				    unsigned int skip);
unsigned long long usbkpi_sequence(void);
u32		lkpi_crc32(const void *buf, size_t len);

/* drivers register through usb_register() (the RTOS shells register
 * explicitly before the enumeration starts; the Linux shell rides the
 * kernel's module_usb_driver) */
void		lkpi_usb_start(void);
void		lkpi_usb_trace_set(bool on);
void		lkpi_usb_force_sync_set(bool on);
/* the drain thread's OS handle (the wedge report reads the state/stack
 * watermark off it); opaque here - the OSAL world casts it back */
void *		lkpi_usb_drain_thread_id(void);

/* claim-time plumbing (usbh_rtw88_class.c) */
struct usb_interface;
void	lkpi_usb_device_bind(struct usb_device *dev,
	    struct usb_interface *intf, void *hport);
int	lkpi_usb_probe_drivers(struct usb_interface *intf);
void	lkpi_usb_disconnect_drivers(struct usb_interface *intf);

#endif /* LINUXKPI_USB_H */
