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
 *   - usb_kill_urb cancels by actual ownership (specs/lkpi-usb-stream
 *     §3.8): a QUEUED record is claimed and completed -ENOENT inline in
 *     the killer's context; an in-flight record goes through cherryusb's
 *     native two-phase kill, whose single completion claims through the
 *     normal IRQ path; kill returns only after the record quiesced.
 *   - every urb crossing is recorded into the usbkpi ring: that ring is
 *     the byte-comparison exit the linux<->rtos diff methodology reads.
 */

/* CherryUSB headers first: with their USB_DEF_H set, linux/usb.h skips
 * the descriptor definitions and the whole TU uses one descriptor type. */
#include "usbh_core.h"
#include "usb_osal.h"
#include "usb_errno.h"

#include <tx_api.h> /* TODO(de-threadx): sole remaining tx touchpoint is
 * the drain-thread stack-watermark introspection below; it dies with the
 * cherryusb OSAL migration (usb_osal_thread_create returns TX_THREAD*),
 * next round. */

#include "cmsis_os2_ext.h" /* the critical-section ext */
#include "generic_timer.h"
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
/* The wlan line arms RTW_USB_RXCB_NUM(4) self-rearming rx urbs (usb.h:
 * 4 ctrl blocks + 8 free skbs - the W3 ledger's "8 arms" counted the
 * skbs).  With the bulk-IN single-flight arbiter below only ONE of them
 * holds a stage at a time (parked urbs pin none), plus transient TX asks
 * - the MSC-era 2+2 pool still exhausted on the first bring-up with them
 * armed (stage pool exhausted -> rsvd write -ENOMEM, board 2026-10-04
 * W3). 12 small + 2 big keeps the MSC shape and leaves headroom for rx
 * redarms + control + tx.  ALL slots must be initialised: the first cut
 * listed only the 2+2 entries and left the rest zero-capped, i.e.
 * permanently unclaimable (a cap>=len edge can never match 0) - found
 * when porting the arbiter (r3 evidence build, 2026-10-04). */
