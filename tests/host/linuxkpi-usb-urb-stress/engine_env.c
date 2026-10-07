/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * engine_env.c - the environment wrapper: controller + device + driver + knobs.
 *
 * The wrapper is the harness's adapter over the backend seam
 * (components/linuxkpi/usb/lkpi_usb_backend.h).  It forwards the engine's three
 * seam calls to the synthetic controller of the sibling host suite and adds the
 * knobs:
 *
 *   submit_urb        a transfer is parked in the wrapper's own pending list
 *                     (with a due step, a programmed error, a delivery order
 *                     slot) or handed straight to the controller when the knob
 *                     says "pass through" (the cancel-the-controller-queue
 *                     cells);
 *   kill_urb          a transfer the wrapper still holds is cancelled and its
 *                     single completion delivered by the killer, exactly like
 *                     the controller's own seam_kill; a transfer already at the
 *                     controller goes to the controller's kill;
 *   control_transfer  the pass-through, plus the never-answering kind that
 *                     answers -LKPI_USB_BE_ERR_TIMEOUT (the -ETIMEDOUT source).
 *
 * Every completion - the controller's, the wrapper's own cancels, the error
 * injections - funnels through wrap_deliver(), which counts the seam delivery,
 * copies the controller's result back into the engine's own backend urb, and
 * then calls the engine's completion.  This is also where the double-completion
 * knob lives: the must-go-red self-proof of the exactly-once rule.
 *
 * Determinism: the controller's worker is held for the whole run
 * (mock_hcd_hold_set), so no transfer completes unless the harness advances its
 * step counter and pumps the controller itself (mock_backend_pump).  The
 * wrapper's own state is protected by a recursive mutex, because the engine's
 * drain thread legitimately re-enters the wrapper (a completion's callback may
 * resubmit, and the engine re-arms the next queued urb from the drain).  The
 * harness waits for the driver callbacks through the completion semaphore, so
 * every assertion runs against a settled engine.
 */

#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include <linux/usb.h>

#include "linuxkpi_usb.h"
#include "lkpi_usb_backend.h"
#include "mock_hcd.h"
#include "usb_msc_driver.h"

#include "engine_env.h"
#include "judge.h"

#define ENV_VID	0x1234
#define ENV_PID	0x5678

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

/* the pending-list depth: the engine arms at most one transfer per endpoint
 * (its per-endpoint single flight), and the harness addresses three
 * endpoints, so a handful of entries is the whole working set */
#define ENV_PEND_MAX	7

struct env_pend {
	struct lkpi_usb_be_urb	cp;		/* the copy handed to the controller */
	struct lkpi_usb_be_urb	*orig;		/* the engine's own backend urb */
	struct stress_urb	*s;
	unsigned int		gen;
	unsigned long long	seq;		/* submission order (the order knob's
						 * FIFO/REVERSE authority) */
	int			live;
	int			at_ctrl;
	int			err;		/* the programmed seam code */
	int			double_complete;
	unsigned int		due;		/* the step the transfer completes at */
};

static struct env_pend g_pend[ENV_PEND_MAX];
static unsigned int g_pend_cursor;
static unsigned long long g_pend_seq;	/* the submission counter */

static struct stress_urb *g_rows[ENV_MAX_URBS];
static unsigned int g_row_n;

static struct mock_backend *g_b;
static const struct lkpi_usb_be_ops *g_mock_ops;
static void *g_mock_ctx;
static osMutexId_t g_lock;
static osSemaphoreId_t g_cb_sem;
static unsigned int g_clk;		/* the harness's step counter */
static unsigned int g_op;		/* the harness's operation index */
static unsigned int g_cb_total;		/* driver-visible completions */
static unsigned int g_seam_total;	/* seam deliveries */
static unsigned int g_cb_seq;
static unsigned int g_at_ctrl;

/* the knobs (sticky until env_reset_knobs; captured per submit) */
static unsigned int g_delay;		/* extra steps before the due point */
static int g_park;			/* never complete */
static int g_pass;			/* into the controller at submit time */
static int g_err;			/* the programmed seam error */
static unsigned int g_order = ENV_ORDER_FIFO;
static int g_double_complete;
static int g_control_timeout;

/* the harness's device (one interface, the peer's two bulk endpoints) */
static struct usb_device g_dev;
static struct usb_interface g_intf;
static struct usb_host_interface g_alt;
static struct usb_host_endpoint g_eps[2];
/* one buffer arena per row; the widest data stage this harness stages is a
 * 1024-byte control read (the scripted short-read shapes of the sibling suite) */
