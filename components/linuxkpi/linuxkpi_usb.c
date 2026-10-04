/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linuxkpi_usb - the Linux USB host API over CherryUSB's usbh engine.
 *
 * Ported from the prior experiment line ([EXPL] linuxkpi_usb.c, board-proven
 * through the rtw88 L1 PASS round) onto the freewebcam cherryusb pin
 * 323ade2f + patches (SPEC S6/linuxkpi): the old-pin EHCI specifics (the
 * 0003 software data-toggle carrier, the 0004 diag hook) are dropped - the
 * xHCI backend keeps the toggle in the controller and ships its own
 * diagnostics (usbh xdump/xreg/stall).  The P6 gap fills land here:
 *   - usb_submit_urb control-class URBs ([EXPL] rejected them with
 *     -EINVAL): the setup line is staged 64-byte aligned (the xHCI port
 *     asserts urb->setup alignment) and the urb rides hport->ep0;
 *   - usb_maxpacket (the bulk segmentation ceiling lkpi_udisk consumes).
 *
 * Contract notes (all load-bearing, unchanged):
 *   - urb completions arrive from CherryUSB's HC interrupt / hub thread;
 *     they are deferred through a done-list to the drain thread, so the
 *     driver's spinlocks stay passive mutexes.
 *   - URB_ZERO_PACKET maps onto CherryUSB's USBH_URB_ZERO_PACKET, which
 *     the xHCI port implements HC-side (an explicit zero-length TRB for a
 *     bulk OUT ending on a max-packet boundary); this layer only forwards
 *     the flag.
 *   - usb_kill_urb produces a -ENOENT completion exactly once (the
 *     submitted flag guards the double completion when CherryUSB also
 *     reports the cancel).
 *   - every urb crossing is recorded into the usbkpi ring: that ring is
 *     the byte-comparison exit the linux<->rtos diff methodology reads.
 */

/* CherryUSB headers first: with their USB_DEF_H set, linux/usb.h skips
 * the descriptor definitions and the whole TU uses one descriptor type. */
#include "usbh_core.h"
#include "usb_osal.h"
#include "usb_errno.h"

#include <tx_api.h>

#include "linuxkpi_core.h"

#include <linux/usb.h>
#include "linuxkpi_usb.h"

/* must match the value the xHCI port header defines */
#ifndef USBH_URB_ZERO_PACKET
#define	USBH_URB_ZERO_PACKET	(1u << 0)
#endif

/* ------------------------------------------------------------------ */
/* 64-byte-aligned DMA staging                                         */
/* ------------------------------------------------------------------ */

/*
 * Under CONFIG_USB_DCACHE_ENABLE the xHCI port DMAs straight out of the
 * urb pointers and asserts both setup and transfer_buffer are 64-byte
 * aligned (xhci_submit_internal).  The driver hands us heap, stack or
 * rodata pointers, so the kpi stages every transfer: the control setup
 * line through a per-urb aligned pair, data through a per-urb stage
 * allocated at first submit and reused across resubmits (the fixed-pool
 * lesson: per-transfer heap churn broke the TLSF free lists once already;
 * if a datapath round ever shows this churning, the stage graduates to a
 * pool).
 */
#define	LKPI_USB_STAGE_ALIGN	64

/* P6-b board finding: a stage that lives in the tlsf heap loses bulk-IN
 * DMA (the controller reports a complete IN while the stage still holds
 * the previous OUT's bytes) and corrupts heap metadata around it (the
 * tlsf double-free assert).  The heap had never been a DMA target before
 * (thread stacks only) - the static-pool graduation the [EXPL] stage
 * comment reserved for exactly this class of datapath trouble. */
/* P6-c form round (2026-10-01): the first slot a BOT sequence takes is
 * sized up-front (see LKPI_STAGE_MIN) so the CBW/data/CSW legs never
 * churn slots mid-sequence.  The pool is two-class: small 32 KiB slots
 * for control-adjacent traffic, and 128 KiB bulk slots - the chunk the
 * vendored msc adapter proved at 37.3/24.2 MB/s and the lkpi_udisk
 * bw/soak shape rides.  (The resize dance this replaced was exonerated
 * by the TRB-pool evidence: the real P6-b root cause was the cached
 * hep, see the submit path comment.) */
#define	LKPI_SMALL_SLOTS	2
#define	LKPI_STAGE_SIZE		32768
#define	LKPI_BIG_SLOTS		2
#define	LKPI_STAGE_BIG_SIZE	131072u
#define	LKPI_STAGE_MIN		4096u

/* 64 KiB alignment on the big slots: a 128 KiB data leg splits into two
 * chained TRBs that each stay inside one 64 KiB boundary (the port
 * contract, the shape of the vendored msc's g_msc_data buffer) */
static u8 lkpi_stage_small_mem[LKPI_SMALL_SLOTS][LKPI_STAGE_SIZE]
	__attribute__((aligned(LKPI_USB_STAGE_ALIGN)));
static u8 lkpi_stage_big_mem[LKPI_BIG_SLOTS][LKPI_STAGE_BIG_SIZE]
	__attribute__((aligned(65536)));

struct lkpi_stage_slot {
	u8	*mem;
	u32	cap;
	bool	used;
};

static struct lkpi_stage_slot lkpi_stage_pool[LKPI_SMALL_SLOTS +
    LKPI_BIG_SLOTS] = {
	{ lkpi_stage_small_mem[0], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[1], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_big_mem[0], LKPI_STAGE_BIG_SIZE, false },
	{ lkpi_stage_big_mem[1], LKPI_STAGE_BIG_SIZE, false },
};

#define	LKPI_STAGE_SLOTS	(LKPI_SMALL_SLOTS + LKPI_BIG_SLOTS)

static void lkpi_usb_stage_put(struct urb *urb);

/* the rawbulk diagnostic's stage-slot variant: the P6-b bisect that
 * separates "the stage pool's memory is the poison" from "the kpi submit
 * path is" (raw urb shape, pool address).  Read-only exposure - the
 * caller never owns the slot. */
const void *
lkpi_usb_stage_slot_mem(unsigned int i)
{

	if (i >= LKPI_STAGE_SLOTS)
		return NULL;
	return lkpi_stage_pool[i].mem;
}

