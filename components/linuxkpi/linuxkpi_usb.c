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
/* [S5a] W3: the small-slot count is config-given (-DLKPI_SMALL_SLOTS).
 * The wlan inirp arms NR_RECVBUFF bulk-IN urbs that each hold a stage
 * for their whole lifetime (MAX_RECVBUF_SZ=32KiB = LKPI_STAGE_SIZE, so
 * a small slot is exactly an RX holding), plus transient TX/mgmt asks;
 * the udisk lane keeps the P6-c two-plus-two form by default. */
#ifndef	LKPI_SMALL_SLOTS
#define	LKPI_SMALL_SLOTS	2
#endif
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
	 * threads while the drain thread releases slots, and the bool
	 * flags must not tear against that. */
	{
		TX_INTERRUPT_SAVE_AREA

		TX_DISABLE;
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
		TX_RESTORE;
	}
	if (hit == NULL)
		lkpi_printf("usbkpi: stage pool exhausted\n");
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
		TX_INTERRUPT_SAVE_AREA

		TX_DISABLE;
		((struct lkpi_stage_slot *)urb->_stage_raw)->used = false;
		TX_RESTORE;
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

/* [S5b-W3] stall probe (read by the wlanmon thread in os/threadx/mon.c):
 * the in-flight control transfer and sync submit windows.  The up-stall
 * left the console silent after the cts2self handler print - this trail
 * names the exact call in flight (or none) at the moment of the dump.
 * Single volatile snapshot per window (ctrl on ep0 is serialized by the
 * hubport mutex; a colliding sync bulk submit can only blur the bulk
 * pair, the n counter still shows progress).  A zero tick = idle. */
static volatile u32 lkpi_usb_ctrl_ctx_tick;	/* 0 = no ctrl in flight */
static volatile u8 lkpi_usb_ctrl_ctx_req;
static volatile u8 lkpi_usb_ctrl_ctx_type;
static volatile u16 lkpi_usb_ctrl_ctx_val;
static volatile u16 lkpi_usb_ctrl_ctx_idx;
static volatile u16 lkpi_usb_ctrl_ctx_len;
static volatile u32 lkpi_usb_ctrl_ctx_n;	/* completed ctrl calls */
static volatile u32 lkpi_usb_sync_ctx_tick;	/* 0 = no sync submit */
static volatile u8 lkpi_usb_sync_ctx_ep;
static volatile u32 lkpi_usb_sync_ctx_len;
static volatile u32 lkpi_usb_sync_ctx_n;	/* completed sync submits */