static u8 g_arena[ENV_MAX_URBS][ENV_ARENA];

static void	env_lock(void);
static void	env_unlock(void);
static void	env_cb(struct urb *u);

/* ------------------------------------------------------------------ */
/* the completion funnel                                               */
/* ------------------------------------------------------------------ */

/* map the engine's backend urb back to the harness row: the engine fills the
 * backend from the caller's urb (linuxkpi_usb.c lkpi_usb_arm), so the identity
 * is the transfer's own storage - the setup line for a control transfer, the
 * buffer for a bulk one.  Each row owns distinct buffers. */
static struct stress_urb *
env_be_row(const struct lkpi_usb_be_urb *be)
{
	unsigned int i;

	for (i = 0; i < g_row_n; i++) {
		struct stress_urb *s = g_rows[i];
		struct urb *u;

		if (s == NULL || s->u == NULL || s->freed)
			continue;	/* a released row keeps no identity */
		u = s->u;
		if (be->setup != NULL) {
			if (u->setup_packet == be->setup)
				return s;
			continue;
		}
		if (u->transfer_buffer != NULL && be->transfer_buffer != NULL &&
		    u->transfer_buffer == be->transfer_buffer &&
		    u->transfer_buffer_length == be->transfer_buffer_length)
			return s;
	}
	return NULL;
}

static struct env_pend *
pend_alloc(struct stress_urb *s, struct lkpi_usb_be_urb *orig)
{
	unsigned int i;

	for (i = 0; i < ENV_PEND_MAX; i++) {
		struct env_pend *p = &g_pend[(g_pend_cursor + i) % ENV_PEND_MAX];

		if (p->live)
			continue;
		g_pend_cursor = (unsigned int)(p - g_pend) + 1u;
		memset(p, 0, sizeof(*p));
		p->s = s;
		p->orig = orig;
		p->live = 1;
		p->gen++;
		p->seq = ++g_pend_seq;
		return p;
	}
	return NULL;	/* the engine can hold at most one per endpoint */
}

static void
wrap_deliver(struct env_pend *p)
{
	struct stress_urb *s = p->s;
	struct lkpi_usb_be_urb *o = p->orig;
	int nbytes;

	/* copy the controller's own result back into the engine's backend urb
	 * before the engine's completion reads it (linuxkpi_usb.c
	 * lkpi_usb_be_complete reads ep->be.errorcode / actual_length) */
	o->errorcode = p->cp.errorcode;
	o->actual_length = p->cp.actual_length;
	if (p->err != 0) {
		o->errorcode = p->err;
		o->actual_length = 0;
	}
	nbytes = o->errorcode == 0 ? (int)o->actual_length : -1;

	s->seam_count++;
	g_seam_total++;
	if (o->complete != NULL)
		o->complete(o->arg, nbytes);
	if (p->double_complete && o->complete != NULL) {
		/* the fault knob: a second giveback for the same transfer.  The
		 * engine's completion identity is the endpoint; a second event
		 * either hits its duplicate-claim guard or, when the endpoint
		 * already re-armed, claims the successor.  The fault scenario
		 * keeps the endpoint empty so the guard is the path under test.
		 * The seam tally counts the second giveback too - that is the
		 * violation the exactly-once rule names. */
		s->seam_count++;
		g_seam_total++;
		o->complete(o->arg, nbytes);
	}
}

static void
pend_release(struct env_pend *p)
{
	p->live = 0;
	p->at_ctrl = 0;
	p->orig = NULL;
	p->s = NULL;
}

/* the trampoline the controller calls: the copy carries it, so a controller
 * completion re-enters the funnel and reaches the engine from there */
static void
wrap_cp_complete(void *arg, int nbytes)
{
	struct env_pend *p = arg;

	(void)nbytes;
	env_lock();
	if (p->at_ctrl) {
		p->at_ctrl = 0;
		g_at_ctrl--;
	}
	wrap_deliver(p);
	pend_release(p);
	env_unlock();
}

/* hand a pending transfer to the controller's own queue (the pass-through
 * knob and the delayed completion both use this) */
static void
pend_forward(struct env_pend *p)
{
	p->cp = *p->orig;
	p->cp.complete = wrap_cp_complete;
	p->cp.arg = p;
	p->cp.timeout = 0;
	if (g_mock_ops->submit_urb(g_mock_ctx, &p->cp) == 0) {
		p->at_ctrl = 1;
		g_at_ctrl++;
		return;
	}
	/* the controller refused: deliver one refusal the way the engine's own
	 * arm-failure path would, then release the slot */
	p->cp.errorcode = -LKPI_USB_BE_ERR_NOMEM;
	p->cp.actual_length = 0;
	wrap_deliver(p);
	pend_release(p);
}