static void *
lkpi_usb_stage_get(struct urb *urb, u32 len)
{
	unsigned int i;

	if (len == 0)
		return NULL;
	if (len < LKPI_STAGE_MIN)
		len = LKPI_STAGE_MIN;
	if (urb->_stage != NULL && urb->_stage_len >= len)
		return urb->_stage;
	if (len > LKPI_STAGE_BIG_SIZE) {
		lkpi_printf("usbkpi: stage %u exceeds pool\n",
		    (unsigned int)len);
		return NULL;
	}
	if (urb->_stage != NULL)
		lkpi_usb_stage_put(urb);
	/* scan order prefers the small slots for small asks (they come
	 * first); a big ask skips them on the cap test */
	for (i = 0; i < LKPI_STAGE_SLOTS; i++) {
		if (!lkpi_stage_pool[i].used &&
		    lkpi_stage_pool[i].cap >= len) {
			lkpi_stage_pool[i].used = true;
			urb->_stage = lkpi_stage_pool[i].mem;
			urb->_stage_raw = &lkpi_stage_pool[i];
			urb->_stage_len = len;
			return urb->_stage;
		}
	}
	lkpi_printf("usbkpi: stage pool exhausted\n");
	return NULL;
}

/* the 8-byte control-setup stage: allocated once per urb, never resized */
static void *
lkpi_usb_setup_stage_get(struct urb *urb)
{
	void *raw, *aligned;

	if (urb->_setup_stage != NULL)
		return urb->_setup_stage;
	raw = lkpi_kmem_alloc(8 + LKPI_USB_STAGE_ALIGN, GFP_KERNEL, false);
	if (raw == NULL)
		return NULL;
	aligned = (void *)(((uintptr_t)raw + LKPI_USB_STAGE_ALIGN) &
	    ~(uintptr_t)(LKPI_USB_STAGE_ALIGN - 1));
	urb->_setup_stage = aligned;
	urb->_setup_raw = raw;
	return aligned;
}

static void
lkpi_usb_stage_put(struct urb *urb)
{

	if (urb->_stage_raw != NULL) {
		((struct lkpi_stage_slot *)urb->_stage_raw)->used = false;
		urb->_stage = NULL;
		urb->_stage_raw = NULL;
		urb->_stage_len = 0;
	}
	if (urb->_setup_raw != NULL) {
		lkpi_printf("usbkpi: free setup stage %p\n",
		    urb->_setup_raw);
		lkpi_kmem_free(urb->_setup_raw);
		urb->_setup_stage = NULL;
		urb->_setup_raw = NULL;
	}
}

/* the bytes the controller actually reads/writes (the stage when one is
 * staged, the driver buffer otherwise) */
static const void *
lkpi_usb_xfer_ptr(const struct urb *urb)
{

	return urb->_stage != NULL ? urb->_stage : urb->transfer_buffer;
}

/* ------------------------------------------------------------------ */
/* usbkpi dump ring (the comparison exit)                              */
/* ------------------------------------------------------------------ */

/* 16k records = a full probe/soak window (power-on ctrl burst + the data
 * interleave) survives in the ring; 512 KiB of bss */
#define	USBKPI_RING_N	16384

static struct usbkpi_record usbkpi_ring[USBKPI_RING_N];
static unsigned int usbkpi_ring_head;
static u64 usbkpi_seq;
static bool usbkpi_enabled;
/* capture filter: bitmask of USB_ENDPOINT_XFER_* types; narrow it while
 * hunting so a ctrl poll burst cannot evict the bulk segment */
static unsigned int usbkpi_capture_mask = 0xffu;

static void
usbkpi_push(const struct urb *urb, bool is_in, bool is_submit, u32 len,
    int raw_err)
{
	struct usbkpi_record *r;
	unsigned int type = usb_pipetype(urb->pipe);

	if (!usbkpi_enabled || urb == NULL ||
	    (usbkpi_capture_mask & (1u << type)) == 0)
		return;
	r = &usbkpi_ring[usbkpi_ring_head % USBKPI_RING_N];
	memset(r, 0, sizeof(*r));
	r->seq = ++usbkpi_seq;
	r->tick = (u32)tx_time_get();
	r->dir = is_in ? 1 : 0;
	/* bit7 = submit phase: urb crossings pair S/C like usbmon's lines,
	 * the diff keys on it; control records keep plain type */
	r->type = (u8)(type | (is_submit ? 0x80u : 0));
	r->ep = usb_pipeendpoint(urb->pipe) | (is_in ? 0x80 : 0);
	r->len = len;
	/* the raw CherryUSB errorcode (positive form), 0 on success - the
	 * lkpi errno mapping loses the distinction the diff needs */
	r->status = (u8)(raw_err < 0 ? -raw_err : 0);
	if (urb->transfer_buffer_length != 0) {
		const void *bytes = lkpi_usb_xfer_ptr(urb);
		u32 n = len > 8 ? 8 : len;

		if (bytes != NULL) {
			memcpy(r->head, bytes, n);
			r->crc = lkpi_crc32(bytes, len);
		}
	}
	usbkpi_ring_head++;
}

/* 1 entering, 2 ctrl_mtx held, 3 inside usbh_control_transfer, 4 back;
 * read by usbkpi_wedge_report */
static volatile unsigned int lkpi_usb_ctrl_stage;
static struct usbh_hubport *lkpi_usb_last_hport;

/* ------------------------------------------------------------------ */
/* rxhold (control experiment; the R-LK3 shape, kept for forensics)    */
/* ------------------------------------------------------------------ */

/*
 * Diagnostic gate: while held, bulk-IN submits are stashed instead of
 * armed, so a window runs without any concurrent rx churn.  The submitter
 * sees success and simply never gets a completion - the driver's resubmit
 * loop stays quiet, no retry storm.
 */
static bool usbkpi_rxhold_gate;
static struct urb *usbkpi_rxhold_q[8];
static unsigned int usbkpi_rxhold_n;

