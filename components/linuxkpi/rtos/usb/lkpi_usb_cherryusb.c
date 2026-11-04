/* SPDX-License-Identifier: BSD-3-Clause */
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
 *         adapter, tests/board/rtos/platform/board/rk3568-itx/usb/usb_glue.c (the stack-to-board
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
 *   poll(ctx)                             usb_board_bus_ready() + USBH_IRQHandler()
 *                                         for every started bus (EHCI included), run
 *                                         from the engine's drain thread
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
#include "components/linuxkpi/rtos/usb/lkpi_usb_backend.h"
#include "components/linuxkpi/lkpi_census.h"
#include "components/linuxkpi/rtos/usb/lkpi_usb_cherryusb.h"

#undef DIV_ROUND_CLOSEST

#include "usbh_core.h"
#include "usb_errno.h"
#include "usb_domain.h"
#include "usb_osal.h"

/* ------------------------------------------------------------------ */
/* geometry                                                            */
/* ------------------------------------------------------------------ */

/* The stage geometry and the counter record live in the adapter's header (they
 * are what a bring-up reads); this translation unit only validates them. */
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

static struct lkpi_usb_chusb_stats chu_stats;

void
lkpi_usb_cherryusb_stats(struct lkpi_usb_chusb_stats *out)
{

	/* the geometry is a compile-time fact, published with the counters so a
	 * reader never has to know which image it is looking at */
	chu_stats.stage_size = CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE;
	chu_stats.stage_slots = CONFIG_LKPI_USB_CHERRYUSB_STAGE_SLOTS;
	*out = chu_stats;
}

/* ------------------------------------------------------------------ */
/* the endpoint probe's target                                          */
/* ------------------------------------------------------------------ */

/*
 * Which device the last transfer went to.  Only the address is kept, never the
 * pointer: by the time a stall watchdog asks, the port could have been torn
 * down and rebuilt, and dereferencing a stale hubport is exactly the kind of
 * defect this probe is meant to observe, not to commit.  The port is looked up
 * again through the bus's own root hub when the probe runs.
 *
 * Both submission paths record, because the last transfer before a stall is a
 * bulk one in every stall this project has measured - recording only the
 * control path would leave the probe with a target from minutes earlier.
 */
static struct {
	int		valid;
	uint8_t		busid;
	uint8_t		dev_addr;
} chu_last_dev;

static void
chu_note_target(struct usbh_hubport *hport)
{

	if (hport == NULL || hport->bus == NULL)
		return;
	chu_last_dev.valid = 1;
	chu_last_dev.busid = hport->bus->busid;
	chu_last_dev.dev_addr = hport->dev_addr;
}

/*
 * Find an enumerated port by (bus, address), walking root hub -> hubs -> device.
 * The bench's deepest chain is root -> on-board hub -> dongle (depth 1 for the
 * device), so the walk is bounded at three levels and never trusts a hub
 * pointer that is not connected.
 */
#define CHU_PROBE_MAX_DEPTH	3

static struct usbh_hubport *
chu_hubport_find_in(struct usbh_hub *hub, uint8_t addr, int depth)
{
	uint8_t port;

	if (hub == NULL || depth > CHU_PROBE_MAX_DEPTH)
		return NULL;
	for (port = 0; port < hub->nports; port++) {
		struct usbh_hubport *hp = &hub->child[port];
		struct usbh_hubport *found;

		if (!hp->connected)
			continue;
		if (hp->dev_addr == addr)
			return hp;
		if (hp->self != NULL) {
			found = chu_hubport_find_in(hp->self, addr, depth + 1);
			if (found != NULL)
				return found;
		}
	}
	return NULL;
}

static struct usbh_hubport *
chu_hubport_find(uint8_t busid, uint8_t addr)
{

	if (busid >= CONFIG_USBHOST_MAX_BUS)
		return NULL;
	return chu_hubport_find_in(&g_usbhost_bus[busid].hcd.roothub, addr, 0);
}

/*
 * One other device on the same controller, if there is one.  The verdict a
 * stalled transfer needs is "is it this device, or this controller?", and only
 * a second device on the same controller can answer it.  `skip_addr` is the
 * stalled device itself.
 */
static struct usbh_hubport *
chu_hubport_other(uint8_t busid, uint8_t skip_addr, int *count)
{
	struct usbh_hub *hub;
	uint8_t port;
	struct usbh_hubport *first = NULL;

	*count = 0;
	if (busid >= CONFIG_USBHOST_MAX_BUS)
		return NULL;
	hub = &g_usbhost_bus[busid].hcd.roothub;
	for (port = 0; port < hub->nports; port++) {
		struct usbh_hubport *hp = &hub->child[port];
		struct usbh_hub *child;
		uint8_t inner;

		if (!hp->connected)
			continue;
		if (hp->dev_addr != skip_addr) {
			(*count)++;
			if (first == NULL)
				first = hp;
			continue;
		}
		/* the stalled device may sit behind a hub: count its siblings */
		child = hp->self;
		if (child == NULL)
			continue;
		for (inner = 0; inner < child->nports; inner++) {
			struct usbh_hubport *sib = &child->child[inner];

			if (!sib->connected || sib->dev_addr == skip_addr)
				continue;
			(*count)++;
			if (first == NULL)
				first = sib;
		}
	}
	return first;
}

#ifdef CONFIG_LKPI_CENSUS
/*
 * The seam's own census face.  The counters below are the answer to "did the
 * backend ever refuse work?" - a phase whose throughput fell while
 * slots_exhausted or stage_exhausted climbed is a resource shortage, not a
 * device that had nothing to give, and the two read identically in every
 * timing bucket.  The geometry fields are re-derived by the getter, so zeroing
 * the struct costs nothing.
 */
void
lkpi_usb_cherryusb_census_reset(void)
{

	(void)memset(&chu_stats, 0, sizeof(chu_stats));
}

void
lkpi_usb_cherryusb_census_print(void)
{
	struct lkpi_usb_chusb_stats s;

	lkpi_usb_cherryusb_stats(&s);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: A chusb submits=%u done=%u pass=%u staged=%u "
	    "slots_x=%u stage_x=%u stage_ovf=%u refused=%u\n",
	    s.submits, s.completed, s.stage_passthrough, s.stage_staged,
	    s.slots_exhausted, s.stage_exhausted, s.stage_overflow, s.refused);
}
#endif /* CONFIG_LKPI_CENSUS */

