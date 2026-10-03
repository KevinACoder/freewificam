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
/* [W6] RX stall watchdog: counters + per-ep busy age; and the manual
 * trigger of the same kill/re-arm recovery the watchdog runs */
void		usbkpi_rx_report(void);
void		lkpi_usb_rx_kick(void);
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

/* the last claim the class hook landed (port/adapters/linuxkpi/
 * lkpi_usbh_class.c): the raw-submit diagnostic submits against the
 * same hubport/interface the linux driver rides (P6-b root-cause
 * experiments, issues/2026-10-01-xhci-bulk-td-not-fetched) */
struct usbh_hubport;
struct usbh_hubport *lkpi_usbh_claimed_hport(void);
/* the Linux-view builders (shared by the per-line class hooks; wlan joins
 * in S5a-W2) */
struct usb_device *lkpi_usbh_build_device(struct usbh_hubport *hport,
    uint8_t intf_idx, struct usb_interface **intf_out);
void lkpi_usbh_free_device(struct usb_device *dev);
uint8_t	lkpi_usbh_claimed_intf(void);

/* the stage pool bisect (usbkpi rawbulk stage): a raw-shape urb whose
 * buffers ARE the stage pool's memory - separates a poisoned pool
 * address from a guilty submit path (P6-b root cause, 2026-10-01) */
const void *lkpi_usb_stage_slot_mem(unsigned int i);

#endif /* LINUXKPI_USB_H */