/* ------------------------------------------------------------------ */
/* the seam operations                                                 */
/* ------------------------------------------------------------------ */

static int
wrap_submit(void *ctx, struct lkpi_usb_be_urb *urb)
{
	struct stress_urb *s;
	struct env_pend *p;

	(void)ctx;
	if (urb == NULL)
		return -LKPI_USB_BE_ERR_INVAL;
	/* the synchronous lane (timeout != 0, complete == NULL) blocks in the
	 * backend and must not be deferred: forward it as it is */
	if (urb->timeout != 0 || urb->complete == NULL)
		return g_mock_ops->submit_urb(g_mock_ctx, urb);

	env_lock();
	s = env_be_row(urb);
	if (s == NULL) {
		/* an untracked transfer: no knob may touch it */
		int rc = g_mock_ops->submit_urb(g_mock_ctx, urb);

		env_unlock();
		return rc;
	}
	p = pend_alloc(s, urb);
	if (p == NULL) {
		env_unlock();
		return -LKPI_USB_BE_ERR_NOMEM;
	}
	p->err = g_err;
	p->double_complete = g_double_complete;
	g_double_complete = 0;
	if (g_pass) {
		pend_forward(p);
		env_unlock();
		return 0;
	}
	p->due = g_park ? 0xffffffffu : g_clk + g_delay + 1u;
	env_unlock();
	return 0;
}

static int
wrap_kill(void *ctx, struct lkpi_usb_be_urb *urb)
{
	struct env_pend *p = NULL;
	unsigned int i;
	int rc;

	(void)ctx;
	if (urb == NULL)
		return -LKPI_USB_BE_ERR_INVAL;
	env_lock();
	for (i = 0; i < ENV_PEND_MAX; i++) {
		if (g_pend[i].live && g_pend[i].orig == urb) {
			p = &g_pend[i];
			break;
		}
	}
	if (p == NULL) {
		env_unlock();
		return -LKPI_USB_BE_ERR_BUSY;	/* claimed or finished elsewhere */
	}
	if (p->at_ctrl) {
		/* still in the controller's queue: let the controller cancel it
		 * and deliver, exactly as its own seam_kill does */
		rc = g_mock_ops->kill_urb(g_mock_ctx, &p->cp);
		env_unlock();
		return rc;
	}
	/* the wrapper still holds it: cancel and deliver the one completion this
	 * transfer owed, from the killer's context (the controller's seam_kill
	 * shape: -LKPI_USB_BE_ERR_CANCELLED) */
	p->cp = *p->orig;
	p->cp.errorcode = -LKPI_USB_BE_ERR_CANCELLED;
	p->cp.actual_length = 0;
	p->err = -LKPI_USB_BE_ERR_CANCELLED;
	p->double_complete = 0;
	wrap_deliver(p);
	pend_release(p);
	env_unlock();
	return 0;
}

static int
wrap_control(void *ctx, struct lkpi_usb_be_hubport *port,
    const struct lkpi_usb_setup_packet *setup, void *buffer)
{
	int rc;

	(void)ctx;
	env_lock();
	if (g_control_timeout) {
		env_unlock();
		return -LKPI_USB_BE_ERR_TIMEOUT;
	}
	rc = g_mock_ops->control_transfer(g_mock_ctx, port, setup, buffer);
	env_unlock();
	return rc;
}

static const struct lkpi_usb_be_ops g_wrap_ops = {
	.submit_urb = wrap_submit,
	.kill_urb = wrap_kill,
	.control_transfer = wrap_control,
};

/* ------------------------------------------------------------------ */
/* the driver-facing callback                                          */
/* ------------------------------------------------------------------ */

static void
env_cb(struct urb *u)
{
	struct stress_urb *s;

	env_lock();
	s = env_row(u);
	if (s == NULL) {
		env_unlock();
		judge_check(0, JUDGE_RULE_EXACTLY_ONCE, u, 0, 1);
		return;
	}
	s->cb_count++;
	s->cb_seq = ++g_cb_seq;
	if (s->cb_count == 1)
		s->cb_first = u->status;
	s->cb_last = u->status;
	s->cb_tid = osThreadGetId();
	g_cb_total++;
	(void)osSemaphoreRelease(g_cb_sem);
	if (s->observe != NULL) {
		/* the reentrancy cells: the observer may free, kill or resubmit;
		 * the lock is recursive so those calls may re-enter the wrapper */
		s->observe(s, u);
	}
	env_unlock();
}

