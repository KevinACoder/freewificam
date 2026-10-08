/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linuxkpi_usb - the Linux USB host API over a narrow backend seam.
 *
 * The engine is the in-tree linuxkpi USB layer (a FreeBSD-shape port of
 * sys/compat/linuxkpi/common/src/linux_usb.c, board-proven through the
 * wireless driver round).  It hands transfers to its controller through
 * lkpi_usb_backend.h only: submit, kill, a synchronous control transfer.  The
 * host mock suite plugs a synthetic controller and a scripted device behind
 * that seam; the target build maps it onto the production controller driver.
 *
 * Contract notes (all load-bearing, unchanged):
 *   - urb completions arrive from the controller's completion context; they
 *     are deferred through a done-list to the drain thread, so the driver's
 *     spinlocks stay passive mutexes and the completion callback never runs
 *     in the submitting context (the Linux __usb_hcd_giveback_urb split).
 *   - a synchronous submit (complete == NULL shape) blocks until its
 *     transfer finished and reports short reads per the transfer flags.
 *   - usb_kill_urb cancels by actual ownership: a queued urb is claimed and
 *     completed -ENOENT inline in the killer's context; an in-flight urb goes
 *     through the backend's two-phase kill, whose single completion claims
 *     through the normal path; kill returns only after the urb quiesced.
 *   - usb_free_urb kills first: a freed urb can no longer have a completion
 *     in flight.  The allocation seam additionally keeps a live-urb registry
 *     and a freed-pointer ring, so a double free or a write through a freed
 *     urb is named and refused instead of corrupting memory (lkpi_usb_mem_*).
 *
 * The diagnostics surface of the board line (capture ring, endpoint ledger,
 * wedge report, vendor-specific knobs) is not part of this build: its
 * consumers live on the board.  What a test observes here is the seam's own
 * counters and the controller model's capture, which is the same separation
 * the container's segment map draws.
 */
#include "os/cmsis2/cmsis2.h"	/* OS primitives: threads, semaphores, the
				 * critical-section extension */
#include "linuxkpi_core.h"

#include <linux/usb.h>
#include "lkpi_usb_backend.h"
#include "linuxkpi_usb.h"

/* ------------------------------------------------------------------ */
/* DMA staging hook (passthrough)                                      */
/* ------------------------------------------------------------------ */
/*
 * The board line stages every transfer through 64-byte-aligned pool memory,
 * because its controller DMAs straight out of the urb pointers.  That is a
 * property of the controller adapter, not of this engine: here the seam hands
 * the caller's buffer through, and this hook keeps the place where an adapter
 * that needs alignment does its work (the board adapter returns to this point
 * in its own round; the fixed-slot pool and its sizing went with the
 * controller-specific diagnosis).  What still happens on every crossing is a
 * grant count, so a test can see the engine reached the hook.  Passthrough
 * plus count - no data movement, no allocation.
 */
static u32 lkpi_usb_stage_grants;

unsigned int
lkpi_usb_stage_grant_count(void)
{

	return lkpi_usb_stage_grants;
}

static void *
lkpi_usb_stage_get(struct urb *urb, u32 len)
{

	if (len == 0)
		return NULL;
	lkpi_usb_stage_grants++;
	return urb->transfer_buffer;
}

