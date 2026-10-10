/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linuxkpi_usb.h - the in-tree face of the linuxkpi USB layer.
 *
 * The engine's public entry points: bring-up, the synchronous-submit policy
 * switch, the driver registry helpers used at claim time, and the allocation
 * seam's counters.  The board line's diagnostics surface (capture ring,
 * endpoint ledger, wedge report) is not part of this build; a test observes
 * the seam counters and the controller model instead.
 */

#ifndef LINUXKPI_USB_H
#define LINUXKPI_USB_H

#include "linuxkpi_core.h"

/* drivers register through usb_register() (the target shells register
 * explicitly before the enumeration starts) */
void		lkpi_usb_start(void);
void		lkpi_usb_trace_set(bool on);
void		lkpi_usb_force_sync_set(bool on);

/* claim-time plumbing (called by the bring-up that claims an interface) */
struct usb_device;
struct usb_interface;
void	lkpi_usb_device_bind(struct usb_device *dev,
		    struct usb_interface *intf, void *hport);
int	lkpi_usb_probe_drivers(struct usb_interface *intf);
void	lkpi_usb_disconnect_drivers(struct usb_interface *intf);

/* ------------------------------------------------------------------ */
/* the allocation seam's evidence                                      */
/* ------------------------------------------------------------------ */

struct lkpi_usb_mem_stats {
	u32	n_urb_alloc;
	u32	n_urb_free;
	u32	n_coherent_alloc;
	u32	n_coherent_free;
	u32	alloc_fail;
	u32	double_free;	/* named and skipped */
	u32	stray_free;	/* pointer outside the pool */
	u32	uaf_write;	/* write through a released pointer */
};

/* the coherent buffer pair the Linux face declares (include/linux/usb.h
 * routes its inline helpers here so the seam counts them like urbs) */
void	*lkpi_usb_alloc_coherent(size_t size, unsigned int mem_flags);
void	lkpi_usb_free_coherent(void *p, size_t size);

void	lkpi_usb_mem_stats_get(struct lkpi_usb_mem_stats *out);
/* the first pointer a named counter flagged (NULL when none did) */
const void *lkpi_usb_mem_first_witness(void);
/* 1 when the pointer is in the released ring (or carries the id poison) */
int	lkpi_usb_urb_is_freed(const struct urb *urb);
/* record a use-after-free write attempt on a released object */
void	lkpi_usb_uaf_hit(const void *p);
/* DMA staging hook counter (passthrough in this build) */
unsigned int	lkpi_usb_stage_grant_count(void);
/* flow-control parking: a transient backend shortage (the controller's own
 * slot/stage pool momentarily exhausted) parks the endpoint instead of
 * surfacing an error to the driver; these counters are that mechanism's
 * evidence (a park is resumed by the retry pass that follows a resource
 * release: a delivery batch, a control transfer or a synchronous submit) */
unsigned int	lkpi_usb_park_count(void);
unsigned int	lkpi_usb_resume_count(void);
unsigned int	lkpi_usb_parked_now(void);
/* how many drivers the registry holds (the module-section walker's observable
 * result, and the bring-up's self-check before enumeration starts) */
unsigned int	lkpi_usb_driver_count_get(void);

#endif /* LINUXKPI_USB_H */