/* ------------------------------------------------------------------ */
/* the device                                                          */
/* ------------------------------------------------------------------ */

static void
device_build(struct mock_backend *b)
{
	memset(&g_dev, 0, sizeof(g_dev));
	memset(&g_intf, 0, sizeof(g_intf));
	memset(&g_alt, 0, sizeof(g_alt));
	memset(g_eps, 0, sizeof(g_eps));

	g_dev.devnum = 1;
	g_dev.speed = USB_SPEED_HIGH;
	g_dev.state = USB_STATE_DEFAULT;
	g_dev.descriptor.bLength = 18;
	g_dev.descriptor.bDescriptorType = 0x01;
	g_dev.descriptor.bcdUSB = 0x0200;
	g_dev.descriptor.bMaxPacketSize0 = 64;
	g_dev.descriptor.idVendor = ENV_VID;
	g_dev.descriptor.idProduct = ENV_PID;
	g_dev.descriptor.bNumConfigurations = 1;
	g_dev._vendor = ENV_VID;
	g_dev._product = ENV_PID;

	g_dev.ep0.desc.bLength = 7;
	g_dev.ep0.desc.bDescriptorType = 0x05;
	g_dev.ep0.desc.bEndpointAddress = 0x00;
	g_dev.ep0.desc.bmAttributes = 0x00;
	g_dev.ep0.desc.wMaxPacketSize = 64;

	g_eps[0].desc.bLength = 7;
	g_eps[0].desc.bDescriptorType = 0x05;
	g_eps[0].desc.bEndpointAddress = 0x81;	/* bulk IN */
	g_eps[0].desc.bmAttributes = 0x02;
	g_eps[0].desc.wMaxPacketSize = 512;
	g_eps[0].hcpriv = &b->ep_in;

	g_eps[1].desc.bLength = 7;
	g_eps[1].desc.bDescriptorType = 0x05;
	g_eps[1].desc.bEndpointAddress = 0x01;	/* bulk OUT */
	g_eps[1].desc.bmAttributes = 0x02;
	g_eps[1].desc.wMaxPacketSize = 512;
	g_eps[1].hcpriv = &b->ep_out;

	g_alt.desc.bLength = 9;
	g_alt.desc.bDescriptorType = 0x04;
	g_alt.desc.bInterfaceNumber = 0;
	g_alt.desc.bAlternateSetting = 0;
	g_alt.desc.bNumEndpoints = 2;
	g_alt.desc.bInterfaceClass = 0x08;
	g_alt.desc.bInterfaceSubClass = 0x06;
	g_alt.desc.bInterfaceProtocol = 0x50;
	g_alt.endpoint = g_eps;
	g_alt.num_endpoints = 2;

	g_intf.altsetting = &g_alt;
	g_intf.num_altsetting = 1;
	g_intf.cur_altsetting = &g_alt;
	g_intf.udev = &g_dev;

	lkpi_usb_device_bind(&g_dev, &g_intf, &b->port);
}

/* ------------------------------------------------------------------ */
/* bring-up                                                            */
/* ------------------------------------------------------------------ */

int
env_init(void)
{
	osSemaphoreAttr_t sattr = { 0 };
	osMutexAttr_t mattr = { 0 };
	unsigned int i;

	memset(g_pend, 0, sizeof(g_pend));
	memset(g_rows, 0, sizeof(g_rows));
	for (i = 0; i < ENV_MAX_URBS; i++) {
		unsigned int j;

		for (j = 0; j < sizeof(g_arena[i]); j++)
			g_arena[i][j] = 0xa5;
	}
	mattr.name = "stress_lock";
	mattr.attr_bits = osMutexRecursive;
	g_lock = osMutexNew(&mattr);
	if (g_lock == NULL)
		return -1;
	sattr.name = "stress_cb";
	g_cb_sem = osSemaphoreNew(0xffffu, 0u, &sattr);
	if (g_cb_sem == NULL)
		return -1;
	g_b = mock_backend_create();
	if (g_b == NULL)
		return -1;
	g_mock_ops = mock_backend_ops();
	g_mock_ctx = g_b;
	/* the harness's ops are the seam the engine talks to */
	lkpi_usb_be_register(&g_wrap_ops, NULL);
	/* the worker never drains: the harness pumps the controller itself */
	mock_hcd_hold_set(g_b, 1);
	mock_hcd_connect(g_b);
	device_build(g_b);
	if (usb_register(&mock_msc_driver) != 0)
		return -1;
	/* bring-up sanity: the clean-room command-driver stand-in answers the
	 * probe and runs one bulk-only-transport command on the sync lane (the
	 * lane the stand-in is written for) */
	g_intf.claimed = true;
	if (lkpi_usb_probe_drivers(&g_intf) != 0 || mock_msc_probe_calls() != 1)
		return -1;
	lkpi_usb_force_sync_set(true);
	if (mock_msc_tur(&g_dev) != 0 || mock_msc_command_count() != 1)
		return -1;
	/* the cells run the asynchronous lane: the engine defers every completion
	 * to its drain thread, which is what the reentrancy cells measure */
	lkpi_usb_force_sync_set(false);
	env_reset_knobs();
	printf("URBSTRESS: env controller=ready driver=%s tur=ok worker=held\n",
	    mock_msc_driver.name);
	return 0;
}