void
usbkpi_rxhold_set(bool on)
{
	struct urb *held[8];
	unsigned int n = 0, i;

	TX_INTERRUPT_SAVE_AREA

	TX_DISABLE;
	usbkpi_rxhold_gate = on;
	if (!on) {
		n = usbkpi_rxhold_n;
		usbkpi_rxhold_n = 0;
		memcpy(held, usbkpi_rxhold_q, n * sizeof(*held));
	}
	TX_RESTORE;

	/* release outside the critical section: arming sleeps */
	for (i = 0; i < n; i++) {
		lkpi_printf("usbkpi: rxhold release urb %p\n",
		    (void *)held[i]);
		(void)usb_submit_urb(held[i], 0);
	}
}

bool
usbkpi_rxhold(void)
{

	return usbkpi_rxhold_gate;
}

void
usbkpi_wedge_report(void)
{

	lkpi_printf("usbkpi: wedge report: ctrl_stage=%u seq=%llu\n",
	    lkpi_usb_ctrl_stage, (unsigned long long)usbkpi_seq);
	{
		/* the drain thread's ThreadX state and the 0xEF stack-fill
		 * watermark, read straight from the TCB */
		TX_THREAD *t = lkpi_usb_drain_thread_id();

		if (t != NULL) {
			const uint32_t *lo = t->tx_thread_stack_start;
			const uint32_t *hi = t->tx_thread_stack_end;
			unsigned int freeb = 0;

			while (lo + (freeb >> 2) < hi &&
			    lo[freeb >> 2] == 0xEFEFEFEFu)
				freeb += 4;
			lkpi_printf("usbkpi: drain state=%lu run=%lu "
			    "stackfree=%u/%u\n",
			    (unsigned long)t->tx_thread_state,
			    (unsigned long)t->tx_thread_run_count,
			    freeb, (unsigned)t->tx_thread_stack_size);
		}
	}
}

static void
usbkpi_push_ctrl(struct usb_device *dev, u8 requesttype, u8 request,
    u16 value, u16 index, u16 size, const void *data, int raw_err,
    bool submit)
{
	struct usbkpi_record *r;

	if (!usbkpi_enabled || dev == NULL ||
	    (usbkpi_capture_mask & (1u << USB_ENDPOINT_XFER_CONTROL)) == 0)
		return;
	r = &usbkpi_ring[usbkpi_ring_head % USBKPI_RING_N];
	memset(r, 0, sizeof(*r));
	r->seq = ++usbkpi_seq;
	r->tick = (u32)tx_time_get();
	r->dir = (requesttype & USB_DIR_IN) != 0 ? 1 : 0;
	/* same convention as the urb records: bit7 = submit phase */
	r->type = (u8)(USB_ENDPOINT_XFER_CONTROL | (submit ? 0x80u : 0));
	r->ep = 0;
	r->len = size;
	/* the raw CherryUSB errorcode (positive form), 0 on success - the
	 * lkpi errno mapping loses the distinction the diff needs */
	r->status = (u8)(raw_err < 0 ? -raw_err : 0);
	if (data != NULL && size != 0) {
		u32 n = size > 4 ? 4 : size;

		memcpy(r->head, data, n);
		r->crc = lkpi_crc32(data, size);
	}
	/* control records pack the request line too: [0..3] the payload
	 * prefix, [4..5] wValue, [6] bRequest, [7] bmRequestType */
	r->head[4] = (u8)value;
	r->head[5] = (u8)(value >> 8);
	r->head[6] = request;
	r->head[7] = requesttype;
	(void)index;
	usbkpi_ring_head++;
}

void
usbkpi_set_enabled(bool on)
{

	/* silent: the shell command owns the reply (print discipline -
	 * command output via csh, background/diag output via the sink) */
	usbkpi_enabled = on;
}

void
usbkpi_set_type_mask(unsigned int mask)
{

	usbkpi_capture_mask = mask == 0 ? 0xffu : mask;
}

unsigned int
usbkpi_type_mask(void)
{

	return usbkpi_capture_mask;
}

unsigned int
usbkpi_get_records(struct usbkpi_record *out, unsigned int n,
    unsigned int skip)
{
	unsigned int total = usbkpi_ring_head % USBKPI_RING_N;
	unsigned int avail = total > skip ? total - skip : 0;
	unsigned int count = avail < n ? avail : n;
	unsigned int i;

	for (i = 0; i < count; i++) {
		unsigned int idx = (usbkpi_ring_head - skip - count + i) %
		    USBKPI_RING_N;

		memcpy(&out[i], &usbkpi_ring[idx], sizeof(*out));
	}
	return count;
}

unsigned long long
usbkpi_sequence(void)
{

	return usbkpi_seq;
}

u32
lkpi_crc32(const void *buf, size_t len)
{
	static u32 table[256];
	static bool table_ready;
	const u8 *p = buf;
	u32 crc = 0xffffffff;
	size_t i;

	if (!table_ready) {
		u32 c;
		int b, k;

		for (b = 0; b < 256; b++) {
			c = (u32)b;
			for (k = 0; k < 8; k++)
				c = (c & 1) ? 0xedb88320 ^ (c >> 1) : c >> 1;
			table[b] = c;
		}
		table_ready = true;
	}
	for (i = 0; i < len; i++)
		crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
	return crc ^ 0xffffffff;
}

/* ------------------------------------------------------------------ */
/* driver registry (the RTOS shells register explicitly; the Linux shell
 * rides the kernel's own module_usb_driver)                           */
/* ------------------------------------------------------------------ */

#define	LKPI_USB_DRIVERS_MAX	4

static struct usb_driver *lkpi_usb_drivers[LKPI_USB_DRIVERS_MAX];
static unsigned int lkpi_usb_driver_count;

int
usb_register(struct usb_driver *driver)
{

	if (driver == NULL || driver->probe == NULL)
		return -EINVAL;
	if (lkpi_usb_driver_count >= LKPI_USB_DRIVERS_MAX)
		return -ENOSPC;
	lkpi_usb_drivers[lkpi_usb_driver_count++] = driver;
	lkpi_printf("usbkpi: usb driver '%s' registered\n", driver->name);
	return 0;
}

void
usb_deregister(struct usb_driver *driver)
{
	unsigned int i;

	for (i = 0; i < lkpi_usb_driver_count; i++) {
		if (lkpi_usb_drivers[i] == driver)
			lkpi_usb_drivers[i] = NULL;
	}
}

