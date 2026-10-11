/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * lkpi_usb_backend.h - the backend seam of the linuxkpi USB layer.
 *
 * The urb engine (linuxkpi_usb.c) speaks Linux semantics to its callers and
 * reaches the controller through this narrow interface only: submit, kill and
 * a synchronous control transfer, plus one opaque identity per endpoint. The
 * seam carries no register, ring or DMA vocabulary - a backend is anything
 * that can move a transfer and report its completion.
 *
 * Implementations:
 *   - the host mock suite plugs in a synthetic HCD + scripted device model
 *     (the Gate 1 shape: transfer level, no registers);
 *   - the target build maps the same three calls onto the production host
 *     controller driver.
 *
 * Completion contract (the engine's deferred model): an asynchronous submit
 * (timeout == 0, complete != NULL) is accepted and the backend calls
 * complete(arg, nbytes) exactly once from a context of its own, after
 * filling actual_length and errorcode. A synchronous submit (timeout != 0)
 * blocks until the transfer finished and returns 0 or a negative error code;
 * it never calls complete. Kill cancels a submitted transfer, delivers that
 * one completion itself and returns 0 when it owned the transfer.
 *
 * Error codes are this seam's own (values kept identical to the controller
 * library numbering the engine was ported against, so every mapping in the
 * engine keeps its meaning; LKPI_USB_BE_ERR_SHORT is the seam's addition for
 * the control-transfer short read the Linux error contract reports as
 * -EREMOTEIO). Error values travel as the negative of the code.
 *
 * @design ports & adapters: the engine is the adapter-neutral core, a backend
 * is the adapter; the seam is deliberately narrow (three operations, one
 * identity) so a second backend cannot fork the engine. Shape reference:
 * FreeBSD sys/compat/linuxkpi/common/src/linux_usb.c (the lkpi_usb_transfer
 * hand-off, xfer setup/teardown at :380-470) ported through the in-tree
 * engine's usbh call surface (linuxkpi_usb.c).
 */

#ifndef LKPI_USB_BACKEND_H
#define LKPI_USB_BACKEND_H

#include "linuxkpi_core.h"

/* transfer flags */
#define	LKPI_USB_BE_URB_ZERO_PACKET	(1u << 0)

/* error codes (negative of these travel in errorcode and return values) */
#define	LKPI_USB_BE_ERR_NOMEM		1
#define	LKPI_USB_BE_ERR_INVAL		2
#define	LKPI_USB_BE_ERR_NODEV		3
#define	LKPI_USB_BE_ERR_NOTCONN		4
#define	LKPI_USB_BE_ERR_NOTSUPP		5
#define	LKPI_USB_BE_ERR_BUSY		6
#define	LKPI_USB_BE_ERR_RANGE		7
#define	LKPI_USB_BE_ERR_STALL		8
#define	LKPI_USB_BE_ERR_BABBLE		9
#define	LKPI_USB_BE_ERR_NAK		10
#define	LKPI_USB_BE_ERR_DT		11
#define	LKPI_USB_BE_ERR_IO		12
#define	LKPI_USB_BE_ERR_SHUTDOWN	13
#define	LKPI_USB_BE_ERR_TIMEOUT		14
/* seam addition: a control transfer moved fewer bytes than its wLength */
#define	LKPI_USB_BE_ERR_SHORT		15
/* seam addition: the transfer was cancelled by kill */
#define	LKPI_USB_BE_ERR_CANCELLED	16

/* the Linux errno the short data stage maps to (upstream numbering; the core
 * header's errno list predates this consumer) */
#ifndef EREMOTEIO
#define	EREMOTEIO	121
#endif

/* the 8-byte standard control request line (USB 2.0 chapter 9.3) */
struct lkpi_usb_setup_packet {
	u8	bmRequestType;
	u8	bRequest;
	u16	wValue;
	u16	wIndex;
	u16	wLength;
} __packed;

struct lkpi_usb_be_urb;
struct lkpi_usb_be_hubport;

/* one addressable endpoint of a backend; the engine keeps the pointer as the
 * endpoint identity and never dereferences hcpriv itself */
struct lkpi_usb_be_ep {
	void	*hcpriv;
	u8	data_toggle;	/* next DATA PID; endpoint-owned, initially DATA0 */
};

/* one root port of a backend; ep0 is the control endpoint the engine stages
 * control transfers on */
struct lkpi_usb_be_hubport {
	void			*hcpriv;
	struct lkpi_usb_be_ep	ep0;
	unsigned int		port;
};

/* the backend transfer object; one per engine endpoint for asynchronous
 * traffic and a small static pool for the synchronous lane (the engine owns
 * that storage - an urb never embeds it) */
struct lkpi_usb_be_urb {
	struct lkpi_usb_be_hubport	*hport;
	struct lkpi_usb_be_ep		*ep;
	void				*setup;		/* 8-byte setup line, control only */
	void				*transfer_buffer;
	u32				transfer_buffer_length;
	u32				actual_length;	/* backend fills on completion */
	u32				timeout;	/* 0 = async; ms = sync */
	u32				transfer_flags;
	u32				interval;	/* interrupt endpoints */
	int				errorcode;	/* 0 or negative of the codes above */
	void				(*complete)(void *arg, int nbytes);
	void				*arg;
	void				*hcpriv;	/* backend-private per transfer */
};

struct lkpi_usb_be_ops {
	int	(*submit_urb)(void *ctx, struct lkpi_usb_be_urb *urb);
	int	(*kill_urb)(void *ctx, struct lkpi_usb_be_urb *urb);
	int	(*control_transfer)(void *ctx, struct lkpi_usb_be_hubport *port,
		    const struct lkpi_usb_setup_packet *setup, void *buffer);
};

/* install the backend (the host mock harness and the target bring-up call
 * this once before the first submit; without a backend every transfer is
 * refused with -LKPI_USB_BE_ERR_NODEV) */
void	lkpi_usb_be_register(const struct lkpi_usb_be_ops *ops, void *ctx);

/* the engine's three backend entries */
int	lkpi_usb_be_submit_urb(struct lkpi_usb_be_urb *urb);
int	lkpi_usb_be_kill_urb(struct lkpi_usb_be_urb *urb);
int	lkpi_usb_be_control_transfer(struct lkpi_usb_be_hubport *port,
	    const struct lkpi_usb_setup_packet *setup, void *buffer);

#endif /* LKPI_USB_BACKEND_H */