static void
lkpi_usb_stage_put(struct urb *urb)
{

	(void)urb;
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

/* how many drivers are registered (the bring-up and its tests read this) */
unsigned int
lkpi_usb_driver_count_get(void)
{

	return lkpi_usb_driver_count;
}

/*
 * The Linux module registration macro (linuxkpi_core.h:1535-1544) puts each
 * registered driver's address into the `.lkpi_module` section, and this is the
 * walker that turns the section back into usb_register() calls: the linker
 * script brackets the section (arch/aarch64/baremetal.ld), a host build gets the
 * same brackets from its own two anchor objects (the host suite's Makefile
 * places them around the driver objects). The walk is the Linux kernel's own
 * `module_usb_driver` shape: every entry is one driver, registered in section
 * order, and the USB domain's start follows.
 *
 * The bracket symbols are weak on purpose: a build that links the engine without
 * a module section (the mock suites that register their drivers by hand) keeps
 * working, and says so - a silent zero-driver start would hide a missing bracket
 * on a build that does carry drivers.
 */
extern void *__lkpi_module_start[] __attribute__((weak));
extern void *__lkpi_module_end[] __attribute__((weak));

void
lkpi_usb_init(void)
{
	void *const *it;
	unsigned int n = 0;

	/* the bracket comparison is arithmetic on the addresses, never a
	 * relational operator on two distinct arrays */
	if (__lkpi_module_start != NULL && __lkpi_module_end != NULL &&
	    (uintptr_t)__lkpi_module_end > (uintptr_t)__lkpi_module_start) {
		for (it = __lkpi_module_start; it < __lkpi_module_end; it++) {
			if (*it == NULL)
				continue;
			usb_register((struct usb_driver *)*it);
			n++;
		}
		lkpi_printf("usbkpi: module section walk registered %u driver(s)\n",
		    n);
	} else {
		lkpi_printf("usbkpi: no .lkpi_module bracket in this link; "
		    "drivers must be registered explicitly\n");
	}
	lkpi_usb_start();
}

/*
 * The diagnostic level the class hook reads when it prints a probe's bus (a
 * port-layer build may raise it; the host and board builds leave it at the
 * default and the driver's own debug parameter drives the noise).
 */
int
lkpi_usb_debug_level(void)
{
	static int level;

	return level;
}

/* ------------------------------------------------------------------ */
/* the per-endpoint engine                                              */
/*                                                                     */
/* FreeBSD-shape port of linux_usb.c @d5c09e32c (BSD-2): every async   */
/* endpoint owns ONE backend transfer (the seam's lkpi_usb_be_urb, the */
/* FreeBSD usb_xfer analogue) plus a FIFO of Linux URBs; the           */
/* completion claims the in-flight urb and defers it to the drain      */
/* thread, which publishes the result, runs the driver callback and    */
/* immediately re-arms the next queued urb (the source's               */
/* non_isoc_callback SETUP->TRANSFERRED fall-through).  usb_free_urb   */
/* kills first (source :1214-1228) - a freed urb can no longer have a  */
/* completion in flight, which is the structural fix for the freed-urb */
/* zero-write family.  Function-level lineage and the adaptation log   */
/* are in PROVENANCE.md and DIVERGENCES.md of this component.          */
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
	void			*key;	/* backend endpoint (or &hport->ep0) */
	u8			ep_addr;
	struct lkpi_usb_be_urb be;	/* THE backend urb (one in flight) */
	struct urb		*cur;	/* the in-flight urb, NULL when idle */
	struct urb		*q_head, *q_tail;
	unsigned int		qlen;
	/* single-slot done handoff (one in flight => one pending) */
	struct urb		*done_urb;
	int			done_status;
	u32			done_len;
	struct lkpi_usb_ep	*_done_next;	/* done-list linkage */
	u32			enqueued, completions, kicks, kills;
};

static struct lkpi_usb_ep lkpi_eps[LKPI_EP_N];
static u32 lkpi_alloc_seq;		/* urb alloc identity source */

/* first-violation latches: the FIRST hit prints one compact evidence
 * line and every hit counts */
static u32 lkpi_vio_dup_claim, lkpi_vio_submit_live, lkpi_vio_free_live,
	   lkpi_vio_kill_wait_timeout, lkpi_vio_busy_reject,
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
static osSemaphoreId_t lkpi_usb_done_sem;
static bool lkpi_usb_done_ready;
static osThreadId_t lkpi_usb_drain_thread;
/* bring-up trace: every transfer crossed, caller-gated */
static bool lkpi_usb_trace;
static unsigned int lkpi_usb_trace_n;
/* sync-submit is the shape for the block-storage lane; drivers that ride
 * completion callbacks (the wireless lines) flip it off at bring-up. */
static bool lkpi_usb_force_sync = true;

void lkpi_usb_force_sync_set(bool on)
{

	lkpi_usb_force_sync = on;
	lkpi_printf("usb: sync-submit %s\n", on ? "on" : "off");
}

static int
lkpi_usb_errno(int be_err)
{

	/* backend error codes come back negative of the LKPI_USB_BE_ERR_* values */
	switch (-be_err) {
	case 0:
		return 0;
	case LKPI_USB_BE_ERR_TIMEOUT:
		return -ETIMEDOUT;
	case LKPI_USB_BE_ERR_NOTCONN:
	case LKPI_USB_BE_ERR_SHUTDOWN:
	case LKPI_USB_BE_ERR_NODEV:
		return -ENODEV;
	case LKPI_USB_BE_ERR_STALL:
		return -EPIPE;
	case LKPI_USB_BE_ERR_NOMEM:
		return -ENOMEM;
	case LKPI_USB_BE_ERR_SHORT:
		return -EREMOTEIO;
	case LKPI_USB_BE_ERR_CANCELLED:
		return -ENOENT;
	default:
		return -EIO;
	}
}

/* ------------------------------------------------------------------ */
/* the sync-lane backend pool                                          */
/* ------------------------------------------------------------------ */

/* The synchronous lane (complete==NULL: the submit blocks in the backend
 * until its transfer finished) needs a backend urb per in-flight sync
 * transfer.  The old shape embedded one in every struct urb; the engine
 * moved the async traffic to per-endpoint backends and this static 4-deep
 * pool serves the sync lane - a freed urb never carries a backend urb, so
 * a stale completion has no write target on the heap here either. */
#define	LKPI_SYNC_BE_N	4
static struct lkpi_usb_be_urb lkpi_sync_be[LKPI_SYNC_BE_N];
static bool lkpi_sync_be_used[LKPI_SYNC_BE_N];

