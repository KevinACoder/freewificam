/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    lkpi_usb_cherryusb.c
 * @brief   The target backend of the linuxkpi USB seam: the three seam operations mapped
 *          onto the CherryUSB host stack, with a static non-cacheable transfer stage.
 *
 * @design ports & adapters - the linuxkpi USB engine is the adapter-neutral core
 *         (components/linuxkpi/usb/lkpi_usb_backend.h:31-36 names the pattern), and this
 *         file is the production adapter: each of the seam's three operations maps onto
 *         exactly one CherryUSB call, and nothing else moves. The transfer stage is a
 *         static, cache-line aligned, self-returning slot pool in the non-cacheable
 *         window (arch/aarch64/baremetal.ld:133-140), which is the structural answer to
 *         this project's bulk-transfer loss (an unknowable DMA buffer alignment), and it
 *         keeps every DMA buffer out of the general heap - the lesson the previous
 *         bring-up paid for. Reference for the adapter shape: the sibling in-tree
 *         adapter, ports/cherryusb/board/rk3568-itx/usb_glue.c (the stack-to-board
 *         glue), and the host mock's seam implementation
 *         (tests/host/linuxkpi-usb-test/mock_hcd.c:550-598) which this file mirrors on
 *         the production stack.
 *
 * Mapping (the written cross-check for the seam's three operations; the RUN ledger
 * carries the same table with its evidence):
 *
 *   seam                                  CherryUSB
 *   ------------------------------------  --------------------------------------------
 *   submit_urb(ctx, be_urb)               usbh_submit_urb() (core/usbh_core.c:1308)
 *   kill_urb(ctx, be_urb)                 usbh_kill_urb()   (core/usbh_core.c:1317)
 *   control_transfer(ctx, port, setup, b) usbh_control_transfer() (core/usbh_core.c:706)
 *   be_hubport.hcpriv                     struct usbh_hubport *
 *   be_ep.hcpriv                          struct usb_endpoint_descriptor *
 *   be_urb.timeout                        passed through: 0 = asynchronous (the stack
 *                                         queues and returns), != 0 = the stack blocks
 *                                         until the transfer finished
 *   be_urb.transfer_flags ZERO_PACKET     no equivalent in the stack; counted, not set
 *                                         (no consumer in this driver - the 8188eu
 *                                         path never sets URB_ZERO_PACKET)
 *   be_urb.interval                       unused here: the stack derives the interval
 *                                         from the endpoint descriptor it is handed
 *
 * Error codes: the seam's numbering is CherryUSB's (common/usb_errno.h:9-22) over the
 * shared range, so an error travels untranslated except for the one seam-added case -
 * a killed transfer, where the HCD writes its shutdown code and the seam's contract
 * says CANCELLED (the engine reports the Linux -ENOENT either way; rewriting it inside
 * the kill keeps the cancellation naming itself instead of riding the detach code).
 *
 * Completion contract: an asynchronous submit passes the seam's completion through a
 * wrapper, which copies a staged IN transfer back to the caller's buffer, fills
 * actual_length/errorcode on the seam urb and calls the seam completion exactly once
 * (a second call for the same transfer is counted and dropped). A synchronous submit
 * (timeout != 0) blocks in the stack and never calls a completion; when it returns, the
 * outcome is placed in the seam urb's errorcode and the submit reports the finished
 * transfer to the engine (return 0), which is the shape the engine's synchronous lane
 * reads (components/linuxkpi/usb/linuxkpi_usb.c:925-933).
 *
 * Compile-time shape: the board and cross-compile gate build this adapter.
 * The dedicated host adapter test also links it against a scripted CherryUSB
 * controller boundary; other mock suites replace it at the backend seam.
 */

#include <stdint.h>
#include <string.h>