/* ------------------------------------------------------------------ */
/* deferred completions                                                */
/* ------------------------------------------------------------------ */

/*
 * The HC interrupt handler runs urb->complete inline, so the done list
 * is guarded by the interrupt disable (a sleeping mutex here would be a
 * lockup: the producer is the HC IRQ, not a thread).  A counting
 * semaphore wakes the drain thread, which is where the driver's
 * completion callbacks finally run - the passive level the driver's
 * spinlocks-as-mutexes model assumes.
 */
static struct urb *lkpi_usb_done_head;
static struct urb *lkpi_usb_done_tail;
static usb_osal_sem_t lkpi_usb_done_sem;
static bool lkpi_usb_done_ready;
static TX_THREAD *lkpi_usb_drain_tcb;
/* all urb submissions serialize here: the CherryUSB async list is not
 * safe with two submitting threads (the rx workqueue resubmit racing a
 * shell-thread submit wedged the EHCI schedule on the prior line, M4) */
static struct mutex lkpi_usb_submit_mtx;
/* bring-up trace: every transfer crossed, shell-gated */
static bool lkpi_usb_trace;
static unsigned int lkpi_usb_trace_n;
/* P6-b forensics (spec 开放问题 3 实验): force the cherryusb SYNCHRONOUS
 * submit shape (hurb->timeout>0, the submitter sleeps on the ring's
 * waitsem - the board-proven P5-d/e usbh_msc shape) instead of the pure
 * async arm; the completion path is shared, so only the wait moves. */
/* P6-b board verdict (2026-10-01): sync-submit is THE shape.  The async
 * done-list + drain-thread chain lost completions for seconds (the probe
 * urb's completion was delivered 19 s late, straight into a re-used
 * urb), and its HC-reference accounting double-freed the urb under the
 * sync hybrid.  The xHCI bus is polled through cherryusb's own sync
 * waitsem path (the P5-d/e usbh_msc shape), so the kpi layer submits
 * synchronously and delivers the completion on the submitter's (passive)
 * context; `usbkpi sync off` re-arms the legacy async chain for
 * forensics only. */
static bool lkpi_usb_force_sync = true;

void lkpi_usb_force_sync_set(bool on)
{

	lkpi_usb_force_sync = on;
	lkpi_printf("usb: sync-submit %s\n", on ? "on" : "off");
}

void *
lkpi_usb_drain_thread_id(void)
{

	return lkpi_usb_drain_tcb;
}

static int
lkpi_usb_errno(int usbh_err)
{

	/* usbh errorcodes come back negative of the USB_ERR_* values */
	switch (-usbh_err) {
	case 0:
		return 0;
	case USB_ERR_TIMEOUT:
		return -ETIMEDOUT;
	case USB_ERR_NOTCONN:
	case USB_ERR_SHUTDOWN:
	case USB_ERR_NODEV:
		return -ENODEV;
	case USB_ERR_STALL:
		return -EPIPE;
	case USB_ERR_NOMEM:
		return -ENOMEM;
	default:
		return -EIO;
	}
}

static void
lkpi_usb_enqueue_done(struct urb *urb)
{
	TX_INTERRUPT_SAVE_AREA

	TX_DISABLE;
	if (urb->submitted) {
		urb->submitted = false;
		urb->_defer_next = NULL;
		if (lkpi_usb_done_tail != NULL)
			((struct urb *)lkpi_usb_done_tail)->_defer_next = urb;
		else
			lkpi_usb_done_head = urb;
		lkpi_usb_done_tail = urb;
		usb_osal_sem_give(lkpi_usb_done_sem);
	}
	TX_RESTORE;
}

static void
lkpi_usb_complete_cb(void *arg, int nbytes)
{
	struct urb *urb = arg;
	struct usbh_urb *hurb = urb->_usbh_urb;

	if (hurb->errorcode != 0)
		urb->status = lkpi_usb_errno(hurb->errorcode);
	else
		urb->status = 0;
	urb->actual_length = nbytes > 0 ? (u32)nbytes : hurb->actual_length;
	usbkpi_push(urb, usb_pipein(urb->pipe), false,
	    urb->status == 0 ? urb->actual_length : 0, hurb->errorcode);
	lkpi_usb_enqueue_done(urb);
}

static void
lkpi_usb_deliver(struct urb *urb)
{

	if (urb->_sync) {
		/* synchronous path: wake the waiter */
		lkpi_complete(&urb->_done);
	} else if (urb->complete != NULL) {
		urb->complete(urb);
	}
}

static void
lkpi_usb_drain_one(struct urb *urb)
{

	/* move the stage's received bytes home before anything can observe
	 * the completion (the usbkpi record in the IRQ callback already
	 * read the stage) */
	if (urb->_stage != NULL && urb->status == 0 && usb_pipein(urb->pipe) &&
	    urb->actual_length != 0 && urb->transfer_buffer != NULL)
		memcpy(urb->transfer_buffer, urb->_stage, urb->actual_length);

	lkpi_usb_deliver(urb);
	/* the HC reference: the urb may go away here if the caller already
	 * dropped its own reference (rtw88's submit-then-free pattern) */
	usb_free_urb(urb);
}

static void *lkpi_usb_drain_thread_entry(void *arg);

static void *
lkpi_usb_drain_thread_entry(void *arg)
{
	struct urb *urb;

	(void)arg;
	for (;;) {
		(void)usb_osal_sem_take(lkpi_usb_done_sem, 0xffffffffU);
		for (;;) {
			TX_INTERRUPT_SAVE_AREA

			TX_DISABLE;
			urb = lkpi_usb_done_head;
			if (urb != NULL) {
				lkpi_usb_done_head =
				    (struct urb *)urb->_defer_next;
				if (lkpi_usb_done_head == NULL)
					lkpi_usb_done_tail = NULL;
				urb->_defer_next = NULL;
			}
			TX_RESTORE;
			if (urb == NULL)
				break;
			lkpi_usb_drain_one(urb);
		}
	}
	return NULL;
}

/* the ep0 staging mutex: usb_control_transfer serializes on the hubport
 * mutex, but our stage is filled before that lock */
static struct mutex lkpi_usb_ctrl_mtx;