static struct lkpi_usb_be_urb *
lkpi_sync_be_take(void)
{
	struct lkpi_usb_be_urb *be = NULL;
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
lkpi_sync_be_put(struct lkpi_usb_be_urb *be)
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

static struct lkpi_usb_be_hubport *lkpi_usb_hport(struct usb_device *dev);

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
	spin_unlock_irqrestore(&ep->lock, flags);

	/* done list (interrupt-guarded): append the ENDPOINT, wake the drain */
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
	(void)osSemaphoreRelease(lkpi_usb_done_sem);
}

static void
lkpi_usb_deliver(struct urb *urb)
{

	/* the giveback contract (Linux __usb_hcd_giveback_urb, hcd.c:1645):
	 * the anchor's reference is dropped BEFORE the completion callback
	 * runs.  Without it a completed anchored urb kept refs >= 1 forever
	 * and the drain-side usb_free_urb never reached 0 - anchored TX
	 * frames then pinned their buffers without bound.  Idempotent - the
	 * kill paths clear urb->anchor before calling here. */
	usb_unanchor_urb(urb);
	if (urb->complete != NULL)
		urb->complete(urb);
}

static void
lkpi_usb_drain_one(struct lkpi_usb_ep *ep)
{
	struct urb *urb;
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
	urb->_qstate = LKPI_Q_CALLBACK;
	spin_unlock_irqrestore(&ep->lock, flags);

	/* publish the captured result into the driver-visible fields; the
	 * received bytes are already in the caller's buffer (the seam moves
	 * data in place), so only an over-long report needs clamping */
	urb->status = st;
	urb->actual_length = len;
	if (urb->status == 0 && usb_pipein(urb->pipe) &&
	    urb->actual_length > urb->transfer_buffer_length) {
		lkpi_printf("lkpi: RX overrun clamp %u -> %u\n",
		    urb->actual_length, urb->transfer_buffer_length);
		urb->actual_length = urb->transfer_buffer_length;
	}

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
}

static void lkpi_usb_drain_thread_entry(void *arg);

static void
lkpi_usb_drain_thread_entry(void *arg)
{
	struct lkpi_usb_ep *ep;

	(void)arg;
	for (;;) {
		(void)osSemaphoreAcquire(lkpi_usb_done_sem, osWaitForever);
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
}

/* fit the claimed urb onto the HC: grant the DMA stage, copy OUT data
 * into it, fill the backend urb (the source's SETUP leg + the arm
 * half of the old lkpi_stream_arm). */
static int
lkpi_usb_arm(struct lkpi_usb_ep *ep, struct urb *urb)
{
	struct lkpi_usb_be_hubport *hport = lkpi_usb_hport(urb->dev);
	unsigned int type = usb_pipetype(urb->pipe);

	if (hport == NULL)
		return -ENODEV;
	memset(&ep->be, 0, sizeof(ep->be));
	ep->be.hport = hport;

	if (type == USB_ENDPOINT_XFER_CONTROL) {
		/* the control ep0 key is the hubport's own ep0; the setup line
		 * stays the caller's 8-byte packet - an adapter that needs it
		 * staged aligned does that behind the seam */
		if (urb->setup_packet == NULL)
			return -EINVAL;
		ep->be.ep = &hport->ep0;
		ep->be.setup = urb->setup_packet;
	} else {
		ep->be.ep = urb->hep->hcpriv;
	}

	if (urb->transfer_buffer_length != 0) {
		void *stage = lkpi_usb_stage_get(urb,
		    urb->transfer_buffer_length);

		/* the hook cannot fail in the passthrough build: a NULL here
		 * is a malformed urb (a nonzero length without a buffer) and
		 * the completion path reports it as -EINVAL */
		if (stage == NULL)
			return -EINVAL;
		ep->be.transfer_buffer = stage;
	} else {
		ep->be.transfer_buffer = NULL;
	}
	ep->be.transfer_buffer_length = urb->transfer_buffer_length;
	ep->be.timeout = 0U;	/* async arm */
	ep->be.complete = lkpi_usb_be_complete;
	ep->be.arg = ep;	/* the ENDPOINT: persistent identity */
	if ((urb->transfer_flags & URB_ZERO_PACKET) != 0)
		ep->be.transfer_flags |= LKPI_USB_BE_URB_ZERO_PACKET;
	if (type == USB_ENDPOINT_XFER_INT) {
		if (urb->dev->speed == USB_SPEED_HIGH ||
		    urb->dev->speed == USB_SPEED_SUPER ||
		    urb->dev->speed == USB_SPEED_SUPER_PLUS)
			ep->be.interval = (u32)urb->interval * 125U;
		else
			ep->be.interval = (u32)urb->interval * 1000U;
	}
	return 0;
}

/* the consumer pump (the source's SETUP leg loop): while the endpoint
 * is idle and the FIFO is not, claim the head and hand it to the
 * backend.  Callers: a producer's submit and the drain thread after a
 * delivery; the ep lock decides which of them wins each claim. */
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
			urb->_qstate = LKPI_Q_QUEUED;
			ep->cur = NULL;
			spin_unlock_irqrestore(&ep->lock, flags);
			return;
		}
		if (ret == 0)
			ret = lkpi_usb_be_submit_urb(&ep->be);
		if (ret == 0)
			return;		/* in flight; the completion claim ends it */

		/* the backend refused a claim we own (device gone / port
		 * shutdown): the submission was accepted, so exactly one
		 * completion is still owed - route it through the done
		 * list and try the next head.  cur is released HERE (no
		 * completion will ever claim it); the driver callback
		 * never runs inline. */
		{
			if (ret == -LKPI_USB_BE_ERR_BUSY)
				lkpi_vio_latch(&lkpi_vio_busy_reject,
				    "backend busy on own claim", urb->_alloc_id, 0);
			lkpi_usb_stage_put(urb);
			spin_lock_irqsave(&ep->lock, flags);
			ep->cur = NULL;
			urb->_qstate = LKPI_Q_DONE;
			ep->done_urb = urb;
			ep->done_status = lkpi_usb_errno(ret);
			ep->done_len = 0;
			spin_unlock_irqrestore(&ep->lock, flags);
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
			(void)osSemaphoreRelease(lkpi_usb_done_sem);
			continue;
		}
	}
}

