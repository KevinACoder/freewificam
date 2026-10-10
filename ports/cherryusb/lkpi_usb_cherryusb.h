/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    lkpi_usb_cherryusb.h
 * @brief   What a reader of the CherryUSB backend can see without reaching into it:
 *          the transfer stage's geometry and the backend's named counters.
 *
 * The seam contract itself is adapter-neutral (lkpi_usb_backend.h). This header
 * carries the two things that are specific to this adapter and that a bring-up
 * needs to read: the stage geometry (which decides whether a given transfer can
 * be staged at all) and the counters that separate "the controller refused" from
 * "the transfer never left". The board suite prints them; this adapter's own host
 * test asserts the stage boundary against them.
 *
 * One staged transfer's bytes. The ceiling must cover the widest single transfer
 * the image's driver moves, and that is a per-driver number: the 8188eu path's
 * widest is a full frame (2352 + the RX descriptor, rounded up), while rtw89's
 * receive path submits a 20480-byte bulk-in transfer (its RTW89_USB_RECVBUF_SZ).
 * An image whose driver asks for more than this must raise it, or every such
 * transfer is refused for good (the refusal names itself once through
 * chu_stage_overflow_report). A larger request is never served from the heap.
 * The default is the 8188 line's value.
 */
#ifndef _LKPI_USB_CHERRYUSB_H
#define _LKPI_USB_CHERRYUSB_H

#ifndef CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE
#define CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE	4096
#endif

/* Staging slots: the engine's concurrency, one transfer each. */
#ifndef CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS
#define CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS	16
#endif

/* Transfer slots: one per engine transfer in flight plus the synchronous lane's
 * concurrency. */
#ifndef CONFIG_LKPI_USB_CHERRYUSB_SLOTS
#define CONFIG_LKPI_USB_CHERRYUSB_SLOTS	16
#endif

struct lkpi_usb_chusb_stats {
	unsigned int	submits;		/* submits handed to the stack */
	unsigned int	kills;			/* kill calls */
	unsigned int	control_transfers;	/* synchronous control transfers */
	unsigned int	stage_staged;		/* transfers that took a stage slot */
	unsigned int	stage_passthrough;	/* already inside the window */
	unsigned int	stage_overflow;		/* request larger than a slot: refused */
	unsigned int	stage_exhausted;	/* no free stage slot: refused */
	unsigned int	slots_exhausted;	/* no free transfer slot: refused */
	unsigned int	completed;		/* completions delivered to the seam */
	unsigned int	complete_dropped;	/* a second completion for one transfer */
	unsigned int	kill_translated;	/* HCD shutdown rewritten as cancelled */
	unsigned int	zero_packet_ignored;	/* the flag the stack has no equivalent for */
	unsigned int	refused;		/* malformed requests refused */
	unsigned int	stage_size;		/* the compiled stage slot size */
	unsigned int	stage_slots;		/* the compiled stage slot count */
	/* the first four bytes of the last control IN transfer's stage, taken
	 * BEFORE the copy-back to the caller: the device-facing value.  Read
	 * together with the caller's own view of the same transfer, it separates
	 * "the device returned this" from "the copy back lost it" - a register
	 * read that returns a constant is otherwise indistinguishable from one
	 * whose answer never left the DMA buffer. */
	unsigned int	last_in_stage_word;
	unsigned int	last_in_stage_len;
};

void lkpi_usb_cherryusb_stats(struct lkpi_usb_chusb_stats *out);

#endif /* _LKPI_USB_CHERRYUSB_H */