struct mock_backend *
env_backend(void)
{
	return g_b;
}

struct usb_device *
env_device(void)
{
	return &g_dev;
}

u8 *
env_buf(unsigned int row, unsigned int len)
{
	if (row >= ENV_MAX_URBS || len > sizeof(g_arena[0]))
		return NULL;
	return g_arena[row];
}

/* ------------------------------------------------------------------ */
/* knobs                                                               */
/* ------------------------------------------------------------------ */

void
env_reset_knobs(void)
{
	g_delay = 0;
	g_park = 0;
	g_pass = 0;
	g_err = 0;
	g_order = ENV_ORDER_FIFO;
	g_double_complete = 0;
	g_control_timeout = 0;
}

void
env_knob_delay(unsigned int steps)
{
	g_delay = steps;
	g_park = 0;
	g_pass = 0;
}

void
env_knob_park(void)
{
	g_park = 1;
	g_pass = 0;
	g_delay = 0;
}

void
env_knob_passthrough(void)
{
	g_pass = 1;
	g_park = 0;
	g_delay = 0;
}

void
env_knob_error(int seam_code)
{
	g_err = seam_code;
}

void
env_knob_order(unsigned int mode)
{
	g_order = mode;
}

void
env_knob_double_complete(void)
{
	g_double_complete = 1;
}

void
env_knob_control_timeout(int on)
{
	g_control_timeout = on;
}

/* ------------------------------------------------------------------ */
/* the wrapper-state lock                                              */
/* ------------------------------------------------------------------ */

static void
env_lock(void)
{

	(void)osMutexAcquire(g_lock, osWaitForever);
}

static void
env_unlock(void)
{

	(void)osMutexRelease(g_lock);
}

/* ------------------------------------------------------------------ */
/* rows and urbs                                                       */
/* ------------------------------------------------------------------ */

static void
row_link(struct stress_urb *s, struct urb *u, int want_count, int want_status)
{
	unsigned int i;

	memset(s, 0, sizeof(*s));
	s->u = u;
	s->tracked = 1;
	s->want_count = want_count;
	s->want_status = want_status;
	for (i = 0; i < g_row_n; i++) {
		if (g_rows[i] == s)
			return;
	}
	for (i = 0; i < ENV_MAX_URBS; i++) {
		if (g_rows[i] == NULL) {
			g_rows[i] = s;
			if (i >= g_row_n)
				g_row_n = i + 1u;
			return;
		}
	}
}

struct urb *
env_alloc(struct stress_urb *s, int want_count, int want_status)
{
	struct urb *u;

	env_lock();
	u = usb_alloc_urb(0, GFP_KERNEL);
	if (u != NULL)
		row_link(s, u, want_count, want_status);
	env_unlock();
	return u;
}

void
env_track(struct stress_urb *s, struct urb *u, int want_count, int want_status)
{
	env_lock();
	row_link(s, u, want_count, want_status);
	env_unlock();
}

void
env_forget(struct stress_urb *s)
{
	unsigned int i;

	env_lock();
	for (i = 0; i < g_row_n; i++) {
		if (g_rows[i] == s)
			g_rows[i] = NULL;
	}
	memset(s, 0, sizeof(*s));
	env_unlock();
}

void
env_free_urb(struct stress_urb *s)
{
	void *u;

	env_lock();
	if (s->freed) {			/* already released: nothing to do */
		env_unlock();
		return;
	}
	s->freed = 1;
	u = s->u;
	env_unlock();
	/* outside the lock: a live submission is quiesced first, and that path
	 * delivers a completion which re-enters env_cb (and the lock) */
	usb_free_urb(u);
}

void
env_note_released(struct stress_urb *s)
{

	env_lock();
	s->freed = 1;
	env_unlock();
}

void
env_attach_cb(struct stress_urb *s)
{

	env_lock();
	if (s->u != NULL)
		s->u->complete = env_cb;
	env_unlock();
}