/* ------------------------------------------------------------------ */
/* urb lifecycle                                                       */
/* ------------------------------------------------------------------ */

static void lkpi_usb_kill_quiesce(struct urb *urb);

/* ------------------------------------------------------------------ */
/* the urb allocation seam (M.PAIR / M.NAMED)                          */
/* ------------------------------------------------------------------ */

/*
 * The allocator keeps two things beyond plain malloc: a live-urb registry
 * (so a free of a pointer the seam never handed out is named instead of
 * crashing the heap) and a ring of the freshly released pointers (so a
 * double free or a write through a stale pointer hits a named, refused
 * path instead of a recycled block).  Both are small and fixed; the ring
 * entry is cleared when a new allocation hands the address out again.
 * Counters are the seam's evidence: a clean run ends with
 * n_urb_alloc == n_urb_free and every named counter zero.
 */

#define	LKPI_URB_LIVE_MAX	64
#define	LKPI_URB_FREED_RING	32

/*
 * Embedded urbs. A driver is allowed to put `struct urb` inside a container of
 * its own, initialise it with usb_init_urb() and release it with usb_free_urb()
 * - Linux's kref contract makes both calls lawful for such an urb, and the
 * RTOS-family drivers use exactly that shape for their TX/RX urb pools (the
 * first consumer embeds 96). Those urbs were never handed out by the seam's
 * allocator, so they live in their own birth registry: usb_init_urb() records
 * the address, usb_free_urb() releases the container through kfree() - the same
 * call Linux's urb_destroy makes - and a pointer in neither registry is still a
 * named stray refusal that never reaches the allocator.
 */
#define	LKPI_URB_EMBEDDED_MAX	256

static struct lkpi_usb_mem_stats lkpi_usb_mem;
static const struct urb *lkpi_usb_live[LKPI_URB_LIVE_MAX];
static const struct urb *lkpi_usb_embedded[LKPI_URB_EMBEDDED_MAX];
static unsigned int lkpi_usb_embedded_n;
static unsigned int lkpi_usb_embedded_full;
static const struct urb *lkpi_usb_freed[LKPI_URB_FREED_RING];
static unsigned int lkpi_usb_freed_head;
static const void *lkpi_usb_first_witness;

void
lkpi_usb_mem_stats_get(struct lkpi_usb_mem_stats *out)
{

	*out = lkpi_usb_mem;
}

const void *
lkpi_usb_mem_first_witness(void)
{

	return lkpi_usb_first_witness;
}

/* returns true when the address sits in the released ring: the caller
 * names and refuses the operation instead of touching the block */
static bool
lkpi_usb_freed_hit(const void *p)
{
	unsigned int i;

	for (i = 0; i < LKPI_URB_FREED_RING; i++) {
		if (lkpi_usb_freed[i] == p) {
			if (lkpi_usb_first_witness == NULL)
				lkpi_usb_first_witness = p;
			return true;
		}
	}
	return false;
}

/* the geometry check the engine performs before touching a freed slot:
 * called by the seam's own negative cases and by the submit/kill guards */
void
lkpi_usb_uaf_hit(const void *p)
{

	lkpi_usb_mem.uaf_write++;
	if (lkpi_usb_first_witness == NULL)
		lkpi_usb_first_witness = p;
}

static void
lkpi_usb_freed_note(const struct urb *urb)
{

	lkpi_usb_freed[lkpi_usb_freed_head %
	    LKPI_URB_FREED_RING] = urb;
	lkpi_usb_freed_head++;
}

