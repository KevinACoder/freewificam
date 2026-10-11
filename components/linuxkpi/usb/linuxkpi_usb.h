/* SPDX-License-Identifier: BSD-3-Clause */
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

/* ------------------------------------------------------------------ */
/* the transfer ledger (a bring-up's one-line evidence)                */
/* ------------------------------------------------------------------ */

/*
 * The endpoint table's bookkeeping, aggregated and per endpoint.  This is what
 * separates "the controller refused the transfer" from "the transfer never left"
 * and from "it left and nothing came back": a receive path that submits but
 * never completes shows enqueued climbing with completions flat; one the backend
 * refuses shows parks climbing; one that never submits at all shows enqueued
 * flat.  The board suite prints it, which is how a single boot names the failing
 * leg instead of a round of guessing.
 */
struct lkpi_usb_engine_stats {
	unsigned int	eps;		/* endpoints in the table */
	unsigned int	enqueued;	/* submits accepted, summed */
	unsigned int	kicks;		/* handovers attempted, summed */
	unsigned int	completions;	/* completions delivered, summed */
	unsigned int	kills;		/* kills, summed */
	unsigned int	parks;		/* flow-control parks, summed */
	unsigned int	resumes;	/* parks cleared, summed */
	unsigned int	parked_now;	/* endpoints parked at report time */
	unsigned int	in_flight;	/* endpoints with a transfer in flight */
	unsigned int	qlen;		/* urbs queued behind the in-flight one */
	unsigned int	errors;		/* error completions, summed */
};

struct lkpi_usb_ep_stats {
	unsigned int	index;
	unsigned int	ep_addr;
	unsigned int	qlen;
	unsigned int	in_flight;
	unsigned int	parked;
	unsigned int	enqueued;
	unsigned int	kicks;
	unsigned int	completions;
	unsigned int	kills;
	unsigned int	parks;
	unsigned int	resumes;
	/* error completions (status != 0) this endpoint saw: a transfer the
	 * driver's callback may treat as a reason to retire a buffer rather
	 * than re-arm it */
	unsigned int	errors;
	/* the first six words of this endpoint's most recent successful IN
	 * completion, and its length (0 when none): for a receive endpoint those
	 * words are the descriptor the driver's dispatcher reads */
	unsigned int	last_in_words[6];
	unsigned int	last_in_len;
};

unsigned int	lkpi_usb_ep_count(void);
/* copy endpoint `index`'s bookkeeping; non-zero when the index is out of range */
int		lkpi_usb_ep_stats_get(unsigned int index, struct lkpi_usb_ep_stats *out);
void		lkpi_usb_engine_stats_get(struct lkpi_usb_engine_stats *out);

/*
 * The last few control transfers that carried data, newest first: the register
 * address the caller asked for (the vendor protocol puts it in wValue/wIndex)
 * and the first four bytes it got back.  A bring-up needs this because a
 * register read that "succeeds" and returns a constant is indistinguishable
 * from one that works, and the driver on top of it only reports the symptom
 * (a status bit that never appears).
 */
struct lkpi_usb_ctrl_ev {
	unsigned int	value;	/* wValue: the low half of the register address */
	unsigned int	index;	/* wIndex: bits 16-23 of the register address */
	unsigned int	len;	/* wLength */
	int		is_in;
	unsigned int	data;	/* the first four bytes, little endian */
};

/* copy up to `max` of the most recent events; returns how many were copied */
unsigned int	lkpi_usb_ctrl_trace(struct lkpi_usb_ctrl_ev *out, unsigned int max);
/* how many drivers the registry holds (the module-section walker's observable
 * result, and the bring-up's self-check before enumeration starts) */
unsigned int	lkpi_usb_driver_count_get(void);

#endif /* LINUXKPI_USB_H */
