/* SPDX-License-Identifier: BSD-3-Clause */
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
	unsigned int	zero_packet_ignored;	/* ZLP asked for but not required (IN, len 0, aligned to nothing) */
	unsigned int	zlp_added;		/* OUTs that got the maxpacket-boundary delimiter */
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
	/* bulk-IN (receive) completions: the diagnostic that separates "the
	 * controller stopped completing the receive URBs" from "the driver's
	 * workqueue stopped consuming them" when a receive count freezes. */
	unsigned int	rx_completes;
	unsigned int	rx_errors;
	unsigned int	rx_last_len;
	/* The split a stalled receive needs: was the completion short (one MPDU
	 * per transfer, the shape the driver's 2328-byte URB expects) or did it
	 * fill the whole buffer (an aggregate bigger than the URB, or a transfer
	 * the controller cut at the buffer's end)?  Filled only under
	 * CONFIG_LKPI_USB_OBS, so a clean image carries no extra work. */
	unsigned int	rx_full;		/* completions that filled the URB */
	unsigned int	rx_max_len;		/* largest bulk-IN completion */
	unsigned int	rx_len_hist[6];		/* <256 <512 <1024 <1600 <2328 >=2328 */
	int		rx_last_err;		/* last non-zero completion code */
	/* the endpoint probe's own ledger: how often it ran and how each of its
	 * four transfers came out.  A stall investigation reads these instead
	 * of re-deriving them from the console lines, which can be truncated. */
	unsigned int	probe_runs;
	unsigned int	probe_desc_ok;
	unsigned int	probe_desc_fail;
	unsigned int	probe_other_ok;
	unsigned int	probe_other_fail;
};

void lkpi_usb_cherryusb_stats(struct lkpi_usb_chusb_stats *out);

/* The bytes of one aggregated bulk-OUT transfer, taken at completion from the
 * stage (the buffer the controller actually read).  Captured once, for the
 * first transfer between the two sizes below, because the question is what a
 * multi-frame buffer looks like on the wire and the console cannot be asked
 * during a full-rate burst: the ledger path reads it later, at an anchor. */
struct lkpi_usb_agg_snap {
	unsigned int	valid;
	unsigned int	len;		/* transfer buffer length */
	unsigned int	act;		/* actual length */
	int		err;
	unsigned int	used;		/* bytes stored in buf */
	uint8_t		buf[576];
};

void lkpi_usb_cherryusb_agg_snap(struct lkpi_usb_agg_snap *out);

/*
 * "Is the device still there on the control pipe?" - asked after a bulk data
 * plane has stopped, and the one question that separates the three candidates
 * that look identical from the completion counters alone.
 *
 * A GET_DESCRIPTOR(DEVICE) to the device that stopped proves the controller,
 * its schedule and the whole host completion path still work, so whatever is
 * stuck is that device's bulk endpoint.  A device that answers its bulk
 * traffic being the only one on the bus proves nothing either way; the probe
 * therefore also asks ONE other device on the same controller, which is what
 * tells "this device's USB died" from "this controller stopped".  When there
 * is no other device, the field says `-2` (unavailable) rather than inventing
 * a target - the two cases then cannot be told apart and the verdict says so.
 *
 * Cost, and the caller must know it: the stack's control transfer retries
 * three times at a 500 ms timeout each, so a dead device makes one probe take
 * up to ~1.5 s.  It runs once per boot, from the stall watchdog.
 */
#define LKPI_USB_PROBE_UNAVAILABLE	(-2)	/* nothing to probe / not attempted */

struct lkpi_usb_chusb_probe {
	int		present;	/* 1 = the stalled device is still enumerated */
	uint8_t		busid;
	uint8_t		dev_addr;
	int		desc_rc;	/* GET_DESCRIPTOR(DEVICE): 0 ok, else stack code */
	unsigned int	desc_len;	/* bytes the stack reported */
	unsigned int	desc_ms;	/* wall time of that transfer */
	uint8_t		desc[18];	/* raw bytes: idVendor/idProduct/bcdUSB */
	int		status_rc;	/* GET_STATUS(device) */
	unsigned int	status_len;
	unsigned int	status_ms;
	unsigned int	status_word;
	int		other_count;	/* other enumerated devices on the bus */
	int		other_rc;	/* GET_DESCRIPTOR on one of them */
	unsigned int	other_len;
	unsigned int	other_ms;
	unsigned int	other_id;	/* (vendor << 16) | product on success */
	unsigned int	roothub_status;	/* the root hub's port status, if read */
	int		roothub_rc;
};

/* Fill `out` by probing the device the last transfer went to.  Returns 0 when
 * a probe ran (the result is inside `out`), -EBUSY on a second call in the
 * same boot, -ENODEV when no device has been transferred to yet. */
int lkpi_usb_cherryusb_ep0_probe(struct lkpi_usb_chusb_probe *out);

#endif /* _LKPI_USB_CHERRYUSB_H */