/* ------------------------------------------------------------------ */
/* urb lifecycle                                                       */
/* ------------------------------------------------------------------ */

struct urb *
usb_alloc_urb(int iso_packets, gfp_t mem_flags)
{
	struct urb *urb;
	size_t size = sizeof(struct urb) + sizeof(struct usbh_urb) +
	    (iso_packets > 0 ? (size_t)iso_packets * 64 : 0);

	(void)mem_flags;
	urb = kzalloc(size, GFP_KERNEL);
	if (urb == NULL)
		return NULL;
	urb->_usbh_urb = (char *)urb + sizeof(*urb);
	urb->_refs = 1;
	lkpi_completion_init(&urb->_done);
	return urb;
}

void
usb_free_urb(struct urb *urb)
{

	if (urb == NULL)
		return;
	lkpi_printf("usbkpi: free_urb %p refs %d\n", (void *)urb,
	    urb->_refs);
	if (--urb->_refs > 0)
		return;
	lkpi_completion_destroy(&urb->_done);
	lkpi_usb_stage_put(urb);
	kfree(urb);
}

static struct usbh_hubport *
lkpi_usb_hport(struct usb_device *dev)
{

	return dev != NULL ? (struct usbh_hubport *)dev->_hport : NULL;
}

static struct usb_host_endpoint *
lkpi_usb_find_hep(struct usb_device *dev, unsigned int pipe)
{
	struct usb_interface *intf;
	unsigned int i;

	if (dev == NULL)
		return NULL;
	intf = (struct usb_interface *)dev->_intf;
	if (intf == NULL || intf->cur_altsetting == NULL)
		return NULL;
	for (i = 0; i < (unsigned int)intf->cur_altsetting->num_endpoints;
	    i++) {
		struct usb_host_endpoint *hep =
		    &intf->cur_altsetting->endpoint[i];

		if (usb_endpoint_num(&hep->desc) == usb_pipeendpoint(pipe) &&
		    usb_endpoint_dir_in(&hep->desc) == usb_pipein(pipe))
			return hep;
	}
	return NULL;
}

u16
usb_maxpacket(struct usb_device *udev, int pipe)
{
	struct usb_host_endpoint *hep = lkpi_usb_find_hep(udev,
	    (unsigned int)pipe);

	/* shape-identical to [LNX] usb.h:2060 (ep0 answers 64 there via
	 * dev->ep0; here the hubport's ep0 descriptor carries it) */
	if (hep == NULL)
		return 0;
	return usb_endpoint_maxp(&hep->desc);
}