/* the birth/death ledger of embedded urbs (see the note above the storage) */
static void
lkpi_usb_embedded_note(const struct urb *urb)
{
	unsigned int i;

	for (i = 0; i < lkpi_usb_embedded_n; i++) {
		if (lkpi_usb_embedded[i] == urb)
			return;		/* re-init of a live one: already known */
	}
	if (lkpi_usb_embedded_n >= LKPI_URB_EMBEDDED_MAX) {
		lkpi_usb_embedded_full++;
		return;
	}
	lkpi_usb_embedded[lkpi_usb_embedded_n++] = urb;
}

static int
lkpi_usb_embedded_find(const struct urb *urb)
{
	unsigned int i;

	for (i = 0; i < lkpi_usb_embedded_n; i++)
		if (lkpi_usb_embedded[i] == urb)
			return (int)i;
	return -1;
}

static void
lkpi_usb_embedded_del(unsigned int idx)
{

	lkpi_usb_embedded[idx] = lkpi_usb_embedded[lkpi_usb_embedded_n - 1];
	lkpi_usb_embedded_n--;
}

/* the address is live again: any stale release record must go (the same rule
 * usb_alloc_urb applies for seam-allocated urbs) */
static void
lkpi_usb_freed_clear(const struct urb *urb)
{
	unsigned int i;

	for (i = 0; i < LKPI_URB_FREED_RING; i++) {
		if (lkpi_usb_freed[i] == urb)
			lkpi_usb_freed[i] = NULL;
	}
}

void *
lkpi_usb_alloc_coherent(size_t size, unsigned int mem_flags)
{
	void *p = kzalloc(size, mem_flags);

	if (p == NULL)
		lkpi_usb_mem.alloc_fail++;
	else
		lkpi_usb_mem.n_coherent_alloc++;
	return p;
}

void
lkpi_usb_free_coherent(void *p, size_t size)
{

	(void)size;
	if (p == NULL)
		return;
	lkpi_usb_mem.n_coherent_free++;
	kfree(p);
}

/* address to index for the freed-pointer ring / 0 when not freed */
int
lkpi_usb_urb_is_freed(const struct urb *urb)
{

	return lkpi_usb_freed_hit(urb) || urb->_alloc_id == LKPI_URB_ID_POISON ?
	    1 : 0;
}

/*
 * usb_init_urb ([LNX] drivers/usb/core/urb.c:44-52): zero the structure and arm
 * its refcount and lists. The driver's urbs are embedded in its own containers
 * (the first consumer's TX/RX urb structs embed `struct urb` first) and
 * usb_free_urb later releases such a container, which is the Linux kref
 * contract this helper's birth note makes auditable: the address enters the
 * embedded registry here, so the release is recognised as lawful instead of
 * being refused as a stray, and a stale release record for a recycled address is
 * cleared (`usb_alloc_urb` does exactly the same for seam-allocated urbs).
 */
void
usb_init_urb(struct urb *urb)
{

	if (urb == NULL)
		return;
	memset(urb, 0, sizeof(*urb));
	urb->_refs = 1;
	INIT_LIST_HEAD(&urb->anchor_list);
	lkpi_usb_freed_clear(urb);
	lkpi_usb_embedded_note(urb);
}

struct urb *
usb_alloc_urb(int iso_packets, gfp_t mem_flags)
{
	struct urb *urb;
	size_t size = sizeof(struct urb) +
	    (iso_packets > 0 ? (size_t)iso_packets * 64 : 0);
	unsigned int i;

	(void)mem_flags;
	urb = kzalloc(size, GFP_KERNEL);
	if (urb == NULL) {
		lkpi_usb_mem.alloc_fail++;
		return NULL;
	}
	urb->_refs = 1;
	urb->_alloc_id = ++lkpi_alloc_seq;
	/* a handed-out address leaves the released ring */
	for (i = 0; i < LKPI_URB_FREED_RING; i++) {
		if (lkpi_usb_freed[i] == urb)
			lkpi_usb_freed[i] = NULL;
	}
	for (i = 0; i < LKPI_URB_LIVE_MAX; i++) {
		if (lkpi_usb_live[i] == NULL) {
			lkpi_usb_live[i] = urb;
			break;
		}
	}
	lkpi_usb_mem.n_urb_alloc++;
	return urb;
}