void
wlan_mon_extra_probe(void)
{

	lkpi_printf("MON kpi ctrlstage=%u ctrln=%u syncn=%u\n",
	    lkpi_usb_ctrl_stage, lkpi_usb_ctrl_ctx_n, lkpi_usb_sync_ctx_n);
	if (lkpi_usb_ctrl_ctx_tick != 0)
		lkpi_printf("MON ctrl t=%u in-flight req=%02x type=%02x "
		    "val=%04x idx=%04x len=%u\n",
		    (unsigned)lkpi_usb_ctrl_ctx_tick,
		    (unsigned)lkpi_usb_ctrl_ctx_req,
		    (unsigned)lkpi_usb_ctrl_ctx_type,
		    (unsigned)lkpi_usb_ctrl_ctx_val,
		    (unsigned)lkpi_usb_ctrl_ctx_idx,
		    (unsigned)lkpi_usb_ctrl_ctx_len);
	if (lkpi_usb_sync_ctx_tick != 0)
		lkpi_printf("MON sync t=%u in-flight ep%02x len=%u\n",
		    (unsigned)lkpi_usb_sync_ctx_tick,
		    (unsigned)lkpi_usb_sync_ctx_ep,
		    (unsigned)lkpi_usb_sync_ctx_len);
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
/* [S5b-W3] sync cost sampler: per-sync-submit elapsed (queue+wire+wait,
 * everything under usbh_submit_urb) accumulated per ep0/non-ep0 bucket,
 * printed every 64 completions - the fwdl chunk pacing hunt. 1 ms ticks. */
static struct {
	unsigned long n, sum_ms, max_ms;
	unsigned char max_ep;
} lkpi_sync_cost[2];
static void
lkpi_sync_cost_sample(unsigned char ep, unsigned long ms)
{
	struct lkpi_sync_cost_one { unsigned long n, sum_ms, max_ms; };
	int b = (ep & 0x0fU) == 0U ? 0 : 1;

	lkpi_sync_cost[b].n++;
	lkpi_sync_cost[b].sum_ms += ms;
	if (ms > lkpi_sync_cost[b].max_ms) {
		lkpi_sync_cost[b].max_ms = ms;
		lkpi_sync_cost[b].max_ep = ep;
	}
	if ((lkpi_sync_cost[b].n & 0x3FU) == 0U)
		lkpi_printf("usbkpi: sync cost %s n=%lu avg=%lums max=%lums ep%02x\n",
		    b == 0 ? "ctrl" : "bulk",
		    lkpi_sync_cost[b].n,
		    lkpi_sync_cost[b].sum_ms / lkpi_sync_cost[b].n,
		    lkpi_sync_cost[b].max_ms, lkpi_sync_cost[b].max_ep);
}

/* [W4] sync rejection sampler: the sync failure path prints its first 8
 * hits and every 64th after that (the arm-retry shapes can legitimately
 * hammer it; the per-miss cost stays bounded) */
static unsigned int lkpi_usb_sync_fail_n;
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
/* [S5a] W3: bulk IN rides the async done-list chain (the int shape,
 * board-proven H1/H2).  Sync-only is the udisk BOT contract, not a
 * universal law: the wlan inirp arms NR_RECVBUFF bulk-IN urbs that
 * wait for the device's own moment (beacons) - a 5s sync round per
 * arm would stall ndo_open ~40s and never deliver a beacon, so the
 * scan table would stay empty.  Bulk OUT stays sync in this lane
 * (fw download + probe requests are fire-and-collect); async TX is a
 * W5 data-plane question.  No deadlock against the sync lane: a sync
 * submitter sleeps inside usbh_submit_urb on cherryusb's own waitsem
 * (completion straight from the IRQ, complete is NULL), so a drain-
 * context RX re-submit only ever queues behind one in-flight OUT. */
#ifdef CONFIG_LINUXKPI_USB_RX_ASYNC
static bool lkpi_usb_rx_async = true;
#else
static bool lkpi_usb_rx_async;
#endif

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

static void lkpi_rx_ep_kick(struct usb_endpoint_descriptor *ep);

static void
lkpi_usb_drain_one(struct urb *urb)
{

	/* move the stage's received bytes home before anything can observe
	 * the completion (the usbkpi record in the IRQ callback already
	 * read the stage) */
	if (urb->_stage != NULL && urb->status == 0 && usb_pipein(urb->pipe) &&
	    urb->actual_length != 0 && urb->transfer_buffer != NULL)
		memcpy(urb->transfer_buffer, urb->_stage, urb->actual_length);
	/* [W4] async RX: the stage slot returns the moment its bytes are
	 * home.  The vendor's completion callback (through deliver below)
	 * rearms the urb for the next frame and asks for a stage again in
	 * the submit path - releasing first keeps only the actually
	 * in-flight urbs holding slots instead of every urb that ever ran
	 * (without this the 8 RX urbs pinned 8 slots from their first
	 * completion on, and the next chip re-init ran out). */
	if (urb->_stage != NULL && usb_pipein(urb->pipe))
		lkpi_usb_stage_put(urb);

	lkpi_usb_deliver(urb);
	/* [W4] the endpoint's TD slot is free again: hand it to the next
	 * parked bulk-IN urb (the vendor's completion above resubmitted its
	 * own urb into the same queue, so this also serves the rearm). */
	lkpi_rx_ep_kick(((struct usbh_urb *)urb->_usbh_urb)->ep);
	/* the HC reference: the urb may go away here if the caller already
	 * dropped its own reference (rtw88's submit-then-free pattern) */
	usb_free_urb(urb);
}

/* ------------------------------------------------------------------ */
/* [W4] bulk-IN submit serialization                                   */
/*                                                                     */
/* The cherryusb xHCI port holds ONE TD per endpoint (ring->td.urb is  */
/* the arbiter, every submit to a busy ring returns -USB_ERR_BUSY).    */
/* The vendor's RX path arms NR_RECVBUFF=8 bulk-IN urbs in one go and  */
/* treats the first failure as fatal (rtl8821cu_inirp_init: _FAIL and  */
/* no more urbs) - so on this stack the 2nd..8th arm came back -BUSY,  */
/* the loop aborted, and RX ended with a single armed urb whose        */
/* completion bookkeeping never matched a fresh TD.  The kpi layer now */
/* keeps the contract: one bulk-IN urb per endpoint rides the HC, the  */
/* rest park in a small FIFO and are re-issued as each completion      */
/* lands (drain-thread context, after the vendor's completion callback */
/* had its chance to resubmit - which re-enters this same queue).      */
/* ------------------------------------------------------------------ */

#define	LKPI_RX_EP_N	6
#define	LKPI_RX_EP_QLEN	8

struct lkpi_rx_ep {
	struct usb_endpoint_descriptor *ep;	/* key: the cherryusb endpoint */
	u8		ep_addr;
	bool		busy;		/* one urb of this ep is on the HC */
	unsigned int	n;
	struct urb	*q[LKPI_RX_EP_QLEN];
};

static struct lkpi_rx_ep lkpi_rx_eps[LKPI_RX_EP_N];
static unsigned int lkpi_rx_park_n;

static struct lkpi_rx_ep *
lkpi_rx_ep_find(struct usb_endpoint_descriptor *ep, bool alloc)
{
	struct lkpi_rx_ep *slot = NULL;
	unsigned int i;

	for (i = 0; i < LKPI_RX_EP_N; i++) {
		if (lkpi_rx_eps[i].ep == ep)
			return &lkpi_rx_eps[i];
		if (slot == NULL && lkpi_rx_eps[i].ep == NULL &&
		    !lkpi_rx_eps[i].busy && lkpi_rx_eps[i].n == 0)
			slot = &lkpi_rx_eps[i];
	}
	if (alloc && slot != NULL) {
		slot->ep = ep;
		slot->ep_addr = ep->bEndpointAddress;
	}
	return slot;
}

void
usbkpi_rxq_report(void)
{
	struct {
		struct usb_endpoint_descriptor *ep;
		struct urb *head;
		unsigned int n;
		u8 ep_addr;
		bool busy;
	} snapshot[LKPI_RX_EP_N];
	unsigned int parked, i;

	{
		TX_INTERRUPT_SAVE_AREA

		TX_DISABLE;
		for (i = 0; i < LKPI_RX_EP_N; i++) {
			snapshot[i].ep = lkpi_rx_eps[i].ep;
			snapshot[i].head = lkpi_rx_eps[i].n != 0 ?
			    lkpi_rx_eps[i].q[0] : NULL;
			snapshot[i].n = lkpi_rx_eps[i].n;
			snapshot[i].ep_addr = lkpi_rx_eps[i].ep_addr;
			snapshot[i].busy = lkpi_rx_eps[i].busy;
		}
		parked = lkpi_rx_park_n;
		TX_RESTORE;
	}

	lkpi_printf("usbkpi: rxq parked_total=%u\n", parked);
	for (i = 0; i < LKPI_RX_EP_N; i++) {
		if (snapshot[i].ep == NULL)
			continue;
		lkpi_printf("usbkpi: rxq ep%02x desc=%p busy=%u queued=%u head=%p\n",
		    snapshot[i].ep_addr, (void *)snapshot[i].ep,
		    snapshot[i].busy ? 1U : 0U, snapshot[i].n,
		    (void *)snapshot[i].head);
	}
}

/* completion of the in-flight urb (or a kill): re-issue the next parked */
static void
lkpi_rx_ep_kick(struct usb_endpoint_descriptor *ep)
{
	struct lkpi_rx_ep *rq = lkpi_rx_ep_find(ep, false);

	if (rq == NULL || rq->busy == false)
		return;
	rq->busy = false;
	while (rq->n > 0) {
		struct urb *next = rq->q[0];
		struct usbh_urb *hurb = next->_usbh_urb;
		void *stage;
		int ret;

		memmove(&rq->q[0], &rq->q[1],
		    (rq->n - 1U) * sizeof(rq->q[0]));
		rq->n--;
		/* the parked arm was never fitted for the HC (the park sits
		 * before the stage grant): grant now, or fail it cleanly */
		stage = lkpi_usb_stage_get(next, next->transfer_buffer_length);
		if (stage == NULL) {
			rq->busy = false;
			next->submitted = false;
			next->status = -ENOMEM;
			usbkpi_push(next, usb_pipein(next->pipe), false, 0,
			    -12);
			lkpi_usb_deliver(next);
			usb_free_urb(next);
			continue;
		}
		hurb->transfer_buffer = stage;
		hurb->transfer_buffer_length = next->transfer_buffer_length;
		hurb->timeout = 0U;	/* async arm */
		hurb->complete = lkpi_usb_complete_cb;
		hurb->arg = next;
		next->submitted = true;
		rq->busy = true;
		usbkpi_push(next, usb_pipein(next->pipe), true,
		    next->transfer_buffer_length, 0);
		/* [S5b-W3] the submit runs OUTSIDE the mutex (see the main
		 * submit path): usbh_submit_urb sleeps inside for sync
		 * transfers, and a kick holding this lock while sleeping
		 * wedges the drain thread behind any sync submitter */
		ret = usbh_submit_urb(hurb);
		if (ret == 0)
			return;	/* in flight again */
		/* re-issue failed (kill/device gone): deliver the error */
		rq->busy = false;
		next->submitted = false;
		next->status = -EIO;
		usbkpi_push(next, usb_pipein(next->pipe), false, 0,
		    hurb->errorcode);
		lkpi_usb_deliver(next);
		usb_free_urb(next);	/* the queued arm's HC reference */
	}
}

/* a kill must also pull the urb out of the parked FIFO: the generic
 * kill path below delivers -ENOENT for it and the queue must not
 * re-issue a retired urb */
static void
lkpi_rx_unpark(struct usb_endpoint_descriptor *ep, struct urb *urb)
{
	struct lkpi_rx_ep *rq = lkpi_rx_ep_find(ep, false);
	unsigned int i;

	if (rq == NULL)
		return;
	for (i = 0; i < rq->n; i++) {
		if (rq->q[i] != urb)
			continue;
		memmove(&rq->q[i], &rq->q[i + 1],
		    (rq->n - i - 1U) * sizeof(rq->q[0]));
		rq->n--;
		return;
	}
}

static void *lkpi_usb_drain_thread_entry(void *arg);

static void *
lkpi_usb_drain_thread_entry(void *arg)
{
	struct urb *urb;

	(void)arg;
	for (;;) {
		usb_osal_sem_take(lkpi_usb_done_sem, TX_WAIT_FOREVER);

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
	/* [W2] the embedded _done completion is initialized LAZILY at the
	 * first sync submit (thread context by construction), NOT here: an
	 * urb allocated from a non-thread context (timer/ISR callers exist
	 * in the vendor's paths) made the TX semaphore create fail hard. */
	return urb;
}

void
usb_free_urb(struct urb *urb)
{

	if (urb == NULL)
		return;
	if (lkpi_usb_trace)
		lkpi_printf("usbkpi: free_urb %p refs %d\n", (void *)urb,
		    urb->_refs);
	if (--urb->_refs > 0)
		return;
	/* [W2] release the lazily-created completion shells: without this
	 * every sync urb leaked an osMutex and a TX semaphore on the
	 * created-object lists */
	lkpi_completion_deinit(&urb->_done);
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
	 * so the one-urb-in-flight contract holds by construction.
	 * [S5a] W3: bulk IN rides the same async chain (the wlan RX urbs
	 * wait for the chip's own moment - beacons); bulk OUT stays sync
	 * in this lane (fw download + probe requests are fire-and-collect,
	 * see the flag comment above). */
	bool async = (type == USB_ENDPOINT_XFER_INT) ||
	    (lkpi_usb_rx_async && type == USB_ENDPOINT_XFER_BULK &&
	    usb_pipein(urb->pipe)) || !lkpi_usb_force_sync;
	struct lkpi_rx_ep *rxq = NULL;
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
	/* [W4] bulk single-flight, placed BEFORE the stage grant: the
	 * xHCI port arbites one TD per endpoint and the vendor arms eight
	 * urbs per endpoint, so every arm after the first parks here - and
	 * a parked urb must not pin a stage slot (it rides no DMA).  The
	 * completion kick re-enters this function for the next one. */
	if (async && type == USB_ENDPOINT_XFER_BULK) {
		rxq = lkpi_rx_ep_find(hurb->ep, true);
		if (rxq != NULL && rxq->busy) {
			if (rxq->n < LKPI_RX_EP_QLEN) {
				rxq->q[rxq->n++] = urb;
				lkpi_rx_park_n++;
				/* the queued arm owns the same lifecycle a
				 * submitted one does: mark it in flight (the
				 * kill guard and the vendor's own resubmit
				 * gate key on it) and take the HC reference
				 * the drain/kick-failure paths release */
				urb->submitted = true;
				urb->_refs++;
				if (lkpi_rx_park_n <= 4U)
					lkpi_printf("usbkpi: rxep park ep%02x "
					    "n=%u (parked %u)\n",
					    usb_pipeendpoint(urb->pipe) | 0x80,
					    rxq->n, lkpi_rx_park_n);
				lkpi_mutex_unlock(&lkpi_usb_submit_mtx);
				return 0;
			}
			rxq = NULL;	/* FIFO full: fall through, let the HC talk */
		} else if (rxq != NULL) {
			rxq->busy = true;
		}
	}
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

	/* [W2] lazy completion init: the sync path is the only consumer of
	 * _done and the submitter is a thread by construction */
	if (!async)
		lkpi_completion_init(&urb->_done);

	urb->status = 0;
	urb->actual_length = 0;
	urb->submitted = true;
	usbkpi_push(urb, usb_pipein(urb->pipe), true,
	    urb->transfer_buffer_length, 0);
	/* [S5b-W3] release the mutex BEFORE the submit: usbh_submit_urb
	 * sleeps inside for the sync shape (port sem up to hurb->timeout,
	 * 5 s), and holding lkpi_usb_submit_mtx across that sleep made
	 * every other submitter - the ep0 vendor-control register pollers
	 * (SER/C2HREG) and the drain thread's rx re-arm kick - queue behind
	 * the sleeper.  Board fwdl paced at ~15 ms per 2 KiB bulk chunk
	 * behind the control polls (5 s for a download Linux does in
	 * <1 s) and the drain thread wedged 5 s per contended kick.  The
	 * mutex protects the park FIFO + stage pool + hurb fill only; each
	 * struct urb owns its embedded usbh_urb (usb_alloc_urb), so the
	 * submit itself races nothing the port does not already serialize
	 * internally (per-ring TD arbitration under its critical
	 * sections). */
	lkpi_mutex_unlock(&lkpi_usb_submit_mtx);
	{
		unsigned long t0 = tx_time_get();

		if (!async) {
			/* [S5b-W3] sync-submit trail (see wlan_mon_extra_probe) */
			lkpi_usb_sync_ctx_ep = usb_pipeendpoint(urb->pipe) |
			    (usb_pipein(urb->pipe) ? 0x80U : 0U);
			lkpi_usb_sync_ctx_len = urb->transfer_buffer_length;
			lkpi_usb_sync_ctx_tick = (u32)t0;
		}
		ret = usbh_submit_urb(hurb);
		if (!async)
			lkpi_sync_cost_sample(
			    usb_pipeendpoint(urb->pipe) |
			    (usb_pipein(urb->pipe) ? 0x80U : 0U),
			    tx_time_get() - t0);
		if (!async) {
			lkpi_usb_sync_ctx_tick = 0;
			lkpi_usb_sync_ctx_n++;
		}
	}
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
		/* [W4] the transfer is over the moment the sync submit
		 * returns: the HC consumed the OUT bytes (or the copy-back
		 * above moved the IN bytes home), so the stage slot goes
		 * back.  Without this, every vendor xmitbuf urb - they live
		 * for the adapter's whole life (rtw_os_xmit_resource_* are
		 * probe/detach-tier) - pinned one slot on its first submit
		 * forever, and a scan's probe-request burst drained the
		 * pool: "stage pool exhausted" -> usb_write_port status=-12
		 * for every frame from then on (W4 board: the first survey
		 * returned zero BSS because its own probe requests were
		 * dropped).  Async keeps its stage: the drain thread reads
		 * it at delivery time. */
		if (urb->_stage != NULL)
			lkpi_usb_stage_put(urb);
		return urb->status;
	}
	if (!async && ret != 0) {
		/* the HC rejected or killed the transfer: no completion
		 * callback exists (complete is NULL), same no-reference
		 * accounting rule.
		 *
		 * [W4] do NOT map hurb->errorcode alone: the xhci port
		 * leaves it at the -USB_ERR_BUSY sentinel it sets just
		 * before the queue for every PRE-sentinel rejection
		 * (Configure-EP failure, ring pool dry, hcd not running,
		 * NOTCONN - usb_hc_xhci.c:1462-1592), so mapping it alone
		 * turned "the controller refused this transfer" into
		 * status 0: a false success with nothing on the wire, no
		 * completion, and a driver buffer that never comes back
		 * (the W3 open-fw-dl stall signature).  The HC's own return
		 * value is the truth; errorcode refines a completed-with-
		 * error transfer. */
		int raw = (hurb->errorcode != 0) ? hurb->errorcode : ret;

		/* [S5b-W3] the rejected/failed sync transfer must return its
		 * stage slot here (the completed path at the top does): a
		 * leaked slot per failed chunk drained the pool across the
		 * vendor's fwdl retry bursts */
		if (urb->_stage != NULL)
			lkpi_usb_stage_put(urb);
		urb->submitted = false;
		urb->status = lkpi_usb_errno(raw);
		/* pair the S record so the dump shows the failure leg */
		usbkpi_push(urb, usb_pipein(urb->pipe), false, 0, raw);
		lkpi_usb_sync_fail_n++;
		if ((lkpi_usb_sync_fail_n <= 8U) ||
		    ((lkpi_usb_sync_fail_n & 0x3FU) == 0U)) {
			/* the dispatcher's own check chain (urb, hport,
			 * hport->bus, bus->hc_driver, submit_urb) is printed
			 * raw: a -2 here means one of those is NULL at the
			 * moment of the call, and this line names which */
			struct usbh_hubport *hp = hurb->hport;
			struct usbh_bus *b = (hp != NULL) ? hp->bus : NULL;
			void *sub = (b != NULL && b->hc_driver != NULL) ?
			    (void *)b->hc_driver->submit_urb : NULL;

			lkpi_printf("usbkpi: sync fail ret=%d err=%d ep%02x "
			    "len%u status=%d n=%u urb=%p hurb=%p hport=%p "
			    "bus=%p submit=%p\n", ret, hurb->errorcode,
			    usb_pipeendpoint(urb->pipe) |
			    (usb_pipein(urb->pipe) ? 0x80 : 0),
			    urb->transfer_buffer_length, urb->status,
			    lkpi_usb_sync_fail_n, (void *)urb, (void *)hurb,
			    (void *)hp, (void *)b, sub);

		}
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
	/* [W4] a parked bulk-IN urb never reached the HC: pull it out of the
	 * FIFO first so the completion kick cannot re-issue a retired urb */
	lkpi_rx_unpark(((struct usbh_urb *)urb->_usbh_urb)->ep, urb);
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
		    urb->status);
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
	/* [S5b-W3] the ctrl trail (see wlan_mon_extra_probe) */
	lkpi_usb_ctrl_ctx_req = request;
	lkpi_usb_ctrl_ctx_type = requesttype;
	lkpi_usb_ctrl_ctx_val = value;
	lkpi_usb_ctrl_ctx_idx = index;
	lkpi_usb_ctrl_ctx_len = size;
	lkpi_usb_ctrl_ctx_tick = (u32)tx_time_get();
	ret = usbh_control_transfer(hport, &setup,
	    size != 0 ? stage : NULL);
	lkpi_usb_ctrl_ctx_tick = 0;
	lkpi_usb_ctrl_ctx_n++;
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