/*
 * A request larger than a stage slot is refused every time it is retried, so the
 * counter alone hides the reason a transfer never completes: the request length
 * is a permanent property of the caller, not a transient shortage.  Name it once
 * with the two numbers that decide it.  The largest request in this project is a
 * driver's receive buffer (rtw89 asks for 20480), so the ceiling belongs to the
 * image's configuration, not to a constant chosen for another driver.
 */
static void
chu_stage_overflow_report(unsigned int len)
{
	static int latched;

	if (latched)
		return;
	latched = 1;
	lkpi_printf(ULOG_ERROR_LEVEL, "usbkpi: transfer of %u bytes refused - a stage slot holds %u "
	    "bytes (raise CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE for this image)\n",
	    len, (unsigned int)CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE);
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
	unsigned int		idx;		/* this slot's index (the setup
						 * stage is indexed by it) */
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
 * The setup packet is a DMA buffer the controller reads, and the controller
 * asserts it is cache-line aligned.  The seam's contract deliberately lets the
 * caller hand an unaligned 8-byte line (the engine's arm comment says so: "an
 * adapter that needs it staged aligned does that behind the seam"), so the
 * adapter stages it here exactly as it stages a data buffer - otherwise any
 * consumer that fills its own setup packet (the BOT driver does) trips the
 * controller's assertion.  It stays in cached memory: the controller's submit
 * path cleans it, the same way it cleans the data stage's pass-through.
 */
static unsigned char chu_setup[CONFIG_LKPI_USB_CHERRYUSB_SLOTS][LKPI_CHU_CACHE_LINE]
    __attribute__((aligned(LKPI_CHU_CACHE_LINE)));

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
			slot->idx = i;
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
		chu_stage_overflow_report(len);
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
	if (be->ep != &be->hport->ep0 && slot->urb.ep != NULL &&
	    (slot->urb.ep->bEndpointAddress & USB_EP_DIR_IN) != 0U &&
	    USB_GET_ENDPOINT_TYPE(slot->urb.ep->bmAttributes) ==
	    USB_ENDPOINT_TYPE_BULK) {
		chu_stats.rx_completes++;
		chu_stats.rx_last_len = err == 0 ? (unsigned int)nbytes :
		    slot->urb.actual_length;
		if (err != 0) {
			chu_stats.rx_errors++;
			chu_stats.rx_last_err = err;
		}
#ifdef CONFIG_LKPI_USB_OBS
		{
			unsigned int len = chu_stats.rx_last_len;
			unsigned int idx = 5;

			if (len < 256u)
				idx = 0;
			else if (len < 512u)
				idx = 1;
			else if (len < 1024u)
				idx = 2;
			else if (len < 1600u)
				idx = 3;
			else if (len < 2328u)
				idx = 4;
			chu_stats.rx_len_hist[idx]++;
			if (len > chu_stats.rx_max_len)
				chu_stats.rx_max_len = len;
			if (err == 0 &&
			    slot->urb.transfer_buffer_length != 0U &&
			    len >= slot->urb.transfer_buffer_length)
				chu_stats.rx_full++;
		}
#endif
	}
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
	uint32_t mode_timeout;

	(void)ctx;
	if (be == NULL || be->hport == NULL || be->ep == NULL) {
		chu_stats.refused++;
		return -LKPI_USB_BE_ERR_INVAL;
	}
	hport = (struct usbh_hubport *)be->hport->hcpriv;
	if (hport == NULL)
		return -LKPI_USB_BE_ERR_NOTCONN;
	chu_note_target(hport);

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

	/*
	 * The submission mode is captured before the transfer is handed over:
	 * an asynchronous submission can complete - and recycle the slot, and
	 * the backend object with it - before this function reads anything back
	 * from `be`. Every branch below therefore uses the captured value, not
	 * the object.
	 */
	mode_timeout = be->timeout;

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
		/* the setup packet is staged, not passed through: the controller
		 * asserts its alignment and the caller's line is 8 bytes long */
		memcpy(chu_setup[slot->idx], be->setup,
		    sizeof(struct usb_setup_packet));
		usbh_control_urb_fill(&slot->urb, hport,
		    (struct usb_setup_packet *)chu_setup[slot->idx],
		    slot->urb.transfer_buffer, be->transfer_buffer_length,
		    mode_timeout, mode_timeout == 0 ? chu_complete : NULL, slot);
		break;
	case USB_ENDPOINT_TYPE_INTERRUPT:
		usbh_int_urb_fill(&slot->urb, hport,
		    (struct usb_endpoint_descriptor *)ep, slot->urb.transfer_buffer,
		    be->transfer_buffer_length, mode_timeout,
		    mode_timeout == 0 ? chu_complete : NULL, slot);
		break;
	default:
		usbh_bulk_urb_fill(&slot->urb, hport,
		    (struct usb_endpoint_descriptor *)ep, slot->urb.transfer_buffer,
		    be->transfer_buffer_length, mode_timeout,
		    mode_timeout == 0 ? chu_complete : NULL, slot);
		break;
	}

	if (type != USB_ENDPOINT_TYPE_CONTROL)
		slot->urb.data_toggle = be->ep->data_toggle;
	chu_stats.submits++;
	ret = usbh_submit_urb(&slot->urb);
	if (mode_timeout == 0) {
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
	chu_note_target(hport);

	len = setup->wLength;
	is_in = (setup->bmRequestType & 0x80u) != 0;
	if (len != 0) {
		if (buffer == NULL)
			return -LKPI_USB_BE_ERR_INVAL;
		if (len > CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE) {
			chu_stats.stage_overflow++;
			chu_stage_overflow_report(len);
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
		if (ret >= 0 && is_in && len != 0) {
			/* the device-facing bytes, taken before the copy-back: the
			 * caller's own view of the same transfer is what a
			 * register read reports, and the two must agree */
			if (len >= 4u)
				memcpy(&chu_stats.last_in_stage_word, stage,
				    sizeof(chu_stats.last_in_stage_word));
			chu_stats.last_in_stage_len = (unsigned int)ret;
			memcpy(buffer, stage, len);
		}
		chu_stage_put(stage);
	}
	return ret < 0 ? ret : 0;
}

/*
 * The polled drain (lkpi_usb_be_ops.poll): the controller's own completion path
 * run from the engine's drain thread.  The xHCI cores on this board were
 * measured leaving an event interrupt undelivered with the event in the ring
 * (runs/wireless/2026-11-01-W2-01-wedge: usbsts=EINT, usbcmd=RS|INTE, iman IP
 * clear, evt_ep fresh, IRQ count frozen, every endpoint stalled; a
 * thread-context drain run by hand recovered the cell).  The handler's first
 * act is the controller status read, so calling it costs two register reads
 * when the interrupt was not lost.  Every bus the board has started is polled,
 * EHCI included: the 8851BU sits on an EHCI root, and a synchronous control
 * transfer on it was measured waiting out the full 5 s timeout while the
 * controller's completion was already in its ring - the EHCI frame-index
 * recovery does not cover the control/qH completion path.  The readiness gate
 * is the board glue's (usb_board_bus_ready), not an xHCI-only test.
 */
static void
chu_seam_poll(void *ctx)
{
	uint8_t busid;

	(void)ctx;
	for (busid = 0; busid < USB_BOARD_BUS_COUNT; busid++) {
		size_t flags;

		if (!usb_board_bus_ready(busid))
			continue;
		/* the board's interrupt dispatch runs the handler inside the
		 * stack's critical section; the completion path and a
		 * submitting thread share that protected state */
		flags = usb_osal_enter_critical_section();
		USBH_IRQHandler(busid);
		usb_osal_leave_critical_section(flags);
	}
}

/*
 * The controller's asynchronous schedule, printed once on demand.  Declared
 * here rather than included: the port is a port, and this file must still
 * build for a bus whose controller has no such census (the weak symbol check
 * is the linker's, not a #ifdef on a MACRO this tree does not own).
 *
 * The controller-shape filter is not decoration.  The census reads the EHCI
 * register block at the bus's base, so pointing it at an xHCI bus reads a
 * different controller's layout.  The 8851BU image only ever starts the two
 * EHCI roots, which is why that never showed; the 8821CU image starts only
 * the xHCI ones, so its first stalled-transfer census would have walked xHCI
 * registers as if they were EHCI's.  An observer must not die on the
 * controller it is observing, so the shape check lives at the caller.
 */
extern void usbh_ehci_async_report(uint8_t busid) __attribute__((weak));

void
chu_seam_async_report(void)
{
	uint8_t busid;

	if (usbh_ehci_async_report == NULL)
		return;
	for (busid = 0; busid < USB_BOARD_BUS_COUNT; busid++) {
		if (usb_board_bus_is_xhci(busid))
			continue;
		if (!usb_board_bus_ready(busid))
			continue;
		usbh_ehci_async_report(busid);
	}
}

/*
 * Same shape as the census above: the port owns the schedule, this file only
 * owns the bus list.  Returns how many controllers were re-armed.
 */
extern int usbh_ehci_async_rearm(uint8_t busid) __attribute__((weak));

int
chu_seam_async_rearm(void)
{
	uint8_t busid;
	int n = 0;

	if (usbh_ehci_async_rearm == NULL)
		return 0;
	for (busid = 0; busid < USB_BOARD_BUS_COUNT; busid++) {
		if (usb_board_bus_is_xhci(busid))
			continue;
		if (!usb_board_bus_ready(busid))
			continue;
		usbh_ehci_async_rearm(busid);
		n++;
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* the endpoint probe                                                  */
/* ------------------------------------------------------------------ */

/*
 * The device descriptor the probe asks for, read through the stack's own
 * control path (no direct register access, no private HCD call): the answer
 * therefore covers the controller, its schedule, the completion path, the
 * interrupt or the drain's poll fallback, and this adapter's staging - i.e.
 * everything the bulk path also needs.  A device that answers this is a device
 * whose host side is alive.
 *
 * Every buffer here must satisfy the stack's DMA alignment
 * (CONFIG_USB_ALIGN_SIZE, 64 on this port): the HCD asserts it, and a
 * stack-local setup packet takes the console down the moment the probe runs -
 * which is exactly what the first board arm of W7-06 did.  The probe runs once
 * per boot, so static buffers cannot race with themselves, and the two
 * descriptor buffers are separate so the stalled device's bytes survive the
 * sibling's transfer.
 */
static struct usb_setup_packet chu_probe_setup
	__attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static uint8_t chu_probe_desc[64] __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static uint8_t chu_probe_sibling[64]
	__attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static uint8_t chu_probe_status[64]
	__attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));

static int
chu_probe_get_device_desc(struct usbh_hubport *hport, uint8_t *buf)
{

	memset(&chu_probe_setup, 0, sizeof(chu_probe_setup));
	chu_probe_setup.bmRequestType = USB_REQUEST_DIR_IN |
	    USB_REQUEST_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
	chu_probe_setup.bRequest = USB_REQUEST_GET_DESCRIPTOR;
	chu_probe_setup.wValue = (uint16_t)(USB_DESCRIPTOR_TYPE_DEVICE << 8);
	chu_probe_setup.wIndex = 0;
	chu_probe_setup.wLength = 18;
	return usbh_control_transfer(hport, &chu_probe_setup, buf);
}

static int
chu_probe_get_status(struct usbh_hubport *hport, uint8_t *buf)
{

	memset(&chu_probe_setup, 0, sizeof(chu_probe_setup));
	chu_probe_setup.bmRequestType = USB_REQUEST_DIR_IN |
	    USB_REQUEST_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
	chu_probe_setup.bRequest = USB_REQUEST_GET_STATUS;
	chu_probe_setup.wValue = 0;
	chu_probe_setup.wIndex = 0;
	chu_probe_setup.wLength = 2;
	return usbh_control_transfer(hport, &chu_probe_setup, buf);
}

/* the two bytes the console reports, without trusting the descriptor's length */
static unsigned int
chu_probe_id(const uint8_t *d)
{

	return ((unsigned int)d[8] | ((unsigned int)d[9] << 8)) << 16 |
	    (unsigned int)d[10] | ((unsigned int)d[11] << 8);
}

int
lkpi_usb_cherryusb_ep0_probe(struct lkpi_usb_chusb_probe *out)
{
	struct usbh_hubport *hp, *other;
	uint32_t t0;
	int ret;

	if (out == NULL)
		return -LKPI_USB_BE_ERR_INVAL;
	memset(out, 0, sizeof(*out));
	out->desc_rc = LKPI_USB_PROBE_UNAVAILABLE;
	out->status_rc = LKPI_USB_PROBE_UNAVAILABLE;
	out->other_rc = LKPI_USB_PROBE_UNAVAILABLE;
	out->roothub_rc = LKPI_USB_PROBE_UNAVAILABLE;
	/* the field's meaning is "nothing was asked", so a caller never reads a
	 * zero here as "the device said no" */
	out->other_id = 0;
	out->roothub_status = 0;

	if (chu_stats.probe_runs != 0)
		return -USB_ERR_BUSY;	/* once per boot: the ledger says so */
	if (!chu_last_dev.valid)
		return -USB_ERR_NODEV;

	out->busid = chu_last_dev.busid;
	out->dev_addr = chu_last_dev.dev_addr;
	chu_stats.probe_runs++;

	hp = chu_hubport_find(out->busid, out->dev_addr);
	if (hp == NULL) {
		/* the port is gone: a re-enumerated or unplugged device.  That is
		 * a result in itself and is reported as `present=0`. */
		out->present = 0;
		chu_stats.probe_desc_fail++;
		return 0;
	}
	out->present = 1;

	/* marker discipline: each transfer is timed on its own so a report that
	 * stops arriving names which one hung */
	t0 = osKernelGetTickCount();
	ret = chu_probe_get_device_desc(hp, chu_probe_desc);
	out->desc_ms = (unsigned int)(osKernelGetTickCount() - t0);
	out->desc_rc = ret;
	out->desc_len = ret > 0 ? (unsigned int)ret : 0u;
	if (ret > 0) {
		memcpy(out->desc, chu_probe_desc,
		    (unsigned int)ret < sizeof(out->desc) ?
		    (unsigned int)ret : sizeof(out->desc));
		chu_stats.probe_desc_ok++;
	} else {
		chu_stats.probe_desc_fail++;
	}

	t0 = osKernelGetTickCount();
	ret = chu_probe_get_status(hp, chu_probe_status);
	out->status_ms = (unsigned int)(osKernelGetTickCount() - t0);
	out->status_rc = ret;
	out->status_len = ret > 0 ? (unsigned int)ret : 0u;
	out->status_word = ret >= 2 ? ((unsigned int)chu_probe_status[0] |
	    ((unsigned int)chu_probe_status[1] << 8)) : 0u;

	/* one other device on this controller: the only way to tell "this
	 * device's USB stopped" from "this controller stopped".  Its descriptor
	 * lands in a separate buffer - the stalled device's own bytes are the
	 * verdict's subject and must survive this second transfer. */
	other = chu_hubport_other(out->busid, out->dev_addr, &out->other_count);
	if (other == NULL) {
		out->other_rc = LKPI_USB_PROBE_UNAVAILABLE;
		out->roothub_rc = LKPI_USB_PROBE_UNAVAILABLE;
		return 0;
	}
	t0 = osKernelGetTickCount();
	ret = chu_probe_get_device_desc(other, chu_probe_sibling);
	out->other_ms = (unsigned int)(osKernelGetTickCount() - t0);
	out->other_rc = ret;
	out->other_len = ret > 0 ? (unsigned int)ret : 0u;
	if (ret > 0) {
		out->other_id = chu_probe_id(chu_probe_sibling);
		chu_stats.probe_other_ok++;
	} else {
		chu_stats.probe_other_fail++;
	}
	return 0;
}

static const struct lkpi_usb_be_ops chu_ops = {
	.submit_urb = chu_seam_submit,
	.kill_urb = chu_seam_kill,
	.control_transfer = chu_seam_control,
	.poll = chu_seam_poll,
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