int
usb_submit_urb(struct urb *urb, gfp_t mem_flags)
{
	struct usbh_urb *hurb = urb->_usbh_urb;
	struct usbh_hubport *hport = lkpi_usb_hport(urb->dev);
	unsigned int type = usb_pipetype(urb->pipe);
	/* linuxkpi-hub spec §3.5-2: interrupt pipes ALWAYS take the async
	 * chain, independent of the P6-b global sync flag.  An int
	 * completion's moment is the device's (a parked int TD can sit on
	 * the ring for minutes); a sync submit would burn a 5s HC timeout
	 * round per poll and cannot carry "event whenever".  The v2.3
	 * async vetoes (19s delivery / refs double-free / one-urb-per-ep)
	 * were all BOT multi-urb mixing on one endpoint; the int channel
	 * is a single self-rearming urb on an exclusively-owned endpoint,
	 * so the one-urb-in-flight contract holds by construction. */
	bool async = (type == USB_ENDPOINT_XFER_INT) || !lkpi_usb_force_sync;
	int ret;

	(void)mem_flags;
	if (hport == NULL)
		return -ENODEV;
	if (urb->submitted)
		return -EBUSY;
	/* the drain thread only exists for async submissions: the sync-
	 * default P6-b build never ran lkpi_usb_start, `usbkpi sync off`
	 * forensics arms it, and since linuxkpi-hub H1 so does every
	 * interrupt pipe */
	if (async && !lkpi_usb_done_ready)
		lkpi_usb_start();

	/* rxhold gate (see usbkpi_rxhold_set): hold the urb back from the
	 * HC; the submitter sees success, the urb just never completes */
	if (usbkpi_rxhold_gate && usb_pipein(urb->pipe) &&
	    type == USB_ENDPOINT_XFER_BULK) {
		bool stashed = false;

		TX_INTERRUPT_SAVE_AREA

		TX_DISABLE;
		if (usbkpi_rxhold_n <
		    sizeof(usbkpi_rxhold_q) / sizeof(*usbkpi_rxhold_q)) {
			usbkpi_rxhold_q[usbkpi_rxhold_n++] = urb;
			stashed = true;
		}
		TX_RESTORE;
		if (stashed)
			return 0;
		/* ring full: fall through and arm normally */
	}

	memset(hurb, 0, sizeof(*hurb));
	hurb->hport = hport;

	if (type == USB_ENDPOINT_XFER_CONTROL) {
		/* §3.6-1, the gap lkpi_udisk's BOM Reset / Get Max LUN
		 * consume (and case B's usb-storage ctrl path would): the
		 * setup line is staged 64-byte aligned (the xHCI port
		 * asserts it) and the urb rides hport->ep0. */
		void *setup_stage;

		if (urb->setup_packet == NULL)
			return -EINVAL;
		setup_stage = lkpi_usb_setup_stage_get(urb);
		if (setup_stage == NULL) {
			urb->status = -ENOMEM;
			return -ENOMEM;
		}
		memcpy(setup_stage, urb->setup_packet, 8);
		hurb->ep = &hport->ep0;
		hurb->setup = setup_stage;
	} else {
		/* resolve the endpoint EVERY submit: the cached hep below
		 * was the P6-b root cause - one urb is refilled with a
		 * different pipe per BOT leg (CBW out, data in, CSW in),
		 * and a hep cached on the first (OUT) submit made every
		 * later IN leg queue onto the OUT ring as an OUT transfer
		 * (board 2026-10-01: the TRB pool showed all three legs as
		 * OUT NORMAL TRBs on dci4; the device got 49 bytes of
		 * unplanned bulk-OUT, desynced BOT and NAKed forever -
		 * the entire "bulk IN data never lands" / "zero schedule"
		 * symptom family, issues/2026-10-01-xhci-bulk-td-not-
		 * fetched).  find_hep is a two-iteration loop; the rtw88
		 * resubmit-same-pipe pattern loses nothing. */
		urb->hep = lkpi_usb_find_hep(urb->dev, urb->pipe);
		if (urb->hep == NULL || urb->hep->hcpriv == NULL)
			return -EINVAL;
		hurb->ep = urb->hep->hcpriv;
	}

	lkpi_mutex_lock(&lkpi_usb_submit_mtx);
	if (lkpi_usb_trace)
		lkpi_printf("usb: %s#%u ep%02x len%u\n",
		    type == USB_ENDPOINT_XFER_CONTROL ? "ctrl" :
		    type == USB_ENDPOINT_XFER_BULK ? "bulk" : "urb",
		    ++lkpi_usb_trace_n,
		    usb_pipeendpoint(urb->pipe) |
		    (usb_pipein(urb->pipe) ? 0x80 : 0),
		    urb->transfer_buffer_length);
	if (urb->transfer_buffer_length != 0) {
		void *stage = lkpi_usb_stage_get(urb,
		    urb->transfer_buffer_length);

		if (stage == NULL) {
			lkpi_mutex_unlock(&lkpi_usb_submit_mtx);
			urb->status = -ENOMEM;
			return -ENOMEM;
		}
		/* the stage is reused across resubmits: OUT data is copied
		 * in per submit, IN data is copied back at drain time */
		if (!usb_pipein(urb->pipe))
			memcpy(stage, urb->transfer_buffer,
			    urb->transfer_buffer_length);
		hurb->transfer_buffer = stage;
	} else {
		hurb->transfer_buffer = NULL;
	}
	hurb->transfer_buffer_length = urb->transfer_buffer_length;
	hurb->timeout = async ? 0U : 5000U;
	if (!async) {
		/* sync mode: the submit IS the transfer (the submitter
		 * sleeps on the ring's waitsem, the board-proven P5-d/e
		 * usbh_msc shape).  The completion callback stays NULL so
		 * the done-list is out of the picture entirely - the
		 * async completion chain is where the double-delivery
		 * race lives (tlsf double-free, board 2026-10-01).
		 * (2026-10-01 form fix: the NULL used to be overwritten by
		 * the common fill below, so every sync transfer ALSO ran
		 * the IRQ-side callback - a leaked done-list node per
		 * transfer and a doubled usbkpi C record, and the one
		 * field the msc reference shape does not set.) */
		hurb->complete = NULL;
		hurb->arg = NULL;
	} else {
		hurb->complete = lkpi_usb_complete_cb;
		hurb->arg = urb;
	}
	/* linuxkpi-hub spec §3.5-3: carry the interval in cherryusb's
	 * microsecond invariant ([CHY] usbh_core.h:82) - urb->interval
	 * holds microframes (HS/SS) or frames (LS/FS) after the fill-time
	 * normalization.  Recorded fact (hub spec §3.6-3): the xHCI port
	 * writes but never reads hurb->interval today (the wire period is
	 * the EP context's bInterval exponent fixed at Configure-Endpoint
	 * time); the mapping keeps the semantics complete for a future
	 * cherryusb consumer. */
	if (type == USB_ENDPOINT_XFER_INT) {
		if (urb->dev->speed == USB_SPEED_HIGH ||
		    urb->dev->speed == USB_SPEED_SUPER ||
		    urb->dev->speed == USB_SPEED_SUPER_PLUS)
			hurb->interval = (u32)urb->interval * 125U;
		else
			hurb->interval = (u32)urb->interval * 1000U;
	}
	if ((urb->transfer_flags & URB_ZERO_PACKET) != 0)
		hurb->transfer_flags |= USBH_URB_ZERO_PACKET;

	urb->status = 0;
	urb->actual_length = 0;
	urb->submitted = true;
	usbkpi_push(urb, usb_pipein(urb->pipe), true,
	    urb->transfer_buffer_length, 0);
	ret = usbh_submit_urb(hurb);
	lkpi_mutex_unlock(&lkpi_usb_submit_mtx);
	if (!async && ret == 0) {
		/* the submit returned because the transfer COMPLETED:
		 * collect it right here (the submitter's thread is a
		 * passive context, the same level the drain thread would
		 * deliver at) and hand the completion to the driver.  No
		 * reference accounting: the HC reference only exists for
		 * an async arm, and a sync submit's transfer is over by
		 * the time the call returns either way (the refs double-
		 * decrement made the urb kfree itself under a live
		 * driver, board 2026-10-01). */
		urb->submitted = false;
		urb->status = lkpi_usb_errno(hurb->errorcode);
		urb->actual_length = hurb->actual_length;
		usbkpi_push(urb, usb_pipein(urb->pipe), false,
		    urb->status == 0 ? urb->actual_length : 0,
		    hurb->errorcode);
		if (urb->_stage != NULL && urb->status == 0 &&
		    usb_pipein(urb->pipe) && urb->actual_length != 0 &&
		    urb->transfer_buffer != NULL)
			memcpy(urb->transfer_buffer, urb->_stage,
			    urb->actual_length);
		lkpi_usb_deliver(urb);
		return urb->status;
	}
	if (!async && ret != 0) {
		/* timed out inside the HC: no completion callback exists
		 * (complete is NULL); same no-reference-accounting rule */
		urb->submitted = false;
		urb->status = lkpi_usb_errno(hurb->errorcode);
		return urb->status;
	}
	if (ret != 0) {
		urb->submitted = false;
		urb->status = lkpi_usb_errno(ret);
		/* trace-gated: the capture protocol's arm retry hammers
		 * this path thousands of times by design (hub H2), the
		 * per-miss dump would flood the console */
		if (lkpi_usb_trace)
			lkpi_printf("usbkpi: urb submit raw %d urb %p hurb "
			    "%p w [%p %p %p %p %p %p]\n",
			    ret, (void *)urb, (void *)hurb,
			    ((void **)hurb)[0], ((void **)hurb)[1],
			    ((void **)hurb)[2], ((void **)hurb)[3],
			    ((void **)hurb)[4], ((void **)hurb)[5]);
		return urb->status;
	}
	/* the HC reference: keeps the urb (and its stage) alive across the
	 * driver's usb_free_urb-until-completion usage pattern until the
	 * drain thread delivers the completion.  Sync mode skips it: the
	 * transfer is over when the call returns, and the collect paths
	 * above do their own accounting-free retire. */
	if (async)
		urb->_refs++;
	return 0;
}