#define	LKPI_SMALL_SLOTS	12
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
	{ lkpi_stage_small_mem[2], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[3], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[4], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[5], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[6], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[7], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[8], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[9], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[10], LKPI_STAGE_SIZE, false },
	{ lkpi_stage_small_mem[11], LKPI_STAGE_SIZE, false },
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
	void *hit = NULL;

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
	 * first); a big ask skips them on the cap test.  The scan-and-take
	 * is its own short critical section: the claim runs on submit
	 * threads while the drain thread releases slots (the bulk-IN
	 * arbiter's stage flow), and the bool flags must not tear. */
	{
		os_critical_state_t csr;

		csr = osCriticalEnter();
		for (i = 0; i < LKPI_STAGE_SLOTS; i++) {
			if (!lkpi_stage_pool[i].used &&
			    lkpi_stage_pool[i].cap >= len) {
				lkpi_stage_pool[i].used = true;
				urb->_stage = lkpi_stage_pool[i].mem;
				urb->_stage_raw = &lkpi_stage_pool[i];
				urb->_stage_len = len;
				hit = urb->_stage;
				break;
			}
		}
		osCriticalExit(csr);
	}
	if (hit == NULL) {
		/* first-violation latch: the park-back flow control can
		 * hit this thousands of times during a fw burst (the
		 * retry IS the flow control), one line is evidence and
		 * the count is the trend - a per-miss print floods the
		 * console (board 2026-10-06: 80+ lines in 80ms) */
		static u32 stage_dry_n;

		if (++stage_dry_n == 1)
			lkpi_printf("usbkpi: stage pool exhausted (first)\n");
	}
	return hit;
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
		os_critical_state_t csr;

		csr = osCriticalEnter();
		((struct lkpi_stage_slot *)urb->_stage_raw)->used = false;
		osCriticalExit(csr);
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

/* Always-on per-endpoint crossing counters (W2/rtw89 forensics): the
 * submit/completion pair count per endpoint is the "did every H2C chunk
 * physically go out" ledger - 144 fwdl frames on bulk-OUT ep07, with
 * failures split out by status.  Cheap (a few increments), independent
 * of the capture ring's enabled state; read from the shell via
 * `usbkpi epstat`. */
static inline u64 lkpi_usb_cntvct(void);	/* defined below */

struct usbkpi_epcnt {
	u8	ep;
	u32	submits;
	u32	completes;
	u32	fails;
	u64	first_us;	/* CNTP us of the first crossing */
	u64	last_us;	/* CNTP us of the latest crossing */
	u64	last_sub_us;	/* CNTP us of the latest submit */
	u32	max_sub_gap_us;	/* longest stretch between two submits */
};
static struct usbkpi_epcnt usbkpi_epcnt[10];
static unsigned int usbkpi_epcnt_n;

static void
usbkpi_epcnt_add(u8 ep, bool is_submit, int raw_err)
{
	unsigned int i;

	for (i = 0; i < usbkpi_epcnt_n; i++) {
		if (usbkpi_epcnt[i].ep == ep)
			break;
	}
	if (i == usbkpi_epcnt_n) {
		if (usbkpi_epcnt_n >= sizeof(usbkpi_epcnt) /
		    sizeof(usbkpi_epcnt[0]))
			return;
		usbkpi_epcnt[i].ep = ep;
		usbkpi_epcnt_n++;
	}
	{
		u64 now = lkpi_usb_cntvct();

		if (usbkpi_epcnt[i].first_us == 0)
			usbkpi_epcnt[i].first_us = now;
		usbkpi_epcnt[i].last_us = now;
	}
	if (is_submit) {
		u64 gap;

		if (usbkpi_epcnt[i].last_sub_us != 0) {
			gap = usbkpi_epcnt[i].last_us -
			    usbkpi_epcnt[i].last_sub_us;
			{
				static u32 khz;
				u32 us;

				if (khz == 0) {
					khz = (u32)(gtimer_freq_get() / 1000);
					if (khz == 0)
						khz = 1;
				}
				us = (u32)((gap * 1000ULL) / khz);
				if (us > usbkpi_epcnt[i].max_sub_gap_us)
					usbkpi_epcnt[i].max_sub_gap_us = us;
			}
		}
		usbkpi_epcnt[i].last_sub_us = usbkpi_epcnt[i].last_us;
		usbkpi_epcnt[i].submits++;
	} else {
		usbkpi_epcnt[i].completes++;
		if (raw_err != 0)
			usbkpi_epcnt[i].fails++;
	}
}

void
usbkpi_ep_report(void)
{
	unsigned int i;

	lkpi_printf("usbkpi: epcnt n=%u\n", usbkpi_epcnt_n);
	for (i = 0; i < usbkpi_epcnt_n; i++) {
		static u32 khz;
		u64 span, sub_span;
		u32 span_ms;

		if (khz == 0) {
			khz = (u32)(gtimer_freq_get() / 1000);
			if (khz == 0)
				khz = 1;
		}
		span = usbkpi_epcnt[i].last_us - usbkpi_epcnt[i].first_us;
		sub_span = usbkpi_epcnt[i].last_us -
		    usbkpi_epcnt[i].last_sub_us;
		span_ms = (u32)(span / khz);
		lkpi_printf("usbkpi: ep%02x sub=%u comp=%u fail=%u "
		    "span=%ums tail=%ums maxsubgap=%ums\n",
		    usbkpi_epcnt[i].ep, usbkpi_epcnt[i].submits,
		    usbkpi_epcnt[i].completes, usbkpi_epcnt[i].fails,
		    span_ms, (u32)(sub_span / khz),
		    usbkpi_epcnt[i].max_sub_gap_us / 1000u);
	}
}

static void
usbkpi_push(const struct urb *urb, bool is_in, bool is_submit, u32 len,
    int raw_err)
{
	struct usbkpi_record *r;
	unsigned int type = usb_pipetype(urb->pipe);

	if (urb == NULL)
		return;
	/* the always-on ledger rides ahead of the ring's enable gate */
	{
		u8 ep = (u8)(usb_pipeendpoint(urb->pipe) |
		    (is_in ? 0x80 : 0));
		u32 cl;

		cl = (u32)tx_time_get();
		usbkpi_epcnt_add(ep, is_submit, raw_err);
		(void)cl;
	}
	if (!usbkpi_enabled ||
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

/* [W2/rtw89 forensics] EP0 call ledger, readable from the MON ISR
 * (rtw89_mon.c prints these in its sampler lines so a console silence
 * can be classified in place: EP0 grinding vs pure CPU). */
volatile unsigned int usbkpi_ctrl_total;
volatile unsigned int usbkpi_ctrl_last_val;
volatile unsigned int usbkpi_ctrl_last_idx;
volatile unsigned int usbkpi_ctrl_last_len;
volatile unsigned int usbkpi_ctrl_last_ret;
volatile unsigned int usbkpi_ctrl_last_us;

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

	os_critical_state_t csr;

	csr = osCriticalEnter();
	usbkpi_rxhold_gate = on;
	if (!on) {
		n = usbkpi_rxhold_n;
		usbkpi_rxhold_n = 0;
		memcpy(held, usbkpi_rxhold_q, n * sizeof(*held));
	}
	osCriticalExit(csr);

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
/* the per-endpoint engine ([lkpi-stabilize] FR-2)                     */
/*                                                                     */
/* FreeBSD-shape port of linux_usb.c @d5c09e32c (BSD-2): every async   */
/* endpoint owns ONE backend transfer (a cherryusb usbh_urb, the       */
/* FreeBSD usb_xfer analogue) plus a FIFO of Linux URBs; the IRQ       */
/* completion claims the in-flight urb and defers it to the drain      */
/* thread, which publishes the result, runs the driver callback and   */
/* immediately re-arms the next queued urb (the source's               */
/* non_isoc_callback SETUP->TRANSFERRED fall-through, :1503-1657).    */
/* usb_free_urb kills first (source :1214-1228) - a freed urb can no   */
/* longer have a completion in flight, which is the structural fix    */
/* for the W3e freed-urb zero-write.  Function-level lineage in       */
/* usb/PROVENANCE-USB.md; deviations (anchor family, task-context     */
/* giveback via the drain thread, refs-before-publish, per-submit     */
/* hep resolve, always-staged DMA) in usb/DIVERGENCES-USB.md.         */
/* ------------------------------------------------------------------ */

enum lkpi_urb_qstate {
	LKPI_Q_IDLE = 0,	/* not owned by any endpoint */
	LKPI_Q_QUEUED,		/* in an endpoint FIFO, no TD on the HC */
	LKPI_Q_INFLIGHT,	/* the endpoint's backend urb is on the HC */
	LKPI_Q_DONE,		/* claimed by the IRQ, riding the done list */
	LKPI_Q_CALLBACK,	/* the drain thread is in the callback */
};

#define	LKPI_EP_N	12	/* every async endpoint incl. ep0 + int */
#define	LKPI_EP_QLEN	1024	/* a whole fw burst (~690 chunks) queues */

struct lkpi_usb_ep {
	spinlock_t		lock;	/* DAIF pair: queue/cur/state/stats */
	void			*key;	/* cherryusb endpoint (or &hport->ep0) */
	u8			ep_addr;
	struct usbh_urb	be;	/* THE backend urb (one in flight) */
	struct urb		*cur;	/* the in-flight urb, NULL when idle */
	struct urb		*q_head, *q_tail;
	unsigned int		qlen;
	/* single-slot done handoff (one in flight => one pending) */
	struct urb		*done_urb;
	int			done_status;
	u32			done_len;
	u64			done_tick;
	struct lkpi_usb_ep	*_done_next;	/* done-list linkage */
	u32			enqueued, completions, kicks, kills;
};

static struct lkpi_usb_ep lkpi_eps[LKPI_EP_N];
static u32 lkpi_alloc_seq;		/* urb alloc identity source */

/* first-violation latches: the FIRST hit prints one compact evidence
 * line and every hit counts (kept family; the sub-record latches
 * retired with the records themselves) */
static u32 lkpi_vio_dup_claim, lkpi_vio_submit_live, lkpi_vio_kill_unknown,
	   lkpi_vio_free_live, lkpi_vio_kill_wait_timeout, lkpi_vio_busy_reject,
	   lkpi_vio_done_state;
/* submit/kill refusals of a freed urb (the _alloc_id poison guard) */
static u32 lkpi_probe_urb_submit_dead;

#define	LKPI_URB_ID_POISON	0xdeadbeefU

static void
lkpi_vio_latch(u32 *cnt, const char *what, u32 id, u32 state)
{

	if (++*cnt == 1)
		lkpi_printf("usbkpi: VIO %s id=%u state=%u\n",
		    what, id, state);
}

static struct lkpi_usb_ep *
lkpi_ep_find(void *key, bool alloc)
{
	struct lkpi_usb_ep *slot = NULL;
	unsigned int i;

	for (i = 0; i < LKPI_EP_N; i++) {
		if (lkpi_eps[i].key == key && key != NULL)
			return &lkpi_eps[i];
		if (slot == NULL && lkpi_eps[i].key == NULL &&
		    lkpi_eps[i].cur == NULL && lkpi_eps[i].qlen == 0)
			slot = &lkpi_eps[i];
	}
	if (alloc && slot != NULL) {
		slot->key = key;
		spin_lock_init(&slot->lock);
	}
	return slot;
}

/* ------------------------------------------------------------------ */
/* deferred completions                                                */
/* ------------------------------------------------------------------ */

/*
 * The HC interrupt claims the in-flight urb and hands its endpoint to
 * this list, so the done list is guarded by the interrupt disable (the
 * producer is the HC IRQ, not a thread).  A counting semaphore wakes
 * the drain thread, where the driver's completion callbacks finally
 * run - the passive level the driver's spinlocks-as-mutexes model
 * assumes (Linux __usb_hcd_giveback_urb discipline; FreeBSD instead
 * calls complete inline holding Giant - see DIVERGENCES-USB.md).
 */
static struct lkpi_usb_ep *lkpi_usb_done_head;
static struct lkpi_usb_ep *lkpi_usb_done_tail;
static usb_osal_sem_t lkpi_usb_done_sem;
static bool lkpi_usb_done_ready;
static TX_THREAD *lkpi_usb_drain_tcb;
/* bring-up trace: every transfer crossed, shell-gated */
static bool lkpi_usb_trace;
static unsigned int lkpi_usb_trace_n;
/* P6-b board verdict (2026-10-01): sync-submit is the shape for the
 * udisk/MSC lane (`usbkpi sync off` re-arms the async engine for the
 * wlan lines, which flip it off themselves at bring-up). */
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

/* D-3 forensics: bulk-IN pump gap (HC completion -> drain-thread
 * re-arm kick), counted off the free-running CNTP so the buckets stay
 * real microseconds.  The window is when the endpoint has no posted
 * buffer and the chip must hold received frames in its own FIFO. */
static inline u64 lkpi_usb_cntvct(void)
{
	u64 v;

	v = gtimer_count_get();
	return v;
}

/* elapsed microseconds from a CNTP sample (W2/rtw89 EP0 timeline) */
static u32
lkpi_usb_elapsed_us(u64 c0)
{
	static u32 freq_khz;
	u64 d;

	if (c0 == 0)
		return 0;
	if (freq_khz == 0) {
		u64 frq;

		frq = gtimer_freq_get();
		freq_khz = (u32)(frq / 1000);
		if (freq_khz == 0)
			freq_khz = 1;
	}
	d = lkpi_usb_cntvct() - c0;
	return (u32)((d * 1000ULL) / freq_khz);
}

static u32 lkpi_gap_n, lkpi_gap_lt50, lkpi_gap_lt200, lkpi_gap_lt1k,
	   lkpi_gap_lt5k, lkpi_gap_max_us, lkpi_gap_over5k;
static u32 lkpi_park_n, lkpi_park_peak;

static void
lkpi_usb_gap_record(u64 delta_ticks)
{
	static u32 freq_khz;
	u32 us;

	if (freq_khz == 0) {
		u64 frq;

		frq = gtimer_freq_get();
		freq_khz = (u32)(frq / 1000);
		if (freq_khz == 0)
			freq_khz = 1;
	}
	us = (u32)((delta_ticks * 1000ULL) / freq_khz);

	lkpi_gap_n++;
	if (us < 50)
		lkpi_gap_lt50++;
	else if (us < 200)
		lkpi_gap_lt200++;
	else if (us < 1000)
		lkpi_gap_lt1k++;
	else if (us < 5000)
		lkpi_gap_lt5k++;
	else
		lkpi_gap_over5k++;
	if (us > lkpi_gap_max_us)
		lkpi_gap_max_us = us;
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

/* ------------------------------------------------------------------ */
/* the sync-lane backend pool                                          */
/* ------------------------------------------------------------------ */

/* The P6-b synchronous lane (complete==NULL, submitter sleeps in
 * cherryusb) needs a usbh_urb per in-flight sync transfer.  The old
 * shape embedded one in every struct urb; FR-2 moved the async engine
 * to per-endpoint backends and this static 4-deep pool serves the sync
 * lane - a freed urb no longer carries a backend urb, so the W3e
 * freed-urb usbh_urb.ep write target is gone from this lane too. */
#define	LKPI_SYNC_BE_N	4
static struct usbh_urb lkpi_sync_be[LKPI_SYNC_BE_N];
static bool lkpi_sync_be_used[LKPI_SYNC_BE_N];

static struct usbh_urb *
lkpi_sync_be_take(void)
{
	struct usbh_urb *be = NULL;
	os_critical_state_t csr;
	unsigned int i;

	csr = osCriticalEnter();
	for (i = 0; i < LKPI_SYNC_BE_N; i++) {
		if (!lkpi_sync_be_used[i]) {
			lkpi_sync_be_used[i] = true;
			memset(&lkpi_sync_be[i], 0, sizeof(lkpi_sync_be[i]));
			be = &lkpi_sync_be[i];
			break;
		}
	}
	osCriticalExit(csr);
	return be;
}

static void
lkpi_sync_be_put(struct usbh_urb *be)
{
	os_critical_state_t csr;
	unsigned int i;

	csr = osCriticalEnter();
	for (i = 0; i < LKPI_SYNC_BE_N; i++) {
		if (&lkpi_sync_be[i] == be)
			lkpi_sync_be_used[i] = false;
	}
	osCriticalExit(csr);
}

static struct usbh_hubport *lkpi_usb_hport(struct usb_device *dev);

/* ------------------------------------------------------------------ */
/* completion path: IRQ claim -> done list -> drain -> re-arm          */
/* ------------------------------------------------------------------ */

static void lkpi_usb_kick(struct lkpi_usb_ep *ep);
static void lkpi_usb_deliver(struct urb *urb);
void usb_free_urb(struct urb *urb);

static void
lkpi_usb_be_complete(void *arg, int nbytes)
{
	struct lkpi_usb_ep *ep = arg;
	struct urb *urb;
	unsigned long flags;
	u32 raw_err;
	int st;

	/* arg is the endpoint state - a persistent BSS object, never a
	 * heap record (the codex answer-11 discipline).  The claim is
	 * valid only while ep->cur is the INFLIGHT urb; a completion with
	 * nothing claimable is a double event / HC anomaly and touches
	 * nothing else. */
	spin_lock_irqsave(&ep->lock, flags);
	urb = ep->cur;
	if (urb == NULL || urb->_qstate != LKPI_Q_INFLIGHT ||
	    urb->_ep != ep) {
		spin_unlock_irqrestore(&ep->lock, flags);
		lkpi_vio_latch(&lkpi_vio_dup_claim, "dup claim",
		    urb != NULL ? urb->_alloc_id : 0,
		    urb != NULL ? urb->_qstate : 0);
		return;
	}
	raw_err = (u32)ep->be.errorcode;
	st = raw_err != 0 ? lkpi_usb_errno((int)raw_err) : 0;
	ep->cur = NULL;
	ep->completions++;
	urb->_qstate = LKPI_Q_DONE;
	ep->done_urb = urb;
	ep->done_status = st;
	ep->done_len = nbytes > 0 ? (u32)nbytes : ep->be.actual_length;
	ep->done_tick = lkpi_usb_cntvct();
	spin_unlock_irqrestore(&ep->lock, flags);

	usbkpi_push(urb, usb_pipein(urb->pipe), false,
	    st == 0 ? ep->done_len : 0, (int)raw_err);

	/* done list (DAIF-guarded): append the ENDPOINT, wake the drain */
	{
		os_critical_state_t csr;

		csr = osCriticalEnter();
		if (ep->done_urb == NULL) {
			osCriticalExit(csr);
			return;		/* drained already (cannot happen:
					 * one in flight per ep) */
		}
		ep->_done_next = NULL;
		if (lkpi_usb_done_tail != NULL)
			lkpi_usb_done_tail->_done_next = ep;
		else
			lkpi_usb_done_head = ep;
		lkpi_usb_done_tail = ep;
		osCriticalExit(csr);
	}
	usb_osal_sem_give(lkpi_usb_done_sem);
}

static void
lkpi_usb_deliver(struct urb *urb)
{

	/* the giveback contract (Linux __usb_hcd_giveback_urb, hcd.c:1645):
	 * the anchor's reference is dropped BEFORE the completion callback
	 * runs.  Without it a completed anchored urb kept refs>=1 forever -
	 * rtw89 anchors every TX frame, so the drain-side usb_free_urb
	 * never reached 0 and the OUT stage of every completed TX urb
	 * stayed pinned: 14 completions exhausted the stage pool (board
	 * 2026-10-05 W2).  Idempotent - the kill paths clear urb->anchor
	 * before calling here. */
	usb_unanchor_urb(urb);
	if (urb->complete != NULL)
		urb->complete(urb);
}

static void
lkpi_usb_drain_one(struct lkpi_usb_ep *ep)
{
	struct urb *urb;
	bool in_bulk;
	u64 t_done;
	int st;
	u32 len;
	unsigned long flags;

	spin_lock_irqsave(&ep->lock, flags);
	urb = ep->done_urb;
	if (urb == NULL || urb->_qstate != LKPI_Q_DONE ||
	    urb->_ep != ep) {
		u32 bad_id = urb != NULL ? urb->_alloc_id : 0;
		u8 bad_state = urb != NULL ? urb->_qstate : 0;

		spin_unlock_irqrestore(&ep->lock, flags);
		lkpi_vio_latch(&lkpi_vio_done_state, "drain state",
		    bad_id, bad_state);
		return;
	}
	ep->done_urb = NULL;
	st = ep->done_status;
	len = ep->done_len;
	t_done = ep->done_tick;
	urb->_qstate = LKPI_Q_CALLBACK;
	in_bulk = usb_pipein(urb->pipe) && urb->hep != NULL &&
	    usb_endpoint_xfer_bulk(&urb->hep->desc);
	spin_unlock_irqrestore(&ep->lock, flags);

	/* publish the captured result into the driver-visible fields */
	urb->status = st;
	urb->actual_length = len;

	/* move the stage's received bytes home before anything can observe
	 * the completion.  The copy length is clamped to the buffer the
	 * driver handed in (codex answer-9 mechanism 3). */
	if (urb->_stage != NULL && urb->status == 0 && usb_pipein(urb->pipe) &&
	    urb->actual_length != 0 && urb->transfer_buffer != NULL) {
		u32 n = urb->actual_length;

		if (n > urb->transfer_buffer_length) {
			lkpi_printf("lkpi: RX overrun clamp %u -> %u\n",
			    n, urb->transfer_buffer_length);
			n = urb->transfer_buffer_length;
			urb->actual_length = n;
		}
		memcpy(urb->transfer_buffer, urb->_stage, n);
	}
	/* async RX: the stage slot returns the moment its bytes are home;
	 * the driver's rearm in the callback asks for a stage again in
	 * the submit path - only the in-flight urb ever holds a slot. */
	if (urb->_stage != NULL && usb_pipein(urb->pipe))
		lkpi_usb_stage_put(urb);

	lkpi_usb_deliver(urb);

	/* if the callback resubmitted this urb it is QUEUED again and now
	 * belongs to the queue; otherwise it goes idle */
	spin_lock_irqsave(&ep->lock, flags);
	if (urb->_qstate == LKPI_Q_CALLBACK)
		urb->_qstate = LKPI_Q_IDLE;
	spin_unlock_irqrestore(&ep->lock, flags);

	/* the transport reference: the urb may go away here if the caller
	 * already dropped its own reference (the submit-then-free pattern
	 * of rtw88 and rtw89 TX) */
	usb_free_urb(urb);

	/* the endpoint's backend is free again: hand the next queued urb
	 * to the HC - strictly AFTER the driver callback returned, which
	 * is why this is the last statement (the source's fall-through
	 * SETUP, :1556-1636) */
	lkpi_usb_kick(ep);

	if (in_bulk && t_done != 0)
		lkpi_usb_gap_record(lkpi_usb_cntvct() - t_done);
}

static void *lkpi_usb_drain_thread_entry(void *arg);

static void *
lkpi_usb_drain_thread_entry(void *arg)
{
	struct lkpi_usb_ep *ep;

	(void)arg;
	for (;;) {
		(void)usb_osal_sem_take(lkpi_usb_done_sem, 0xffffffffU);
		for (;;) {
			os_critical_state_t csr;

			csr = osCriticalEnter();
			ep = lkpi_usb_done_head;
			if (ep != NULL) {
				lkpi_usb_done_head = ep->_done_next;
				if (lkpi_usb_done_head == NULL)
					lkpi_usb_done_tail = NULL;
				ep->_done_next = NULL;
			}
			osCriticalExit(csr);
			if (ep == NULL)
				break;
			lkpi_usb_drain_one(ep);
		}
	}
	return NULL;
}

/* fit the claimed urb onto the HC: grant the DMA stage, copy OUT data
 * into it, fill the backend urb (the source's SETUP leg + the arm
 * half of the old lkpi_stream_arm). */
static int
lkpi_usb_arm(struct lkpi_usb_ep *ep, struct urb *urb)
{
	struct usbh_hubport *hport = lkpi_usb_hport(urb->dev);
	unsigned int type = usb_pipetype(urb->pipe);

	if (hport == NULL)
		return -ENODEV;
	memset(&ep->be, 0, sizeof(ep->be));
	ep->be.hport = hport;

	if (type == USB_ENDPOINT_XFER_CONTROL) {
		/* the setup line rides a 64-byte-aligned stage (the xHCI
		 * port asserts it); the control ep0 key is the hubport's
		 * own ep0 (gap §3.6-1) */
		void *setup_stage;

		if (urb->setup_packet == NULL)
			return -EINVAL;
		setup_stage = lkpi_usb_setup_stage_get(urb);
		if (setup_stage == NULL)
			return -ENOMEM;
		memcpy(setup_stage, urb->setup_packet, 8);
		ep->be.ep = &hport->ep0;
		ep->be.setup = setup_stage;
	} else {
		ep->be.ep = urb->hep->hcpriv;
	}

	if (urb->transfer_buffer_length != 0) {
		void *stage = lkpi_usb_stage_get(urb,
		    urb->transfer_buffer_length);

		if (stage == NULL)
			return -ENOMEM;
		if (!usb_pipein(urb->pipe))
			memcpy(stage, urb->transfer_buffer,
			    urb->transfer_buffer_length);
		ep->be.transfer_buffer = stage;
	} else {
		ep->be.transfer_buffer = NULL;
	}
	ep->be.transfer_buffer_length = urb->transfer_buffer_length;
	ep->be.timeout = 0U;	/* async arm */
	ep->be.complete = lkpi_usb_be_complete;
	ep->be.arg = ep;	/* the ENDPOINT: persistent identity */
	if ((urb->transfer_flags & URB_ZERO_PACKET) != 0)
		ep->be.transfer_flags |= USBH_URB_ZERO_PACKET;
	if (type == USB_ENDPOINT_XFER_INT) {
		if (urb->dev->speed == USB_SPEED_HIGH ||
		    urb->dev->speed == USB_SPEED_SUPER ||
		    urb->dev->speed == USB_SPEED_SUPER_PLUS)
			ep->be.interval = (u32)urb->interval * 125U;
		else
			ep->be.interval = (u32)urb->interval * 1000U;
	}
	usbkpi_push(urb, usb_pipein(urb->pipe), true,
	    urb->transfer_buffer_length, 0);
	return 0;
}

/* the consumer pump (the source's SETUP leg loop): while the endpoint
 * is idle and the FIFO is not, claim the head and hand it to
 * cherryusb.  Callers: a producer's submit and the drain thread after
 * a delivery; the ep lock decides which of them wins each claim. */
static void
lkpi_usb_kick(struct lkpi_usb_ep *ep)
{

	for (;;) {
		struct urb *urb;
		unsigned long flags;
		int ret;

		spin_lock_irqsave(&ep->lock, flags);
		/* done_urb pending = a completion is riding the done list:
		 * do not claim another urb until the drain delivered it -
		 * the done handoff is single-slot per endpoint, so a claim
		 * here could overwrite an undelivered result (the HC-refuse
		 * delivery below and a real completion would race for the
		 * slot) */
		if (ep->cur != NULL || ep->qlen == 0 || ep->done_urb != NULL) {
			spin_unlock_irqrestore(&ep->lock, flags);
			return;
		}
		urb = ep->q_head;
		ep->q_head = urb->_qnext;
		if (ep->q_head == NULL)
			ep->q_tail = NULL;
		ep->qlen--;
		__atomic_sub_fetch(&lkpi_park_n, 1, __ATOMIC_RELAXED);
		urb->_qnext = NULL;
		urb->_qstate = LKPI_Q_INFLIGHT;
		ep->cur = urb;
		ep->kicks++;
		spin_unlock_irqrestore(&ep->lock, flags);

		ret = lkpi_usb_arm(ep, urb);
		if (ret == -ENOMEM) {
			/* the DMA stage pool IS this pipeline's flow
			 * control: re-insert at the head UNDER the lock
			 * (a concurrent submit may have appended) and
			 * wait for a completion's drain to free a stage.
			 * The driver never sees the shortage. */
			spin_lock_irqsave(&ep->lock, flags);
			urb->_qnext = ep->q_head;
			ep->q_head = urb;
			if (ep->q_tail == NULL)
				ep->q_tail = urb;
			ep->qlen++;
			__atomic_add_fetch(&lkpi_park_n, 1,
			    __ATOMIC_RELAXED);
			if (lkpi_park_n > lkpi_park_peak)
				lkpi_park_peak = lkpi_park_n;
			urb->_qstate = LKPI_Q_QUEUED;
			ep->cur = NULL;
			spin_unlock_irqrestore(&ep->lock, flags);
			return;
		}
		if (ret == 0)
			ret = usbh_submit_urb(&ep->be);
		if (ret == 0)
			return;		/* in flight; the IRQ claim ends it */

		/* the HC refused a claim we own (device gone / ring
		 * shutdown): the submission was accepted, so exactly one
		 * completion is still owed - route it through the done
		 * list and try the next head.  cur is released HERE (no
		 * completion will ever claim it); the driver callback
		 * never runs inline. */
		{
			int raw = ep->be.errorcode != 0 ? ep->be.errorcode
			    : ret;

			if (ret == -USB_ERR_BUSY)
				lkpi_vio_latch(&lkpi_vio_busy_reject,
				    "hc busy on own claim", urb->_alloc_id, 0);
			lkpi_usb_stage_put(urb);
			spin_lock_irqsave(&ep->lock, flags);
			ep->cur = NULL;
			urb->_qstate = LKPI_Q_DONE;
			ep->done_urb = urb;
			ep->done_status = lkpi_usb_errno(ret);
			ep->done_len = 0;
			ep->done_tick = lkpi_usb_cntvct();
			spin_unlock_irqrestore(&ep->lock, flags);
			usbkpi_push(urb, usb_pipein(urb->pipe), false, 0,
			    raw);
			{
				os_critical_state_t csr;

				csr = osCriticalEnter();
				ep->_done_next = NULL;
				if (lkpi_usb_done_tail != NULL)
					lkpi_usb_done_tail->_done_next = ep;
				else
					lkpi_usb_done_head = ep;
				lkpi_usb_done_tail = ep;
				osCriticalExit(csr);
			}
			usb_osal_sem_give(lkpi_usb_done_sem);
			continue;
		}
	}
}

/* the assert-time probe dump (heap.c calls this via a weak symbol when
 * the linuxkpi USB layer is linked in): the emergency print path is
 * mandatory - the caller can be parked inside the tlsf critical
 * section, where the normal print lock is a hang (N7-stab). */
void board_log_emergency(const char *fmt, ...);

void
lkpi_usb_probe_dump_emergency(void)
{

	board_log_emergency("usbkpi: vio dup=%u sublive=%u killunk=%u "
	    "freelive=%u killwait=%u busy=%u donestate=%u\n",
	    lkpi_vio_dup_claim, lkpi_vio_submit_live, lkpi_vio_kill_unknown,
	    lkpi_vio_free_live, lkpi_vio_kill_wait_timeout,
	    lkpi_vio_busy_reject, lkpi_vio_done_state);
	board_log_emergency("usbkpi: probe dead=%u skbgeo=%u\n",
	    lkpi_probe_urb_submit_dead, lkpi_skb_geo_bad_count());
	lkpi_skb_gen_report();
}

void
usbkpi_stream_report(void)
{
	unsigned int i, parked;

	parked = lkpi_park_n;
	lkpi_printf("usbkpi: vio dup=%u sublive=%u killunk=%u freelive=%u "
	    "killwait=%u busy=%u donestate=%u parked=%u\n",
	    lkpi_vio_dup_claim, lkpi_vio_submit_live, lkpi_vio_kill_unknown,
	    lkpi_vio_free_live, lkpi_vio_kill_wait_timeout,
	    lkpi_vio_busy_reject, lkpi_vio_done_state, parked);
	lkpi_printf("usbkpi: probe dead=%u skbgeo=%u\n",
	    lkpi_probe_urb_submit_dead, lkpi_skb_geo_bad_count());
	lkpi_printf("usbkpi: gap n=%u <50us=%u <200us=%u <1ms=%u <5ms=%u "
	    ">=5ms=%u max=%uus\n",
	    lkpi_gap_n, lkpi_gap_lt50, lkpi_gap_lt200, lkpi_gap_lt1k,
	    lkpi_gap_lt5k, lkpi_gap_over5k, lkpi_gap_max_us);
	for (i = 0; i < LKPI_EP_N; i++) {
		struct lkpi_usb_ep *ep = &lkpi_eps[i];
		struct urb *cur, *head;
		u32 cur_id;
		unsigned long flags;

		if (ep->key == NULL)
			continue;
		spin_lock_irqsave(&ep->lock, flags);
		cur = ep->cur;
		head = ep->q_head;
		cur_id = cur != NULL ? cur->_alloc_id : 0;
		spin_unlock_irqrestore(&ep->lock, flags);
		lkpi_printf("usbkpi: ep%02x cur=%u q=%u head=%p "
		    "e/c/k/k=%u/%u/%u/%u\n",
		    ep->ep_addr, cur_id, ep->qlen, (void *)head,
		    ep->enqueued, ep->completions, ep->kicks, ep->kills);
		(void)cur;
	}
}

/* the ep0 staging mutex: usb_control_transfer serializes on the hubport
 * mutex, but our stage is filled before that lock */
static struct mutex lkpi_usb_ctrl_mtx;

/* ------------------------------------------------------------------ */
/* urb lifecycle                                                       */
/* ------------------------------------------------------------------ */

static void lkpi_usb_kill_quiesce(struct urb *urb);

struct urb *
usb_alloc_urb(int iso_packets, gfp_t mem_flags)
{
	struct urb *urb;
	size_t size = sizeof(struct urb) +
	    (iso_packets > 0 ? (size_t)iso_packets * 64 : 0);

	(void)mem_flags;
	urb = kzalloc(size, GFP_KERNEL);
	if (urb == NULL)
		return NULL;
	urb->_refs = 1;
	urb->_alloc_id = ++lkpi_alloc_seq;
	return urb;
}

void
usb_free_urb(struct urb *urb)
{

	if (urb == NULL)
		return;
	/* trace-gated: once RX flows this fires per completed frame and
	 * swamps the console (board 2026-10-04, W3 RX alive) */
	if (lkpi_usb_trace)
		lkpi_printf("usbkpi: free_urb %p id=%u refs %d\n",
		    (void *)urb, urb->_alloc_id, urb->_refs);
	if (__atomic_sub_fetch(&urb->_refs, 1, __ATOMIC_ACQ_REL) > 0)
		return;		/* plain reference drop, not the release */
	/* [lkpi-stabilize FR-2] the FreeBSD free-kills-first discipline
	 * (source :1214-1228): a releasing free while the engine still
	 * owns the urb drains the submission FIRST - the completion paths
	 * hold nothing after this point, so the freed urb has no writer
	 * left.  This is the structural collection of the W3e freed-urb
	 * zero-write (a stale completion writing urb+224 through the old
	 * embedded usbh_urb pointer, which this engine no longer has). */
	if (urb->_qstate != LKPI_Q_IDLE) {
		lkpi_vio_latch(&lkpi_vio_free_live, "free of live urb",
		    urb->_alloc_id, urb->_qstate);
		lkpi_usb_kill_quiesce(urb);
	}
	lkpi_usb_stage_put(urb);
	urb->_alloc_id = LKPI_URB_ID_POISON;
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

/* the sync lane (lkpi_usb_force_sync=true, the udisk/MSC shape; spec
 * §2.3): the submit IS the transfer - the submitter sleeps on the
 * ring's waitsem with complete==NULL, so the done list, the records
 * and the streams are entirely out of the picture.  Kept on the
 * P6-b/2026-10-01 form: no reference accounting at all, the transfer
 * is over by the time the call returns either way. */
static int
lkpi_usb_submit_sync(struct urb *urb, unsigned int type)
{
	struct usbh_hubport *hport = lkpi_usb_hport(urb->dev);
	struct usbh_urb *hurb;
	int ret;

	(void)type;
	if (hport == NULL)
		return -ENODEV;
	hurb = lkpi_sync_be_take();
	if (hurb == NULL) {
		urb->status = -ENOMEM;
		return -ENOMEM;
	}
	/* the pool hands a zeroed backend: fill the identity the old
	 * embedded-urb shape had lost after the stream-model refactor
	 * (latent first-submit garbage - found during the FR-2 port) */
	hurb->hport = hport;
	if (usb_pipetype(urb->pipe) == USB_ENDPOINT_XFER_CONTROL) {
		void *setup_stage = lkpi_usb_setup_stage_get(urb);

		if (setup_stage == NULL || urb->setup_packet == NULL) {
			lkpi_sync_be_put(hurb);
			urb->status = -EINVAL;
			return -EINVAL;
		}
		memcpy(setup_stage, urb->setup_packet, 8);
		hurb->ep = &hport->ep0;
		hurb->setup = setup_stage;
	} else {
		urb->hep = lkpi_usb_find_hep(urb->dev, urb->pipe);
		if (urb->hep == NULL || urb->hep->hcpriv == NULL) {
			lkpi_sync_be_put(hurb);
			urb->status = -EINVAL;
			return -EINVAL;
		}
		hurb->ep = urb->hep->hcpriv;
	}
	if (urb->transfer_buffer_length != 0) {
		void *stage = lkpi_usb_stage_get(urb,
		    urb->transfer_buffer_length);

		if (stage == NULL) {
			lkpi_sync_be_put(hurb);
			urb->status = -ENOMEM;
			return -ENOMEM;
		}
		/* the stage is reused across resubmits: OUT data is copied
		 * in per submit, IN data is copied back below */
		if (!usb_pipein(urb->pipe)) {
			memcpy(stage, urb->transfer_buffer,
			    urb->transfer_buffer_length);
			/* [S9 D-3] stage-copy verification (codex E3
			 * follow-up): a staging corruption would still
			 * carry a freshly generated valid FCS on air and
			 * collect an AP ACK, so verify the copy verbatim. */
			if (memcmp(stage, urb->transfer_buffer,
			    urb->transfer_buffer_length) != 0)
				printf("lkpi: STAGE MISMATCH pipe=%x len=%u\n",
				    urb->pipe, urb->transfer_buffer_length);
		}
		hurb->transfer_buffer = stage;
	} else {
		hurb->transfer_buffer = NULL;
	}
	hurb->transfer_buffer_length = urb->transfer_buffer_length;
	hurb->timeout = 5000U;
	/* the NULL completion keeps the done list out of the picture
	 * entirely (2026-10-01 form fix: a non-NULL here made every sync
	 * transfer ALSO run the IRQ-side callback - a leaked done-list
	 * node per transfer and a doubled usbkpi C record) */
	hurb->complete = NULL;
	hurb->arg = NULL;
	urb->status = 0;
	urb->actual_length = 0;
	usbkpi_push(urb, usb_pipein(urb->pipe), true,
	    urb->transfer_buffer_length, 0);
	ret = usbh_submit_urb(hurb);
	if (ret == 0) {
		/* the submit returned because the transfer COMPLETED:
		 * collect it right here on the submitter's (passive)
		 * context */
		urb->status = lkpi_usb_errno(hurb->errorcode);
		urb->actual_length = hurb->actual_length;
		usbkpi_push(urb, usb_pipein(urb->pipe), false,
		    urb->status == 0 ? urb->actual_length : 0,
		    hurb->errorcode);
		if (urb->_stage != NULL && urb->status == 0 &&
		    usb_pipein(urb->pipe) && urb->actual_length != 0 &&
		    urb->transfer_buffer != NULL) {
			u32 n = urb->actual_length;

			/* same overrun clamp as the async drain path */
			if (n > urb->transfer_buffer_length) {
				urb->actual_length =
				    urb->transfer_buffer_length;
				n = urb->transfer_buffer_length;
			}
			memcpy(urb->transfer_buffer, urb->_stage, n);
		}
		usb_unanchor_urb(urb);
		lkpi_sync_be_put(hurb);
		return urb->status;
	}
	/* timed out inside the HC: no completion callback exists */
	urb->status = lkpi_usb_errno(hurb->errorcode);
	lkpi_sync_be_put(hurb);
	return urb->status;
}

int
usb_submit_urb(struct urb *urb, gfp_t mem_flags)
{
	struct usbh_hubport *hport = lkpi_usb_hport(urb->dev);
	unsigned int type = usb_pipetype(urb->pipe);
	/* linuxkpi-hub spec §3.5-2: interrupt pipes ALWAYS take the async
	 * chain (an int completion's moment is the device's; a sync submit
	 * would burn a 5s HC timeout round per poll).  The rtw88/rtw89
	 * lanes flip the global flag off (all bulk async): their RX
	 * resubmit loops and TX completions key on the completion
	 * callback, and the per-endpoint single-in-flight engine below
	 * supplies the one-TD-per-endpoint discipline the HC demands. */
	bool async = (type == USB_ENDPOINT_XFER_INT) || !lkpi_usb_force_sync;
	struct lkpi_usb_ep *ep;
	void *key;
	unsigned long flags;

	(void)mem_flags;
	if (hport == NULL)
		return -ENODEV;
	/* [S9b W3e] submit-of-dead-urb guard: a released urb carries the
	 * id poison; refuse before ANY field of it is touched.  First hit
	 * names the caller - this is the stale-pointer writer candidate
	 * the W3c/W3d heap-check scene hunted. */
	if (urb->_alloc_id == LKPI_URB_ID_POISON) {
		lkpi_probe_urb_submit_dead++;
		if (lkpi_probe_urb_submit_dead == 1)
			lkpi_printf("usbkpi: PROBE submit of DEAD urb %p "
			    "(ra=%p) refused\n", (void *)urb,
			    __builtin_return_address(0));
		return -ENODEV;
	}
	if (!async) {
		/* the sync lane: cherryusb's native blocking shape
		 * (P5-d/e board-proven), untouched by the engine */
		return lkpi_usb_submit_sync(urb, type);
	}
	if (urb->_kill_barrier != 0)
		return -EPERM;	/* rule 4 (kill_count, source :425-428) */

	/* the endpoint identity: control class rides the hubport ep0; the
	 * rest resolve the hep EVERY submit - the cached hep was the P6-b
	 * root cause (one urb refilled with a different pipe per BOT leg;
	 * board 2026-10-01: all three legs queued as OUT TRBs). */
	if (type == USB_ENDPOINT_XFER_CONTROL) {
		key = &hport->ep0;
	} else {
		urb->hep = lkpi_usb_find_hep(urb->dev, urb->pipe);
		if (urb->hep == NULL || urb->hep->hcpriv == NULL)
			return -EINVAL;
		key = urb->hep->hcpriv;
	}
	ep = lkpi_ep_find(key, true);
	if (ep == NULL) {
		lkpi_printf("usbkpi: ep table full ep%02x\n",
		    usb_pipeendpoint(urb->pipe) |
		    (usb_pipein(urb->pipe) ? 0x80 : 0));
		return -ENOMEM;
	}
	ep->ep_addr = usb_pipeendpoint(urb->pipe) |
	    (usb_pipein(urb->pipe) ? 0x80 : 0);

	/* the drain thread only exists for async submissions */
	if (!lkpi_usb_done_ready)
		lkpi_usb_start();

	/* rxhold gate (see usbkpi_rxhold_set): hold the urb back from the
	 * HC; the submitter sees success, the urb just never completes */
	if (usbkpi_rxhold_gate && usb_pipein(urb->pipe) &&
	    type == USB_ENDPOINT_XFER_BULK) {
		bool stashed = false;

		os_critical_state_t csr;

		csr = osCriticalEnter();
		if (usbkpi_rxhold_n <
		    sizeof(usbkpi_rxhold_q) / sizeof(*usbkpi_rxhold_q)) {
			usbkpi_rxhold_q[usbkpi_rxhold_n++] = urb;
			stashed = true;
		}
		osCriticalExit(csr);
		if (stashed)
			return 0;
		/* ring full: fall through and queue normally */
	}

	if (lkpi_usb_trace)
		lkpi_printf("usb: urb#%u ep%02x len%u\n",
		    ++lkpi_usb_trace_n,
		    usb_pipeendpoint(urb->pipe) |
		    (usb_pipein(urb->pipe) ? 0x80 : 0),
		    urb->transfer_buffer_length);

	spin_lock_irqsave(&ep->lock, flags);
	/* a live submission already owns this urb - unless it is the legal
	 * in-callback rearm: the drain set CALLBACK before running the
	 * driver's completion, and this submission becomes the next queue
	 * entry (the source's fall-through SETUP discipline) */
	if (urb->_qstate != LKPI_Q_IDLE &&
	    urb->_qstate != LKPI_Q_CALLBACK) {
		u32 live_id = urb->_alloc_id;
		u8 live_state = urb->_qstate;

		spin_unlock_irqrestore(&ep->lock, flags);
		lkpi_vio_latch(&lkpi_vio_submit_live, "submit of live urb",
		    live_id, live_state);
		return -EBUSY;
	}
	/* the transport reference (rule 1): taken while the urb is still
	 * invisible to every other context - the publish-before-reference
	 * window of the old `_refs++ after usbh_submit_urb` (codex
	 * answer-10 root cause #1, the 456B UAF) stays closed by
	 * construction */
	urb->_refs++;
	urb->_ep = ep;
	urb->_qnext = NULL;
	urb->_qstate = LKPI_Q_QUEUED;
	if (ep->q_tail != NULL)
		ep->q_tail->_qnext = urb;
	else
		ep->q_head = urb;
	ep->q_tail = urb;
	ep->qlen++;
	__atomic_add_fetch(&lkpi_park_n, 1, __ATOMIC_RELAXED);
	if (lkpi_park_n > lkpi_park_peak)
		lkpi_park_peak = lkpi_park_n;
	urb->status = 0;
	urb->actual_length = 0;
	ep->enqueued++;
	spin_unlock_irqrestore(&ep->lock, flags);

	/* claim + arm when the endpoint is idle; an accepted submission
	 * the HC then refuses is delivered as exactly one completion
	 * through the done list, never inline here */
	lkpi_usb_kick(ep);
	return 0;
}

/* internal quiesce (no pin, no barrier): the caller owns or has pinned
 * the urb, and owns the kill-barrier discipline (rules 4-7,
 * specs/lkpi-usb-stream §3.8).  Thread context only - the native kill
 * walks the Stop Endpoint command path, which sleeps. */
static void
lkpi_usb_kill_quiesce(struct urb *urb)
{

	for (;;) {
		struct lkpi_usb_ep *ep;
		u8 qs;
		os_critical_state_t csr;

		/* pointer reads only: the ep is a persistent BSS object
		 * (never freed), and qs is a u8 - neither dereferences
		 * anything the free paths could have retired */
		csr = osCriticalEnter();
		ep = urb->_ep;
		qs = urb->_qstate;
		osCriticalExit(csr);
		if (ep == NULL || qs == LKPI_Q_IDLE)
			return;		/* nothing left to quiesce */

		if (qs == LKPI_Q_QUEUED) {
			unsigned long flags;
			bool removed = false;

			spin_lock_irqsave(&ep->lock, flags);
			/* revalidate under the lock: the state may have
			 * moved between the read above and here (a kick
			 * claimed it / the drain delivered it) */
			if (urb->_ep == ep &&
			    urb->_qstate == LKPI_Q_QUEUED) {
				struct urb *it, **pp;

				pp = &ep->q_head;
				while ((it = *pp) != NULL) {
					if (it == urb) {
						*pp = it->_qnext;
						if (ep->q_tail == urb) {
							/* recompute the
							 * tail (O(n), only
							 * on tail kills) */
							struct urb *w =
							    ep->q_head;

							ep->q_tail = NULL;
							while (w != NULL) {
								ep->q_tail = w;
								w = w->_qnext;
							}
						}
						removed = true;
						break;
					}
					pp = &it->_qnext;
				}
				if (removed) {
					ep->qlen--;
					__atomic_sub_fetch(&lkpi_park_n, 1,
					    __ATOMIC_RELAXED);
					urb->_qstate = LKPI_Q_IDLE;
					urb->_ep = NULL;
					ep->kills++;
				}
			}
			spin_unlock_irqrestore(&ep->lock, flags);
			if (!removed)
				continue;	/* state moved; re-examine */

			/* -ENOENT inline in the killer's context, Linux
			 * parity for a submission that never reached the
			 * HC; a resubmit from inside this callback hits
			 * the kill barrier */
			urb->status = -ENOENT;
			usbkpi_push(urb, usb_pipein(urb->pipe), false, 0,
			    -ENOENT);
			lkpi_usb_deliver(urb);
			usb_free_urb(urb);	/* the transport reference */
			continue;		/* a rearm may exist */
		}

		if (qs == LKPI_Q_INFLIGHT) {
			unsigned long flags;
			bool is_cur;
			int kret;

			spin_lock_irqsave(&ep->lock, flags);
			is_cur = ep->cur == urb &&
			    urb->_qstate == LKPI_Q_INFLIGHT;
			spin_unlock_irqrestore(&ep->lock, flags);
			if (is_cur) {
				/* the native two-phase kill, OUTSIDE the
				 * ep lock (the command path sleeps).  Its
				 * inline completion claims through the
				 * normal IRQ path - no second delivery is
				 * manufactured here.  A nonzero return
				 * means claimed but NOT on the HC (the arm
				 * window): the done path delivers it. */
				kret = usbh_kill_urb(&ep->be);
				if (kret == 0)
					continue;	/* now DONE; the
							   wait below drains */
			}
			/* fall through to the bounded wait */
		}

		/* DONE / CALLBACK: the drain thread owns the delivery -
		 * wait for it instead of duplicating it.  The bounded wait
		 * gives UP on timeout (latched evidence) rather than
		 * spinning forever. */
		{
			unsigned int w = 0;
			unsigned long flags;

			for (;;) {
				u8 q2;

				spin_lock_irqsave(&ep->lock, flags);
				q2 = urb->_qstate;
				spin_unlock_irqrestore(&ep->lock, flags);
				if (q2 == LKPI_Q_IDLE ||
				    q2 == LKPI_Q_QUEUED)
					break;		/* delivered (a rearm
							   re-queued it) */
				if (++w > 500) {
					lkpi_vio_latch(
					    &lkpi_vio_kill_wait_timeout,
					    "kill wait timeout",
					    urb->_alloc_id, q2);
					return;	/* give up, evidenced */
				}
				lkpi_msleep(1);
			}
			continue;	/* re-examine (a rearm may exist) */
		}
	}
}

/* protocol rules 4-7: barrier first, cancel by actual ownership,
 * quiesce before returning, never a second delivery. */
void
usb_kill_urb(struct urb *urb)
{

	if (urb == NULL)
		return;
	/* same content-free guard as submit: killing a dead urb would walk
	 * (and wait on) fields of a recycled block */
	if (urb->_alloc_id == LKPI_URB_ID_POISON) {
		lkpi_probe_urb_submit_dead++;
		if (lkpi_probe_urb_submit_dead == 1)
			lkpi_printf("usbkpi: PROBE kill of DEAD urb %p "
			    "(ra=%p) refused\n", (void *)urb,
			    __builtin_return_address(0));
		return;
	}
	/* the killer pins the urb for the whole operation: the delivery
	 * chain (drain's unanchor + transport-reference drop) may free it
	 * mid-kill, and this loop reads urb state after every stage */
	usb_get_urb(urb);
	urb->_kill_barrier = 1;		/* rule 4: barrier before examining */
	lkpi_usb_kill_quiesce(urb);
	urb->_kill_barrier = 0;		/* rule 7: quiesced, drop it */
	usb_put_urb(urb);		/* the killer's pin */
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
	unsigned long flags;

	/* head-unlink loop: each urb is removed from the anchor under the
	 * lock and killed OUTSIDE it, so no iterator ever spans the
	 * unlock+kill window (the old list_for_each_safe shape, codex
	 * answer-10 bug #3) and a concurrently re-anchored urb is picked
	 * up by the next pass */
	for (;;) {
		struct urb *urb;

		spin_lock_irqsave(&anchor->lock, flags);
		anchor->killed = 1;
		urb = list_empty(&anchor->urb_list) ? NULL :
		    list_entry(anchor->urb_list.next, struct urb,
		    anchor_list);
		if (urb != NULL) {
			list_del(&urb->anchor_list);
			urb->anchor = NULL;
		}
		spin_unlock_irqrestore(&anchor->lock, flags);
		if (urb == NULL)
			return;
		usb_kill_urb(urb);
		usb_put_urb(urb);	/* the anchor reference */
	}
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
	u64 c0 = 0;
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

#ifdef CONFIG_RTW89_EP0_TIMELINE
	/* [W2/rtw89 forensics] EP0 call accounting: the driver's vendor
	 * register path (0x05 requests) fires back-to-back reads inside
	 * read_poll_timeout_atomic, and a wedge here (xHCI EP state dcs=0)
	 * shows as single calls costing the full 500 ms HC timeout.  Bounded
	 * prints: slow (> 20 ms) and failed calls, first 48 each; counters
	 * are readable any time from the shell (`usbkpi` report path). */
	c0 = lkpi_usb_cntvct();
#endif

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

#ifdef CONFIG_RTW89_EP0_TIMELINE
	{
		static u32 e0_calls, e0_slow, e0_fail, e0_errs;
		u32 us = lkpi_usb_elapsed_us(c0);

		e0_calls++;
		usbkpi_ctrl_total = e0_calls;
		usbkpi_ctrl_last_val = value;
		usbkpi_ctrl_last_idx = index;
		usbkpi_ctrl_last_len = size;
		usbkpi_ctrl_last_ret = (unsigned int)ret;
		usbkpi_ctrl_last_us = us;
		if (ret < 0) {
			if (e0_fail++ < 48u)
				lkpi_printf("usbkpi: ep0 fail #%u us=%u req=%02x"
				    " val=%04x idx=%04x len=%u ret=%d total_calls=%u\n",
				    (unsigned)e0_fail, (unsigned)us, request,
				    value, index, (unsigned)size, ret,
				    (unsigned)e0_calls);
		} else if (us >= 20000u) {
			/* a healthy vendor req is < 200 us on this board */
			if (e0_slow++ < 48u)
				lkpi_printf("usbkpi: ep0 slow #%u us=%u req=%02x"
				    " val=%04x idx=%04x len=%u ret=%d total_calls=%u\n",
				    (unsigned)e0_slow, (unsigned)us, request,
				    value, index, (unsigned)size, ret,
				    (unsigned)e0_calls);
			/* width sanity: slow READs are the wedge signature */
			(void)e0_errs;
		}
	}
#endif

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
	/* [S9b W3b probe] size anchors for the heap-trail arguments:
	 * read from the built image, never derived from Linux headers by
	 * hand ([lkpi-stabilize] FR-2: no embedded usbh_urb / sub records
	 * any more - the old 296B/304B fusion families are gone). */
	lkpi_printf("usbkpi: sizes sk_buff=%u urb=%u usbh_urb=%u\n",
	    (unsigned)sizeof(struct sk_buff),
	    (unsigned)sizeof(struct urb), (unsigned)sizeof(struct usbh_urb));
}