void
env_fill_bulk(struct stress_urb *s, unsigned int ep, void *buf,
    unsigned int len, int in)
{
	unsigned int pipe;

	(void)ep;
	pipe = in ? usb_rcvbulkpipe(&g_dev, 1) : usb_sndbulkpipe(&g_dev, 1);
	usb_fill_bulk_urb(s->u, &g_dev, pipe, buf, (int)len, env_cb, NULL);
}

void
env_fill_control(struct stress_urb *s, void *setup, void *buf,
    unsigned int len)
{
	usb_fill_control_urb(s->u, &g_dev, usb_rcvctrlpipe(&g_dev, 0),
	    setup, buf, (int)len, env_cb, NULL);
}

int
env_submit(struct stress_urb *s)
{
	int rc;

	/* the lock is not held across the submit: the engine's submit path calls
	 * back into the wrapper (wrap_submit) and, on a kill-on-free, may deliver
	 * a completion that re-enters env_cb */
	rc = usb_submit_urb(s->u, GFP_KERNEL);
	env_lock();
	env_op_bump();
	if (rc == 0)
		s->submits++;
	env_unlock();
	return rc;
}

/*
 * The dead-urb marker: the engine's submit/kill guards name a released urb by
 * the poison value its allocator writes into _alloc_id (linuxkpi_usb.c
 * LKPI_URB_ID_POISON = 0xdeadbeef).  The harness writes that value into a LIVE
 * block and restores it afterwards, so the guard branch runs on storage the
 * sanitizer still owns; a genuine free-then-submit would read a released block,
 * which is precisely the access the guard exists to refuse in the target build
 * (the harness must not manufacture it under a sanitizer).
 */
void
env_mark_dead_id(struct stress_urb *s)
{
	env_lock();
	s->saved_alloc_id = s->u->_alloc_id;
	s->u->_alloc_id = 0xdeadbeefU;
	s->dead_marked = 1;
	env_unlock();
}

void
env_clear_dead_id(struct stress_urb *s)
{
	env_lock();
	if (s->dead_marked) {
		s->u->_alloc_id = s->saved_alloc_id;
		s->dead_marked = 0;
	}
	env_unlock();
}

struct stress_urb *
env_row(const struct urb *u)
{
	unsigned int i;

	for (i = 0; i < g_row_n; i++) {
		if (g_rows[i] != NULL && !g_rows[i]->freed &&
		    g_rows[i]->u == u)
			return g_rows[i];
	}
	return NULL;
}

struct stress_urb *
env_row_n(unsigned int i)
{
	if (i >= g_row_n)
		return NULL;
	return g_rows[i];
}

unsigned int
env_row_count(void)
{
	return g_row_n;
}

void
env_row_reset(struct stress_urb *s)
{
	unsigned int i;

	env_lock();
	for (i = 0; i < g_row_n; i++) {
		if (g_rows[i] == s)
			g_rows[i] = NULL;
	}
	memset(s, 0, sizeof(*s));
	env_unlock();
}

/* ------------------------------------------------------------------ */
/* the step clock                                                      */
/* ------------------------------------------------------------------ */

/*
 * One step: advance the counter, forward every transfer whose due has arrived
 * (in the order the knob selects), then run the controller's queue empty.  A
 * delivery can arm the endpoint's next queued urb inside the engine's drain, so
 * the controller queue may grow during the run; the pump count is re-read and
 * the loop runs the queue empty, which makes one step sufficient to drive the
 * whole pipeline an endpoint can hold.
 */
void
env_step(void)
{
	struct {
		unsigned int idx;
		unsigned int gen;
	} due[ENV_PEND_MAX];
	unsigned int n = 0, i, order_i, to_pump;

	env_lock();
	env_op_bump();
	g_clk++;

	for (i = 0; i < ENV_PEND_MAX; i++) {
		if (!g_pend[i].live || g_pend[i].at_ctrl)
			continue;
		if (g_pend[i].due > g_clk)
			continue;
		due[n].idx = i;
		due[n].gen = g_pend[i].gen;
		n++;
	}
	/* the pending slots are reused round-robin, so the scan order is not the
	 * submission order: sort the batch by its submission sequence, which is
	 * what makes FIFO and REVERSE true orders (a small insertion sort; the
	 * batch is a handful of entries at most) */
	for (i = 1; i < n; i++) {
		unsigned int j = i;
		unsigned int idx = due[i].idx, gen = due[i].gen;

		while (j > 0 && g_pend[due[j - 1].idx].seq > g_pend[idx].seq) {
			due[j].idx = due[j - 1].idx;
			due[j].gen = due[j - 1].gen;
			j--;
		}
		due[j].idx = idx;
		due[j].gen = gen;
	}

	for (order_i = 0; order_i < n; order_i++) {
		unsigned int pick = order_i;
		struct env_pend *p;

		if (g_order == ENV_ORDER_REVERSE)
			pick = n - 1u - order_i;
		else if (g_order == ENV_ORDER_PERM && n > 1u)
			pick = (order_i + 1u) % n;
		p = &g_pend[due[pick].idx];
		if (!p->live || p->gen != due[pick].gen)
			continue;
		if (p->err != 0) {
			/* the programmed error: no controller round, the transfer
			 * reports the code as its single completion */
			p->cp = *p->orig;
			p->cp.errorcode = p->err;
			p->cp.actual_length = 0;
			wrap_deliver(p);
			pend_release(p);
			continue;
		}
		pend_forward(p);
	}

	to_pump = g_at_ctrl;
	for (i = 0; i < to_pump; i++)
		mock_backend_pump(g_b);
	env_unlock();
}