void
usb_kill_urb(struct urb *urb)
{
	TX_INTERRUPT_SAVE_AREA

	if (!urb->submitted)
		return;
	(void)usbh_kill_urb(urb->_usbh_urb);

	/*
	 * CherryUSB reports the cancel through the completion callback for
	 * an in-flight urb, but a queued-but-not-run urb may be dropped
	 * silently.  Give the HC thread a beat, then force the -ENOENT
	 * delivery.  The submitted flag keeps the two paths from both
	 * firing.
	 */
	lkpi_msleep(2);
	TX_DISABLE;
	if (urb->submitted) {
		urb->submitted = false;
		urb->status = -ENOENT;
		TX_RESTORE;
		usbkpi_push(urb, usb_pipein(urb->pipe), false, 0,
		    -ENOENT);
		lkpi_usb_deliver(urb);
		usb_free_urb(urb);	/* the HC reference */
		return;
	}
	TX_RESTORE;
}

void
usb_unlink_urb(struct urb *urb)
{

	usb_kill_urb(urb);
}

/* ------------------------------------------------------------------ */
/* usb_anchor family (spec §3.6-4: the rtl8xxxu concurrency model; the
 * batch-kill is the semantics [LNX] urb.c:812 provides)               */
/* ------------------------------------------------------------------ */

void
init_usb_anchor(struct usb_anchor *anchor)
{

	INIT_LIST_HEAD(&anchor->urb_list);
	lkpi_waitqueue_init(&anchor->wait);
	spin_lock_init(&anchor->lock);
	anchor->killed = 0;
	anchor->suspended = 0;
}

void
usb_anchor_urb(struct urb *urb, struct usb_anchor *anchor)
{
	unsigned long flags;

	usb_get_urb(urb);	/* the anchor reference */
	spin_lock_irqsave(&anchor->lock, flags);
	list_add_tail(&urb->anchor_list, &anchor->urb_list);
	urb->anchor = anchor;
	spin_unlock_irqrestore(&anchor->lock, flags);
}

void
usb_unanchor_urb(struct urb *urb)
{
	unsigned long flags;
	struct usb_anchor *anchor = urb->anchor;

	if (anchor == NULL)
		return;
	spin_lock_irqsave(&anchor->lock, flags);
	list_del(&urb->anchor_list);
	urb->anchor = NULL;
	spin_unlock_irqrestore(&anchor->lock, flags);
	usb_put_urb(urb);
}

void
usb_kill_anchored_urbs(struct usb_anchor *anchor)
{
	struct list_head *pos, *n;
	unsigned long flags;

	spin_lock_irqsave(&anchor->lock, flags);
	anchor->killed = 1;
	list_for_each_safe(pos, n, &anchor->urb_list) {
		struct urb *urb = list_entry(pos, struct urb, anchor_list);

		list_del(pos);
		urb->anchor = NULL;
		/* kill outside the anchor lock: it takes the HC path */
		spin_unlock_irqrestore(&anchor->lock, flags);
		usb_kill_urb(urb);
		usb_put_urb(urb);	/* the anchor reference */
		spin_lock_irqsave(&anchor->lock, flags);
	}
	spin_unlock_irqrestore(&anchor->lock, flags);
}

int
usb_control_msg(struct usb_device *dev, unsigned int pipe, u8 request,
    u8 requesttype, u16 value, u16 index, void *data, u16 size, int timeout)
{
	static struct usb_setup_packet setup
	    __attribute__((aligned(LKPI_USB_STAGE_ALIGN)));
	static u8 stage[512] __attribute__((aligned(LKPI_USB_STAGE_ALIGN)));
	struct usbh_hubport *hport = lkpi_usb_hport(dev);
	bool is_in = (requesttype & USB_DIR_IN) != 0;
	int ret;

	(void)pipe;
	if (hport == NULL)
		return -ENODEV;
	lkpi_usb_last_hport = hport;
	if (size != 0 && data == NULL)
		return -EINVAL;
	if (size > sizeof(stage)) {
		lkpi_printf("usbkpi: control transfer %u bytes exceeds "
		    "staging\n", (unsigned int)size);
		return -ENOMEM;
	}
	/* wedge forensics: where is the caller parked when a control
	 * transfer stops making progress?  1 entering, 2 ctrl_mtx held,
	 * 3 inside usbh_control_transfer, 4 back */
	lkpi_usb_ctrl_stage = 1;

	/* the ep0 stage is filled outside usbh_control_transfer's hubport
	 * mutex, so serialize it here (initialized in lkpi_usb_start);
	 * one aligned setup packet per image */
	lkpi_mutex_lock(&lkpi_usb_ctrl_mtx);
	lkpi_usb_ctrl_stage = 2;
	if (lkpi_usb_trace)
		lkpi_printf("usb: ctrl#%u %s req%02x val%04x len%u\n",
		    ++lkpi_usb_trace_n, requesttype & USB_DIR_IN ? "in" : "out",
		    request, value, size);
	setup.bmRequestType = requesttype;
	setup.bRequest = request;
	setup.wValue = value;
	setup.wIndex = index;
	setup.wLength = size;
	if (!is_in && size != 0)
		memcpy(stage, data, size);
	usbkpi_push_ctrl(dev, requesttype, request, value, index, size,
	    is_in ? NULL : data, 0, true);
	lkpi_usb_ctrl_stage = 3;
	ret = usbh_control_transfer(hport, &setup,
	    size != 0 ? stage : NULL);
	lkpi_usb_ctrl_stage = 4;
	if (is_in) {
		/* record what came back (the stage holds the received
		 * bytes), not the uninitialized caller buffer */
		usbkpi_push_ctrl(dev, requesttype, request, value, index,
		    ret == 0 ? size : (ret > 0 ? ret : size),
		    ret >= 0 ? stage : NULL, ret, false);
		if (ret >= 0 && size != 0)
			memcpy(data, stage, size);
	} else {
		usbkpi_push_ctrl(dev, requesttype, request, value, index,
		    ret >= 0 ? size : 0, ret >= 0 ? stage : NULL, ret,
		    false);
	}
	lkpi_mutex_unlock(&lkpi_usb_ctrl_mtx);
	if (ret < 0)
		return lkpi_usb_errno(ret);
	(void)timeout;
	/* Linux semantics: the actual transferred length (rtw88 compares
	 * it against the requested length itself) */
	return ret;
}