void
usb_free_urb(struct urb *urb)
{
	bool live_entry_found = false;
	unsigned int i;
	int embedded;

	if (urb == NULL)
		return;
	/* trace-gated: once a receive path flows this fires per completed
	 * frame and swamps the console */
	if (lkpi_usb_trace)
		lkpi_printf("usbkpi: free_urb %p id=%u refs %d\n",
		    (void *)urb, urb->_alloc_id, urb->_refs);
	/* the seam guard: a released pointer (or one this seam never
	 * handed out) is named and refused before any field is read */
	if (lkpi_usb_freed_hit(urb)) {
		lkpi_usb_mem.double_free++;
		(void)lkpi_usb_uaf_hit(urb);
		return;
	}
	embedded = lkpi_usb_embedded_find(urb);
	if (urb->_alloc_id == LKPI_URB_ID_POISON && embedded < 0) {
		lkpi_usb_mem.double_free++;
		return;
	}
	if (__atomic_sub_fetch(&urb->_refs, 1, __ATOMIC_ACQ_REL) > 0)
		return;		/* plain reference drop, not the release */
	/* the FreeBSD free-kills-first discipline (source :1214-1228): a
	 * releasing free while the engine still owns the urb drains the
	 * submission FIRST - the completion paths hold nothing after this
	 * point, so the freed urb has no writer left.  This is the
	 * structural collection of the freed-urb zero-write family. */
	if (urb->_qstate != LKPI_Q_IDLE) {
		lkpi_vio_latch(&lkpi_vio_free_live, "free of live urb",
		    urb->_alloc_id, urb->_qstate);
		lkpi_usb_kill_quiesce(urb);
	}
	/* the per-urb DMA stage this seam may have granted */
	lkpi_usb_stage_put(urb);
	if (embedded >= 0) {
		/*
		 * A container the driver allocated and initialised through
		 * usb_init_urb(): the release is lawful, the container is the
		 * driver's and goes back through the same kfree() Linux's
		 * urb_destroy uses (urb.c:20-28). Taking the address out of the
		 * birth registry is what makes a second release of it a named
		 * refusal rather than a heap double free.
		 */
		lkpi_usb_embedded_del((unsigned int)embedded);
		lkpi_usb_mem.n_urb_free++;
		lkpi_usb_freed_note(urb);
		kfree(urb);
		return;
	}
	for (i = 0; i < LKPI_URB_LIVE_MAX; i++) {
		if (lkpi_usb_live[i] == urb) {
			lkpi_usb_live[i] = NULL;
			live_entry_found = true;
			break;
		}
	}
	if (!live_entry_found)
		lkpi_usb_mem.stray_free++;	/* never handed out by this seam */
	lkpi_usb_mem.n_urb_free++;
	lkpi_usb_freed_note(urb);
	urb->_alloc_id = LKPI_URB_ID_POISON;
	kfree(urb);
}

static struct lkpi_usb_be_hubport *
lkpi_usb_hport(struct usb_device *dev)
{

	return dev != NULL ? (struct lkpi_usb_be_hubport *)dev->_hport : NULL;
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

		if ((unsigned int)usb_endpoint_num(&hep->desc) ==
		    usb_pipeendpoint(pipe) &&
		    usb_endpoint_dir_in(&hep->desc) ==
		    (usb_pipein(pipe) ? 1 : 0))
			return hep;
	}
	return NULL;
}

u16
usb_maxpacket(struct usb_device *udev, int pipe)
{
	struct usb_host_endpoint *hep = lkpi_usb_find_hep(udev,
	    (unsigned int)pipe);

	/* shape-identical to the Linux helper (ep0 answers 64 there via
	 * dev->ep0; here the hubport's ep0 descriptor carries it) */
	if (hep == NULL)
		return 0;
	return usb_endpoint_maxp(&hep->desc);
}

/* the synchronous lane: the submit IS the transfer - the caller blocks
 * with complete == NULL and the backend reports the result on return,
 * so the done list and the drain thread are entirely out of the picture.
 * No reference is taken: the transfer is over by the time the call
 * returns either way. */
static int
lkpi_usb_submit_sync(struct urb *urb, unsigned int type)
{
	struct lkpi_usb_be_hubport *hport = lkpi_usb_hport(urb->dev);
	struct lkpi_usb_be_urb *hurb;
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
	 * (latent first-submit garbage) */
	hurb->hport = hport;
	if (usb_pipetype(urb->pipe) == USB_ENDPOINT_XFER_CONTROL) {
		if (urb->setup_packet == NULL) {
			lkpi_sync_be_put(hurb);
			urb->status = -EINVAL;
			return -EINVAL;
		}
		hurb->ep = &hport->ep0;
		hurb->setup = urb->setup_packet;
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
			urb->status = -EINVAL;
			return -EINVAL;
		}
		hurb->transfer_buffer = stage;
	} else {
		hurb->transfer_buffer = NULL;
	}
	hurb->transfer_buffer_length = urb->transfer_buffer_length;
	hurb->timeout = 5000U;
	/* the NULL completion keeps the done list out of the picture
	 * entirely: the blocking submit reports the result itself */
	hurb->complete = NULL;
	hurb->arg = NULL;
	urb->status = 0;
	urb->actual_length = 0;
	ret = lkpi_usb_be_submit_urb(hurb);
	if (ret == 0) {
		/* the submit returned because the transfer finished: collect
		 * it right here, on the submitter's own (passive) context */
		urb->status = lkpi_usb_errno(hurb->errorcode);
		urb->actual_length = hurb->actual_length;
		usb_unanchor_urb(urb);
		lkpi_sync_be_put(hurb);
		return urb->status;
	}
	/* the backend reported a failure and no completion callback exists */
	urb->status = lkpi_usb_errno(hurb->errorcode != 0 ?
	    hurb->errorcode : ret);
	lkpi_sync_be_put(hurb);
	return urb->status;
}