/* tracked rows whose block was released (nothing left to inspect) */
static unsigned int
env_freed_rows(void)
{
	unsigned int i, n = 0;

	env_lock();
	for (i = 0; i < g_row_n; i++)
		if (g_rows[i] != NULL && g_rows[i]->freed)
			n++;
	env_unlock();
	return n;
}

/* the transfers a step can still commit: a parked one (the kill window's knob)
 * only ever completes when a kill claims it, so it does not hold the settle
 * loop open - the cell or the close's drain kills it */
static unsigned int
env_pending_steppable(void)
{
	unsigned int i, n = 0;

	env_lock();
	for (i = 0; i < ENV_PEND_MAX; i++) {
		if (!g_pend[i].live || g_pend[i].at_ctrl)
			continue;
		if (g_pend[i].due == 0xffffffffu)
			continue;
		n++;
	}
	env_unlock();
	return n;
}

unsigned int
env_settle(void)
{
	unsigned int rounds;

	/* A delivery can re-arm the endpoint's next queued urb from inside the
	 * engine's drain, i.e. while this loop waits for callbacks: the pending
	 * set is re-checked AFTER the wait, and a re-armed transfer is stepped
	 * again, so the loop only leaves when the world is quiet.  A parked
	 * transfer (the kill window's knob) is excluded: only a kill completes it,
	 * never a step. */
	for (rounds = 1; rounds <= ENV_STEP_BUDGET; rounds++) {
		env_step();
		if (env_at_controller() != 0 || env_pending_steppable() != 0)
			continue;
		if (env_wait_cb(env_seam_total()) != 0)
			continue;
		if (env_at_controller() == 0 && env_pending_steppable() == 0) {
			/* a callback-released urb leaves the drain's tail (transport
			 * reference drop + endpoint re-arm) unobservable: give it a
			 * bounded grace so the next step does not race it */
			if (env_freed_rows() != 0) {
				(void)osDelay(2u);
				if (env_freed_rows() != 0)
					return rounds;
			}
			return rounds;
		}
	}
	return ENV_STEP_BUDGET;	/* exhausted: the no-deadlock rule's feed */
}

/*
 * The end-of-cell settle condition, stated over the driver face only: every
 * tracked urb has no completion still owed, and the engine's transport
 * reference on it is gone (urb._refs back to the caller's own 1).  The engine's
 * delivery tail runs in its drain thread AFTER the driver callback (drop the
 * reference, re-arm the endpoint), so waiting for this condition is what makes
 * a cell's final accounting exact; the harness waits while blocked, which is
 * what lets the lower-or-equal-priority drain run.
 */
/* a row whose transfer is parked at the backend: only a kill completes it, so
 * the settle loop must not wait for it (the cell or the close's drain does the
 * killing) */
static int
row_parked(const struct stress_urb *s)
{
	unsigned int i;
	int parked = 0;

	env_lock();
	for (i = 0; i < ENV_PEND_MAX; i++) {
		if (g_pend[i].live && !g_pend[i].at_ctrl && g_pend[i].s == s &&
		    g_pend[i].due == 0xffffffffu) {
			parked = 1;
			break;
		}
	}
	env_unlock();
	return parked;
}