/*
 * Include order and the one boundary mechanic. The seam's header (below) pulls in
 * the linuxkpi contract header, which defines a handful of the same utility
 * macros the CherryUSB headers define (MIN, MAX, ARRAY_SIZE, DIV_ROUND_CLOSEST).
 * Three of the stack's copies are #ifndef-guarded, so including the seam first
 * settles them quietly; DIV_ROUND_CLOSEST is not guarded on the stack's side, so
 * its spelling is released right before the stack's headers are included - the
 * stack must see its own definition (the stack's inline functions use it), and
 * this file uses neither.
 */
#include "components/linuxkpi/usb/lkpi_usb_backend.h"

#undef DIV_ROUND_CLOSEST

#include "usbh_core.h"
#include "usb_errno.h"

/* ------------------------------------------------------------------ */
/* geometry                                                            */
/* ------------------------------------------------------------------ */

/* Transfer slots: one per engine transfer in flight plus the synchronous lane's
 * concurrency (the engine's LKPI_EP_N endpoints and its four synchronous slots). */
#ifndef CONFIG_LKPI_USB_CHERRYUSB_SLOTS
#define CONFIG_LKPI_USB_CHERRYUSB_SLOTS	16
#endif

/* Staging slots: the same concurrency, one transfer each. */
#ifndef CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS
#define CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS	16
#endif

/* One staged transfer's bytes: the widest buffer the 8188eu path moves (a full frame
 * is 2352 + the RX descriptor, rounded up; the engine's own control stage is 2048).
 * A larger request is refused with a named counter, never served from the heap. */
#ifndef CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE
#define CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE	4096
#endif

#define LKPI_CHU_CACHE_LINE	64

_Static_assert(CONFIG_LKPI_USB_CHERRYUSB_SLOTS > 0,
    "the transfer slot pool needs at least one slot");
_Static_assert(CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS > 0,
    "the stage pool needs at least one slot");
_Static_assert(CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE % LKPI_CHU_CACHE_LINE == 0,
    "a stage slot must be a whole number of cache lines");

/* ------------------------------------------------------------------ */
/* named counters                                                      */
/* ------------------------------------------------------------------ */

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
};

static struct lkpi_usb_chusb_stats chu_stats;

void
lkpi_usb_cherryusb_stats(struct lkpi_usb_chusb_stats *out)
{

	*out = chu_stats;
}

/* ------------------------------------------------------------------ */
/* platform bindings                                                   */
/* ------------------------------------------------------------------ */

/* This is a port-layer file: it reaches the OS surface directly (the components
 * above it reach it only through the facade). The section is the same short,
 * never-sleeping mask the engine's own spinlocks take. */
#include "os/cmsis2/cmsis2.h"

static unsigned long
chu_lock(void)
{

	return (unsigned long)osCriticalEnter();
}

static void
chu_unlock(unsigned long flags)
{

	osCriticalExit((os_critical_state_t)flags);
}

/* the window's own bounds (the linker script defines them; a link without them -
 * a host tier that carries this file - leaves the region empty and every buffer
 * is staged) */
extern unsigned char __noncacheable_start[] __attribute__((weak));
extern unsigned char __noncacheable_end[] __attribute__((weak));

static int
chu_in_window(const void *buf)
{
	uintptr_t a = (uintptr_t)buf;
	uintptr_t lo = (uintptr_t)__noncacheable_start;
	uintptr_t hi = (uintptr_t)__noncacheable_end;

	if (lo == 0 || hi == 0 || hi <= lo)
		return 0;
	return a >= lo && a < hi;
}

/* ------------------------------------------------------------------ */
/* storage                                                             */
/* ------------------------------------------------------------------ */

struct lkpi_chu_slot {
	struct usbh_urb		urb;		/* handed to the stack */
	struct lkpi_usb_be_urb	*be;		/* the seam's transfer */
	unsigned char		*stage;		/* the staged buffer, NULL when
						 * the caller's buffer passed through */
	unsigned char		*user_buf;	/* the caller's buffer, staged only */
	uint32_t		user_len;
	int			is_in;
	int			in_use;
	int			delivered;	/* completion already delivered */
	int			kill_in_flight;	/* a kill owns the completion */
};