int
usb_submit_urb(struct urb *urb, gfp_t mem_flags)
{
	struct lkpi_usb_be_hubport *hport = lkpi_usb_hport(urb->dev);
	unsigned int type = usb_pipetype(urb->pipe);
	/* interrupt pipes always take the async chain (an int completion's
	 * moment is the device's; a blocking submit would burn a full
	 * backend timeout round per poll).  Drivers that ride completion
	 * callbacks flip the sync flag off: their resubmit loops and TX
	 * completions key on the callback, and the per-endpoint
	 * single-in-flight engine below supplies the one-transfer-per-
	 * endpoint discipline such a controller demands. */
	bool async = (type == USB_ENDPOINT_XFER_INT) || !lkpi_usb_force_sync;
	struct lkpi_usb_ep *ep;
	void *key;
	unsigned long flags;

	(void)mem_flags;
	if (hport == NULL)
		return -ENODEV;
	/* submit-of-dead-urb guard: a released urb carries the id poison;
	 * refuse before ANY field of it is touched, and name the caller -
	 * this is the stale-pointer writer the allocation seam hunts. */
	if (urb->_alloc_id == LKPI_URB_ID_POISON) {
		lkpi_probe_urb_submit_dead++;
		if (lkpi_probe_urb_submit_dead == 1)
			lkpi_printf("usbkpi: PROBE submit of DEAD urb %p "
			    "(ra=%p) refused\n", (void *)urb,
			    __builtin_return_address(0));
		return -ENODEV;
	}
	if (!async) {
		/* the sync lane: the backend's blocking shape */
		return lkpi_usb_submit_sync(urb, type);
	}
	if (urb->_kill_barrier != 0)
		return -EPERM;	/* rule 4 (kill_count, source :425-428) */

	/* the endpoint identity: control class rides the hubport ep0; the
	 * rest resolve the endpoint EVERY submit - a cached endpoint once
	 * sent one urb refilled with a different pipe down the wrong
	 * direction, because the same urb is reused per protocol leg */
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
	/* the transport reference: taken while the urb is still invisible to
	 * every other context, so the publish-before-reference window that
	 * once produced a use-after-free stays closed by construction */
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

/* serializes the shared control setup line and the control-transfer staging
 * among concurrent callers (initialized in lkpi_usb_start) */
static struct mutex lkpi_usb_ctrl_mtx;

/* internal quiesce (no pin, no barrier): the caller owns or has pinned
 * the urb, and owns the kill-barrier discipline.  Thread context only -
 * the backend's kill may sleep. */
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
			 * controller; a resubmit from inside this callback
			 * hits the kill barrier */
			urb->status = -ENOENT;
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
				/* the backend's two-phase kill, OUTSIDE the
				 * ep lock (it may sleep).  Its completion
				 * claims through the normal path - no second
				 * delivery is manufactured here.  A nonzero
				 * return means claimed but NOT on the
				 * controller (the arm window): the done path
				 * delivers it. */
				kret = lkpi_usb_be_kill_urb(&ep->be);
				if (kret == 0)
					continue;	/* now DONE; the
							   wait below drains */
			}
			/* fall through to the bounded wait */
		}

		/* DONE / CALLBACK: the drain thread owns the delivery -
		 * wait for it instead of duplicating it.  The bounded wait
		 * gives up on timeout (latched evidence) rather than
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
	if (lkpi_usb_freed_hit(urb) || urb->_alloc_id == LKPI_URB_ID_POISON) {
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
/* usb_anchor family (the rtl8xxxu concurrency model; the batch-kill is
 * the semantics the Linux urb.c helper provides)                       */
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
	 * unlock+kill window and a concurrently re-anchored urb is picked
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
	static struct lkpi_usb_setup_packet setup
	    __attribute__((aligned(64)));
	/* the widest control data stage the seam accepts; a larger request is
	 * refused with -ENOMEM rather than truncated */
	static u8 stage[2048] __attribute__((aligned(64)));
	struct lkpi_usb_be_hubport *hport = lkpi_usb_hport(dev);
	bool is_in = (requesttype & USB_DIR_IN) != 0;
	int ret;

	(void)pipe;
	(void)timeout;
	if (hport == NULL)
		return -ENODEV;
	if (size != 0 && data == NULL)
		return -EINVAL;
	if (size > sizeof(stage)) {
		lkpi_printf("usbkpi: control transfer %u bytes exceeds "
		    "staging\n", (unsigned int)size);
		return -ENOMEM;
	}
	/* one setup line per transfer: serialized because concurrent
	 * callers would otherwise fill the shared packet while another
	 * transfer is reading it */
	lkpi_mutex_lock(&lkpi_usb_ctrl_mtx);
	setup.bmRequestType = requesttype;
	setup.bRequest = request;
	setup.wValue = value;
	setup.wIndex = index;
	setup.wLength = size;
	if (!is_in && size != 0)
		memcpy(stage, data, size);
	ret = lkpi_usb_be_control_transfer(hport, &setup,
	    size != 0 ? stage : NULL);
	if (ret == 0 && is_in && size != 0)
		memcpy(data, stage, size);
	lkpi_mutex_unlock(&lkpi_usb_ctrl_mtx);
	if (ret < 0)
		return lkpi_usb_errno(ret);
	/* Linux semantics: the transferred length on success (the seam
	 * reports a short data stage as an error, so a success here moved
	 * everything the caller asked for) */
	return (int)size;
}