static int
rows_settled(void)
{
	unsigned int i;

	for (i = 0; i < g_row_n; i++) {
		struct stress_urb *s = g_rows[i];

		if (s == NULL || s->freed || s->u == NULL)
			continue;
		if (row_parked(s))
			continue;	/* the kill window owns that completion */
		if (row_parked(s))
			continue;	/* the kill window owns that completion */
		if (s->submits > s->cb_count)
			return 0;	/* a completion is still owed */
		/* the engine drops its transport reference in the drain's tail,
		 * AFTER the callback: the caller's own 1 is what must be left.
		 * An anchored urb legitimately carries one more (the anchor
		 * reference, dropped by usb_unanchor_urb), so that shape is the
		 * settled state too - it is API state, not an engine hold. */
		if (s->u->_refs > (s->u->anchor != NULL ? 2 : 1))
			return 0;
	}
	return 1;
}

int
env_wait_cb(unsigned int total)
{
	unsigned int spins = 0, last_cb = 0xffffffffu;

	(void)total;
	/* The gate is the driver face (every accepted submission answered, every
	 * transport reference dropped), never the seam tally: a fault-injection
	 * scenario can owe the seam MORE deliveries than the driver can observe
	 * (the duplicate giveback the engine's claim guard absorbs), and that is
	 * precisely the state the judgement rules then name).
	 *
	 * Waiting discipline: block on the completion semaphore; the engine's
	 * drain thread runs below or equal to this task, so blocking is what lets
	 * it complete each delivery tail (the transport-reference drop and the
	 * endpoint re-arm included).  Every sign of progress (a new callback or a
	 * smaller owed set) resets the patience counter, so the bound only fires
	 * when nothing moved for a whole patience window. */
	while (!rows_settled()) {
		osStatus_t st = osSemaphoreAcquire(g_cb_sem, 50u);
		unsigned int cb = env_cb_total();

		if (st == osOK || cb != last_cb) {
			last_cb = cb;
			spins = 0;
			continue;
		}
		if (++spins > 40u)	/* ~2 s with zero progress */
			return -1;
	}
	return 0;
}

int
env_drain_all(void)
{
	struct urb *live[ENV_MAX_URBS];
	unsigned int i, n = 0;

	env_lock();
	for (i = 0; i < g_row_n; i++) {
		struct stress_urb *s = g_rows[i];

		if (s == NULL || s->freed || s->u == NULL)
			continue;
		if (s->submits > s->seam_count)
			live[n++] = s->u;
	}
	env_unlock();
	/* outside the lock: a kill quiesces and its delivery re-enters env_cb */
	for (i = 0; i < n; i++)
		usb_kill_urb(live[i]);
	(void)env_settle();
	return env_wait_cb(env_seam_total());
}

unsigned int
env_cb_total(void)
{
	unsigned int v;

	env_lock();
	v = g_cb_total;
	env_unlock();
	return v;
}

unsigned int
env_seam_total(void)
{
	unsigned int v;

	env_lock();
	v = g_seam_total;
	env_unlock();
	return v;
}

unsigned int
env_pending_live(void)
{
	unsigned int i, n = 0;

	env_lock();
	for (i = 0; i < ENV_PEND_MAX; i++)
		if (g_pend[i].live)
			n++;
	env_unlock();
	return n;
}

unsigned int
env_at_controller(void)
{
	unsigned int v;

	env_lock();
	v = g_at_ctrl;
	env_unlock();
	return v;
}

unsigned int
env_clock_now(void)
{
	unsigned int v;

	env_lock();
	v = g_clk;
	env_unlock();
	return v;
}

unsigned int
env_op(void)
{
	unsigned int v;

	env_lock();
	v = g_op;
	env_unlock();
	return v;
}

void
env_op_bump(void)
{

	g_op++;
}

/* ------------------------------------------------------------------ */
/* engine helpers                                                      */
/* ------------------------------------------------------------------ */

int
env_kill_ran_sync(const struct stress_urb *s)
{

	/* the killer returns only after the urb quiesced, and the engine publishes
	 * the idle state only after the delivery chain ran the driver callback
	 * (linuxkpi_usb.c lkpi_usb_drain_one sets LKPI_Q_IDLE after
	 * lkpi_usb_deliver).  So "every accepted submission has been observed by
	 * the driver" is exactly "the kill drained synchronously" - whichever
	 * context the delivery used (the seam's killer-context cancel, the
	 * engine's inline FIFO cancel, or the drain). */
	return s->cb_count == s->submits;
}

void
env_stats(struct lkpi_usb_mem_stats *out)
{

	lkpi_usb_mem_stats_get(out);
}

int
env_force_sync(int on)
{

	lkpi_usb_force_sync_set(on != 0);
	return 0;
}

const struct lkpi_usb_be_ops *
env_mock_ops(void)
{

	return g_mock_ops;
}

void *
env_mock_ctx(void)
{

	return g_mock_ctx;
}