static struct lkpi_chu_slot chu_slots[CONFIG_LKPI_USB_CHERRYUSB_SLOTS];

/*
 * The transfer stage: static, cache-line aligned, in the non-cacheable window.
 * A staged transfer's buffer needs no cache maintenance at all, which is why the
 * pass-through exists only for buffers already in this region.
 */
static unsigned char chu_stage[CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS]
			      [CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE]
    __attribute__((section(".noncacheable"), aligned(LKPI_CHU_CACHE_LINE)));
static unsigned int chu_stage_busy[CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS];

static struct lkpi_chu_slot *
chu_slot_get(void)
{
	unsigned long flags = chu_lock();
	unsigned int i;
	struct lkpi_chu_slot *slot = NULL;

	for (i = 0; i < CONFIG_LKPI_USB_CHERRYUSB_SLOTS; i++) {
		if (!chu_slots[i].in_use) {
			slot = &chu_slots[i];
			memset(&slot->urb, 0, sizeof(slot->urb));
			slot->be = NULL;
			slot->stage = NULL;
			slot->user_buf = NULL;
			slot->user_len = 0;
			slot->is_in = 0;
			slot->delivered = 0;
			slot->kill_in_flight = 0;
			slot->in_use = 1;
			break;
		}
	}
	chu_unlock(flags);
	return slot;
}

static void
chu_slot_put(struct lkpi_chu_slot *slot)
{
	unsigned long flags = chu_lock();

	slot->be = NULL;
	slot->in_use = 0;
	chu_unlock(flags);
}

static unsigned char *
chu_stage_get(void)
{
	unsigned long flags = chu_lock();
	unsigned char *p = NULL;
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS; i++) {
		if (!chu_stage_busy[i]) {
			chu_stage_busy[i] = 1;
			p = chu_stage[i];
			break;
		}
	}
	chu_unlock(flags);
	return p;
}

static void
chu_stage_put(unsigned char *p)
{
	unsigned long flags;
	unsigned int i;

	if (p == NULL)
		return;
	flags = chu_lock();
	for (i = 0; i < CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS; i++) {
		if (chu_stage[i] == p) {
			chu_stage_busy[i] = 0;
			break;
		}
	}
	chu_unlock(flags);
}

/*
 * Give a transfer its DMA buffer: pass the caller's pointer through when it
 * already lives in the non-cacheable window, stage it otherwise. On success the
 * slot carries the stage and the copy-back target; on refusal the stage is
 * already released and the return is a negative seam error.
 */
static int
chu_slot_prepare(struct lkpi_chu_slot *slot, struct lkpi_usb_be_urb *be, int is_in)
{
	unsigned int len = be->transfer_buffer_length;

	slot->is_in = is_in;
	if (len == 0)
		return 0;			/* no data stage at all */
	if (be->transfer_buffer == NULL) {
		chu_stats.refused++;
		return -LKPI_USB_BE_ERR_INVAL;
	}
	if (chu_in_window(be->transfer_buffer)) {
		chu_stats.stage_passthrough++;
		slot->urb.transfer_buffer = be->transfer_buffer;
		return 0;
	}
	if (len > CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE) {
		chu_stats.stage_overflow++;
		return -LKPI_USB_BE_ERR_NOMEM;
	}
	slot->stage = chu_stage_get();
	if (slot->stage == NULL) {
		chu_stats.stage_exhausted++;
		return -LKPI_USB_BE_ERR_NOMEM;
	}
	chu_stats.stage_staged++;
	slot->user_buf = be->transfer_buffer;
	slot->user_len = len;
	if (!is_in)
		memcpy(slot->stage, be->transfer_buffer, len);
	slot->urb.transfer_buffer = slot->stage;
	return 0;
}