int
usb_clear_halt(struct usb_device *dev, int pipe)
{
	static struct lkpi_usb_setup_packet setup
	    __attribute__((aligned(64)));
	struct lkpi_usb_be_hubport *hport = lkpi_usb_hport(dev);
	int ret;

	if (hport == NULL)
		return -ENODEV;
	setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD |
	    USB_RECIP_ENDPOINT;
	setup.bRequest = 0x01;	/* CLEAR_FEATURE(ENDPOINT_HALT) */
	setup.wValue = 0;
	setup.wIndex = (u16)(usb_pipeendpoint(pipe) |
	    (usb_pipein(pipe) ? 0x80 : 0));
	setup.wLength = 0;
	lkpi_mutex_lock(&lkpi_usb_ctrl_mtx);
	ret = lkpi_usb_be_control_transfer(hport, &setup, NULL);
	lkpi_mutex_unlock(&lkpi_usb_ctrl_mtx);
	return ret == 0 ? 0 : lkpi_usb_errno(ret);
}

int
usb_reset_device(struct usb_device *dev)
{

	/* no detach/reattach chain exists in this port yet: answer
	 * honestly instead of a lying 0 - a driver that lands here
	 * recovers by reconnect, and the console line records it */
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

/* ------------------------------------------------------------------ */
/* the backend seam: registration and the three dispatchers            */
/* ------------------------------------------------------------------ */

static const struct lkpi_usb_be_ops *lkpi_usb_be_ops;
static void *lkpi_usb_be_ctx;

void
lkpi_usb_be_register(const struct lkpi_usb_be_ops *ops, void *ctx)
{

	lkpi_usb_be_ops = ops;
	lkpi_usb_be_ctx = ctx;
}

int
lkpi_usb_be_submit_urb(struct lkpi_usb_be_urb *urb)
{

	if (lkpi_usb_be_ops == NULL || lkpi_usb_be_ops->submit_urb == NULL)
		return -LKPI_USB_BE_ERR_NODEV;
	return lkpi_usb_be_ops->submit_urb(lkpi_usb_be_ctx, urb);
}

int
lkpi_usb_be_kill_urb(struct lkpi_usb_be_urb *urb)
{

	if (lkpi_usb_be_ops == NULL || lkpi_usb_be_ops->kill_urb == NULL)
		return -LKPI_USB_BE_ERR_NODEV;
	return lkpi_usb_be_ops->kill_urb(lkpi_usb_be_ctx, urb);
}

int
lkpi_usb_be_control_transfer(struct lkpi_usb_be_hubport *port,
    const struct lkpi_usb_setup_packet *setup, void *buffer)
{

	if (lkpi_usb_be_ops == NULL ||
	    lkpi_usb_be_ops->control_transfer == NULL)
		return -LKPI_USB_BE_ERR_NODEV;
	return lkpi_usb_be_ops->control_transfer(lkpi_usb_be_ctx, port, setup,
	    buffer);
}

void
lkpi_usb_start(void)
{
	osThreadAttr_t attr = { 0 };

	if (lkpi_usb_done_ready)
		return;
	lkpi_mutex_init(&lkpi_usb_ctrl_mtx);
	lkpi_usb_done_sem = osSemaphoreNew(1u, 0u, NULL);
	/* the drain thread runs the driver completion callbacks; its
	 * priority sits below the submitter's so a driver callback cannot
	 * starve the context that queued the work */
	attr.name = "lkpi_usb_drain";
	attr.stack_size = 16384u;
	attr.priority = osPriorityNormal;
	lkpi_usb_drain_thread = osThreadNew(lkpi_usb_drain_thread_entry, NULL,
	    &attr);
	if (lkpi_usb_drain_thread == NULL) {
		lkpi_printf("usbkpi: cannot start the drain thread\n");
		return;
	}
	lkpi_usb_done_ready = true;
	lkpi_printf("usbkpi: %u usb driver(s) registered\n",
	    lkpi_usb_driver_count);
	/* size anchors for the heap-trail arguments: read from the built
	 * image, never derived from another header by hand */
	lkpi_printf("usbkpi: sizes sk_buff=%u urb=%u\n",
	    (unsigned)sizeof(struct sk_buff), (unsigned)sizeof(struct urb));
}