int
usb_clear_halt(struct usb_device *dev, int pipe)
{
	static struct usb_setup_packet setup
	    __attribute__((aligned(LKPI_USB_STAGE_ALIGN)));
	struct usbh_hubport *hport = lkpi_usb_hport(dev);

	if (hport == NULL)
		return -ENODEV;
	setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD |
	    USB_RECIP_ENDPOINT;
	setup.bRequest = 0x01;	/* CLEAR_FEATURE(ENDPOINT_HALT) */
	setup.wValue = 0;
	setup.wIndex = usb_pipeendpoint(pipe) |
	    (usb_pipein(pipe) ? 0x80 : 0);
	setup.wLength = 0;
	return usbh_control_transfer(hport, &setup, NULL) == 0 ? 0 : -EIO;
}

int
usb_reset_device(struct usb_device *dev)
{

	/* P6-a 定案 (spec §3.5 row 8, two-way ruling): no detach/reattach
	 * chain exists in this port yet, so answer honestly instead of the
	 * prior line's lying 0 - a driver that lands here recovers by
	 * reconnect, and the console line records the downgrade */
	(void)dev;
	lkpi_printf("usbkpi: usb_reset_device -EOPNOTSUPP (no reattach "
	    "chain)\n");
	return -EOPNOTSUPP;
}

void
usb_get_dev(struct usb_device *dev)
{

	(void)dev;
}

void
usb_put_dev(struct usb_device *dev)
{

	(void)dev;
}

/* ------------------------------------------------------------------ */
/* claim-time plumbing (used by the class hook)                        */
/* ------------------------------------------------------------------ */

void
lkpi_usb_device_bind(struct usb_device *dev, struct usb_interface *intf,
    void *hport)
{

	dev->_hport = hport;
	dev->_intf = intf;
	intf->udev = dev;
}

int
lkpi_usb_probe_drivers(struct usb_interface *intf)
{
	unsigned int i;
	int probe_err = -ENODEV;

	for (i = 0; i < lkpi_usb_driver_count; i++) {
		struct usb_driver *drv = lkpi_usb_drivers[i];
		const struct usb_device_id *id;

		if (drv == NULL || drv->probe == NULL || drv->id_table == NULL)
			continue;
		for (id = drv->id_table;
		    id->match_flags != 0 || id->idVendor != 0 ||
		    id->idProduct != 0; id++) {
			struct usb_device *udev = intf->udev;
			struct usb_host_interface *alt = intf->cur_altsetting;
			bool match = true;
			int r;

			if ((id->match_flags & USB_DEVICE_ID_MATCH_VENDOR) !=
			    0 && id->idVendor != udev->_vendor)
				match = false;
			if ((id->match_flags & USB_DEVICE_ID_MATCH_PRODUCT) !=
			    0 && id->idProduct != udev->_product)
				match = false;
			if ((id->match_flags &
			    USB_DEVICE_ID_MATCH_INT_CLASS) != 0 &&
			    id->bInterfaceClass != alt->desc.bInterfaceClass)
				match = false;
			if ((id->match_flags &
			    USB_DEVICE_ID_MATCH_INT_SUBCLASS) != 0 &&
			    id->bInterfaceSubClass !=
			    alt->desc.bInterfaceSubClass)
				match = false;
			if ((id->match_flags &
			    USB_DEVICE_ID_MATCH_INT_PROTOCOL) != 0 &&
			    id->bInterfaceProtocol !=
			    alt->desc.bInterfaceProtocol)
				match = false;
			if (!match)
				continue;
			r = drv->probe(intf, id);
			if (r == 0) {
				intf->claimed = true;
				lkpi_printf("usbkpi: '%s' claimed %04x:%04x\n",
				    drv->name, udev->_vendor, udev->_product);
				return 0;
			}
			/* a matched driver ran its probe and failed: report
			 * its errno, not the generic no-driver one */
			probe_err = r;
		}
	}
	return probe_err;
}

void
lkpi_usb_disconnect_drivers(struct usb_interface *intf)
{
	unsigned int i;

	if (!intf->claimed)
		return;
	for (i = 0; i < lkpi_usb_driver_count; i++) {
		if (lkpi_usb_drivers[i] != NULL &&
		    lkpi_usb_drivers[i]->disconnect != NULL) {
			lkpi_usb_drivers[i]->disconnect(intf);
			break;
		}
	}
	intf->claimed = false;
}

/* ------------------------------------------------------------------ */
/* startup                                                             */
/* ------------------------------------------------------------------ */

void
lkpi_usb_trace_set(bool on)
{

	lkpi_usb_trace = on;
	lkpi_usb_trace_n = 0;
	lkpi_printf("usb: trace %s\n", on ? "on" : "off");
}

void
lkpi_usb_start(void)
{

	if (lkpi_usb_done_ready)
		return;
	lkpi_mutex_init(&lkpi_usb_ctrl_mtx);
	lkpi_mutex_init(&lkpi_usb_submit_mtx);
	lkpi_usb_done_sem = usb_osal_sem_create(0);
	/* usb_osal_thread_create returns the TX_THREAD (usb_osal_threadx.c
	 * shape) - the wedge report reads its state/stack watermark */
	lkpi_usb_drain_tcb =
	    usb_osal_thread_create("usbkpi", 16384, 9,
	    lkpi_usb_drain_thread_entry, NULL);
	if (lkpi_usb_drain_tcb == NULL) {
		lkpi_printf("usbkpi: cannot start the drain thread\n");
		return;
	}
	lkpi_usb_done_ready = true;
	lkpi_printf("usbkpi: %u usb driver(s) registered\n",
	    lkpi_usb_driver_count);
}