/* an IN transfer's bytes go back to the caller (the driver callback reads them
 * next); the stage is released either way */
static void
chu_slot_finish(struct lkpi_chu_slot *slot, int err)
{
	if (slot->stage == NULL)
		return;
	if (slot->is_in && err == 0 && slot->user_buf != NULL) {
		uint32_t actual = slot->urb.actual_length;
		uint32_t n = actual < slot->user_len ? actual : slot->user_len;

		if (n != 0)
			memcpy(slot->user_buf, slot->stage, n);
	}
	chu_stage_put(slot->stage);
	slot->stage = NULL;
	slot->user_buf = NULL;
	slot->user_len = 0;
}

static void
chu_slot_release(struct lkpi_chu_slot *slot)
{

	chu_stage_put(slot->stage);
	slot->stage = NULL;
	slot->user_buf = NULL;
	slot->user_len = 0;
}

/* ------------------------------------------------------------------ */
/* the completion wrapper (async lane)                                 */
/* ------------------------------------------------------------------ */

/*
 * CherryUSB calls this from the controller's completion context with either the
 * byte count (errorcode == 0) or the negative error code. It fills the seam's
 * contract and calls the engine's completion exactly once per transfer.
 */
static void
chu_complete(void *arg, int nbytes)
{
	struct lkpi_chu_slot *slot = (struct lkpi_chu_slot *)arg;
	struct lkpi_usb_be_urb *be = slot->be;
	int err = nbytes < 0 ? nbytes : 0;

	/*
	 * The exactly-once guard. It catches a second completion arriving for the
	 * same transfer while the slot is still marked delivered - the common HCD
	 * double-delivery shape. (A completion arriving after the slot was reused
	 * by a new transfer cannot be told apart by an argument-only protocol; the
	 * engine's own in-flight claim is the second line there, and this adapter
	 * never fabricates a completion.)
	 */
	if (be == NULL || slot->delivered) {
		chu_stats.complete_dropped++;
		return;
	}
	slot->delivered = 1;

	/*
	 * A kill delivers this completion itself, and the HCD writes its own
	 * shutdown code for it. The seam's contract for a cancelled transfer is
	 * the CANCELLED code (the engine reports the Linux -ENOENT then), so the
	 * code is rewritten only inside a kill this adapter is running.
	 */
	if (slot->kill_in_flight && err == -LKPI_USB_BE_ERR_SHUTDOWN) {
		err = -LKPI_USB_BE_ERR_CANCELLED;
		chu_stats.kill_translated++;
	}

	/* State belongs to the endpoint, not the recyclable transfer slot.
	 * The HCD supplies the next PID even for partial/error completions.
	 * Save before giveback can arm the next transfer. Reference: CherryUSB
	 * EHCI usb_hc_ehci.c:512,608,724; Linux USB hcd.c:2002. */
	if (be->ep != &be->hport->ep0)
		be->ep->data_toggle = slot->urb.data_toggle;
	be->actual_length = err == 0 ? (uint32_t)nbytes : slot->urb.actual_length;
	be->errorcode = err;
	chu_slot_finish(slot, err);

	chu_stats.completed++;
	be->hcpriv = NULL;
	slot->be = NULL;
	slot->in_use = 0;

	if (be->complete != NULL)
		be->complete(be->arg, err == 0 ? (int)be->actual_length : err);
}

/* ------------------------------------------------------------------ */
/* the seam operations                                                 */
/* ------------------------------------------------------------------ */

static int
chu_seam_submit(void *ctx, struct lkpi_usb_be_urb *be)
{
	struct usbh_hubport *hport;
	struct lkpi_chu_slot *slot;
	const struct usb_endpoint_descriptor *ep = NULL;
	unsigned int type;
	int is_in;
	int ret;

	(void)ctx;
	if (be == NULL || be->hport == NULL || be->ep == NULL) {
		chu_stats.refused++;
		return -LKPI_USB_BE_ERR_INVAL;
	}
	hport = (struct usbh_hubport *)be->hport->hcpriv;
	if (hport == NULL)
		return -LKPI_USB_BE_ERR_NOTCONN;

	if (be->ep == &be->hport->ep0) {
		type = USB_ENDPOINT_TYPE_CONTROL;
		if (be->setup == NULL) {
			chu_stats.refused++;
			return -LKPI_USB_BE_ERR_INVAL;
		}
		is_in = (((const struct lkpi_usb_setup_packet *)be->setup)
		    ->bmRequestType & 0x80u) != 0;
	} else {
		ep = (const struct usb_endpoint_descriptor *)be->ep->hcpriv;
		if (ep == NULL) {
			chu_stats.refused++;
			return -LKPI_USB_BE_ERR_INVAL;
		}
		type = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
		is_in = (ep->bEndpointAddress & 0x80u) != 0;
	}

	if ((be->transfer_flags & LKPI_USB_BE_URB_ZERO_PACKET) != 0)
		chu_stats.zero_packet_ignored++;

	slot = chu_slot_get();
	if (slot == NULL) {
		chu_stats.slots_exhausted++;
		return -LKPI_USB_BE_ERR_NOMEM;
	}
	ret = chu_slot_prepare(slot, be, is_in);
	if (ret != 0) {
		chu_slot_put(slot);
		return ret;
	}
	slot->be = be;
	be->hcpriv = slot;

	switch (type) {
	case USB_ENDPOINT_TYPE_CONTROL:
		usbh_control_urb_fill(&slot->urb, hport,
		    (struct usb_setup_packet *)be->setup,
		    slot->urb.transfer_buffer, be->transfer_buffer_length,
		    be->timeout, be->timeout == 0 ? chu_complete : NULL, slot);
		break;
	case USB_ENDPOINT_TYPE_INTERRUPT:
		usbh_int_urb_fill(&slot->urb, hport,
		    (struct usb_endpoint_descriptor *)ep, slot->urb.transfer_buffer,
		    be->transfer_buffer_length, be->timeout,
		    be->timeout == 0 ? chu_complete : NULL, slot);
		break;
	default:
		usbh_bulk_urb_fill(&slot->urb, hport,
		    (struct usb_endpoint_descriptor *)ep, slot->urb.transfer_buffer,
		    be->transfer_buffer_length, be->timeout,
		    be->timeout == 0 ? chu_complete : NULL, slot);
		break;
	}

	if (type != USB_ENDPOINT_TYPE_CONTROL)
		slot->urb.data_toggle = be->ep->data_toggle;
	chu_stats.submits++;
	ret = usbh_submit_urb(&slot->urb);
	if (be->timeout == 0) {
		/* asynchronous: accepted here, completed from the controller's
		 * context through the wrapper */
		if (ret < 0) {
			chu_stats.refused++;
			be->hcpriv = NULL;
			chu_slot_release(slot);
			chu_slot_put(slot);
		}
		return ret;
	}

	/*
	 * Synchronous: the stack blocked until the transfer finished (or its own
	 * timeout expired) and never called a completion. The outcome travels in
	 * the seam urb and the finished transfer is reported to the engine, whose
	 * synchronous lane reads exactly those two fields
	 * (components/linuxkpi/usb/linuxkpi_usb.c:925-933).
	 */
	if (type != USB_ENDPOINT_TYPE_CONTROL)
		be->ep->data_toggle = slot->urb.data_toggle;
	be->actual_length = slot->urb.actual_length;
	be->errorcode = ret;
	chu_stats.completed++;
	if (ret >= 0)
		chu_slot_finish(slot, 0);
	else
		chu_slot_release(slot);
	be->hcpriv = NULL;
	chu_slot_put(slot);
	return 0;
}

static int
chu_seam_kill(void *ctx, struct lkpi_usb_be_urb *be)
{
	struct lkpi_chu_slot *slot;
	int ret;

	(void)ctx;
	if (be == NULL || be->hcpriv == NULL)
		return -LKPI_USB_BE_ERR_INVAL;
	slot = (struct lkpi_chu_slot *)be->hcpriv;

	chu_stats.kills++;
	slot->kill_in_flight = 1;
	ret = usbh_kill_urb(&slot->urb);
	slot->kill_in_flight = 0;

	/*
	 * A zero return means the stack owned the transfer, cancelled it and
	 * delivered the one completion itself (through the wrapper above). A
	 * nonzero return means it did not own it - the engine's done path owns
	 * the delivery then, so nothing is delivered here.
	 */
	return ret;
}

static int
chu_seam_control(void *ctx, struct lkpi_usb_be_hubport *port,
    const struct lkpi_usb_setup_packet *setup, void *buffer)
{
	struct usbh_hubport *hport;
	unsigned char *stage = NULL;
	unsigned int len;
	int is_in;
	int ret;

	(void)ctx;
	if (port == NULL || setup == NULL)
		return -LKPI_USB_BE_ERR_INVAL;
	hport = (struct usbh_hubport *)port->hcpriv;
	if (hport == NULL)
		return -LKPI_USB_BE_ERR_NOTCONN;

	len = setup->wLength;
	is_in = (setup->bmRequestType & 0x80u) != 0;
	if (len != 0) {
		if (buffer == NULL)
			return -LKPI_USB_BE_ERR_INVAL;
		if (len > CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE) {
			chu_stats.stage_overflow++;
			return -LKPI_USB_BE_ERR_NOMEM;
		}
		if (chu_in_window(buffer)) {
			chu_stats.stage_passthrough++;
		} else {
			stage = chu_stage_get();
			if (stage == NULL) {
				chu_stats.stage_exhausted++;
				return -LKPI_USB_BE_ERR_NOMEM;
			}
			chu_stats.stage_staged++;
			if (!is_in)
				memcpy(stage, buffer, len);
		}
	}

	chu_stats.control_transfers++;
	ret = usbh_control_transfer(hport, (struct usb_setup_packet *)setup,
	    stage != NULL ? stage : buffer);

	/* the stack reports the data-stage byte count; the seam's contract is an
	 * error when the stage could not fill its length */
	if (ret >= 0 && (unsigned int)ret < len) {
		chu_stage_put(stage);
		return -LKPI_USB_BE_ERR_SHORT;
	}
	if (stage != NULL) {
		if (ret >= 0 && is_in && len != 0)
			memcpy(buffer, stage, len);
		chu_stage_put(stage);
	}
	return ret < 0 ? ret : 0;
}

static const struct lkpi_usb_be_ops chu_ops = {
	.submit_urb = chu_seam_submit,
	.kill_urb = chu_seam_kill,
	.control_transfer = chu_seam_control,
};

/*
 * Install the backend. The USB domain's bring-up calls this once, after the
 * controller driver is registered and before enumeration starts (the board's
 * usb_glue.c is the call site; it is wired with the board image round, where
 * this stack is part of the link).
 */
void
lkpi_usb_cherryusb_init(void)
{
	unsigned int i;

	/*
	 * The alignment the DMA path needs, checked where it can still be
	 * reported instead of assumed: every stage slot must sit on a cache-line
	 * boundary (the attribute above asks for it; the section's own alignment
	 * is the linker script's promise, and this is the proof that the promise
	 * held). A failure here is a link-time layout problem, not a runtime one,
	 * so it is fatal.
	 */
	for (i = 0; i < CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS; i++) {
		if (((uintptr_t)chu_stage[i] % LKPI_CHU_CACHE_LINE) != 0) {
			chu_stats.refused++;
			return;		/* refused: without the stage, no transfers */
		}
	}
	lkpi_usb_be_register(&chu_ops, NULL);
}
