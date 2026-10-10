/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * matrix.c - the 28-cell scenario matrix of the urb stress harness.
 *
 * Six groups over the engine's contract (components/linuxkpi/usb):
 *   B1 submit validation (5 cells)
 *   B2 completion semantics (4)
 *   B3 kill / unlink interleave (6)
 *   B4 the anchor family (4)
 *   B5 injection combinations (5)
 *   B6 timing and reentrancy (4)
 *
 * Every expectation cites its truth in a comment: the in-repo seam header
 * (components/linuxkpi/usb/lkpi_usb_backend.h), the engine's own source
 * (components/linuxkpi/usb/linuxkpi_usb.c), or the Linux errno semantics the
 * port states.  Where the measured behaviour of the engine is itself the fact
 * (the reentrancy group), the cell records the measured rule and asserts only
 * the invariants (no crash, exactly-once, balance).
 *
 * Cells whose misuse shape the sanitizer build cannot manufacture - a genuine
 * read of a released block - drive the guard through its observable contract
 * instead (the poison identity of linuxkpi_usb.c) and say so in the cell.
 *
 * Scheduling note the cells rely on: the harness task runs above the engine's
 * drain thread, so a completion committed by the harness is delivered while the
 * harness is inside a blocking wait (env_settle/env_wait_cb), and the cells
 * assert only after such a wait.  Cell B3.4 uses the same relation the other way
 * round: while the harness spins in a tight loop nothing else runs, so the kill
 * barrier stays up and every submit attempt lands inside the window.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include <linux/usb.h>

#include "lkpi_usb_backend.h"
#include "linuxkpi_usb.h"
#include "mock_hcd.h"

#include "engine_env.h"
#include "judge.h"

#define ROWS_MAX	8
#define LAWFUL_ANY	0x7fffffff

static struct stress_urb g_row[ROWS_MAX];
static unsigned int g_row_i;
static u8 g_setup[ROWS_MAX][8];
static u32 g_bits;			/* the accounting bit per urb */
static struct lkpi_usb_mem_stats g_base;
static unsigned int g_want_df, g_want_sf, g_want_uaf, g_want_af;

/* the reentrancy cells' observations (written from the drain thread) */
static struct lkpi_usb_mem_stats g_stats_in_cb;
static int g_free_in_cb_seen;

/* ------------------------------------------------------------------ */
/* the cell frame                                                      */
/* ------------------------------------------------------------------ */

static void
cell_open(const char *id)
{

	/* the row pool belongs to the previous cell's close: a cell may allocate
	 * its rows before opening (the static initialisers do), so the reset
	 * happens at close, never here */
	env_reset_knobs();
	g_bits = 0;
	judge_begin(id);
}

static int
cell_close(int ok)
{
	struct lkpi_usb_mem_stats st;
	unsigned int i, steps;


	env_reset_knobs();
	steps = env_drain_all();
	/* release every row this cell allocated: the balance rule reads the
	 * seam's counters, so a cell must not leave allocations behind */
	for (i = 0; i < g_row_i; i++) {
		env_free_urb(&g_row[i]);
		env_forget(&g_row[i]);	/* the pool must not keep released rows:
					 * an address reused by the next cell would
					 * otherwise misroute its transfer identity */
	}
	g_row_i = 0;
	(void)env_settle();
	(void)env_wait_cb(env_seam_total());
	judge_rule_no_deadlock(steps, ENV_STEP_BUDGET);
	env_stats(&st);
	judge_rule_balance(&st);
	judge_rule_uaf_named(NULL, &g_base, &st, g_want_df, g_want_sf,
	    g_want_uaf, g_want_af);
	return judge_cell_end(ok);
}

/* a fresh tracked row, with its own buffer arena slot */
static struct stress_urb *
row_new(int want_count, int want_status)
{
	struct stress_urb *s = &g_row[g_row_i];

	if (g_row_i >= ROWS_MAX)
		return NULL;
	g_row_i++;
	if (env_alloc(s, want_count, want_status) == NULL)
		return NULL;
	return s;
}

/* a bulk row; `in` picks the direction, `len` bytes come from the row's arena */
static struct stress_urb *
row_bulk(int want_count, int want_status, int in, unsigned int len)
{
	struct stress_urb *s = row_new(want_count, want_status);
	u8 *buf;

	if (s == NULL)
		return NULL;
	buf = env_buf((unsigned int)(s - g_row), len);
	if (buf == NULL)
		return NULL;
	memset(buf, 0, len);
	env_fill_bulk(s, ENV_EP_BULK_IN, buf, len, in);
	return s;
}

/* a control row: its own setup line, its own buffer */
static struct stress_urb *
row_ctrl(int want_count, int want_status, u8 req, u8 type, u16 value,
    u16 index, unsigned int len)
{
	struct stress_urb *s = row_new(want_count, want_status);
	unsigned int idx;
	u8 *buf;

	if (s == NULL)
		return NULL;
	idx = (unsigned int)(s - g_row);
	memset(g_setup[idx], 0, 8);
	g_setup[idx][0] = type;
	g_setup[idx][1] = req;
	g_setup[idx][2] = (u8)value;
	g_setup[idx][3] = (u8)(value >> 8);
	g_setup[idx][4] = (u8)index;
	g_setup[idx][5] = (u8)(index >> 8);
	g_setup[idx][6] = (u8)len;
	g_setup[idx][7] = (u8)(len >> 8);
	buf = env_buf(idx, len != 0 ? len : 1u);
	if (buf != NULL)
		memset(buf, 0, len);
	env_fill_control(s, g_setup[idx], len != 0 ? buf : NULL, len);
	return s;
}

/* the exactly-once rule over one row */
static void
row_check_once(struct stress_urb *s)
{

	if (s->want_status != LAWFUL_ANY)
		judge_rule_error_code(s->u, s->want_status, s->cb_last);
	judge_rule_exactly_once(s->u, s->want_count, s->cb_count, s->submits,
	    s->seam_count);
}

/* every row of the cell, in submit order */
static void
cell_check_all(void)
{
	unsigned int i;

	for (i = 0; i < g_row_i; i++)
		row_check_once(&g_row[i]);
}

static void
cell_settle(void)
{
	unsigned int steps = env_settle();

	judge_rule_no_deadlock(steps, ENV_STEP_BUDGET);
	(void)env_wait_cb(env_seam_total());
}

/* ------------------------------------------------------------------ */
/* B1 - submit validation                                              */
/* ------------------------------------------------------------------ */

/*
 * B1.1 complete==NULL on the asynchronous lane must be refused with -EINVAL.
 * Truth: lkpi_usb_backend.h, completion contract: "an asynchronous submit
 * (timeout == 0, complete != NULL) is accepted"; the controller's seam
 * (mock_hcd.c seam_submit) answers -LKPI_USB_BE_ERR_INVAL to the missing
 * callback.  Accounting trace: nothing may move - no pending slot, no
 * completion, no counter.
 */
static int
cell_b1_1(void)
{
	struct lkpi_usb_be_urb be;
	struct lkpi_usb_mem_stats before, after;
	unsigned int seam_before;
	int rc, ok;

	cell_open("B1.1");
	env_stats(&before);
	seam_before = env_seam_total();
	memset(&be, 0, sizeof(be));
	be.hport = &env_backend()->port;
	be.ep = &env_backend()->ep_in;
	be.timeout = 0;
	be.complete = NULL;
	be.transfer_buffer = env_buf(0, 8);
	be.transfer_buffer_length = 8;
	rc = lkpi_usb_be_submit_urb(&be);
	env_stats(&after);
	judge_expect(rc == -LKPI_USB_BE_ERR_INVAL, "an asynchronous submit "
	    "without a completion callback is refused with -EINVAL (seam "
	    "contract)", NULL, -LKPI_USB_BE_ERR_INVAL, rc);
	judge_expect(after.n_urb_alloc == before.n_urb_alloc &&
	    after.n_urb_free == before.n_urb_free &&
	    after.n_coherent_alloc == before.n_coherent_alloc &&
	    env_pending_live() == 0 && env_seam_total() == seam_before,
	    "the refusal left no accounting trace (no pending slot, no "
	    "completion, no counter moved)", NULL, 0, (long)env_pending_live());
	ok = rc == -LKPI_USB_BE_ERR_INVAL && env_pending_live() == 0;
	judge_note("rc=%d pending=%u", rc, env_pending_live());
	return cell_close(ok);
}

/*
 * B1.2 a second submit of the same live urb is refused with -EBUSY and the
 * first submission is unharmed.
 * Truth: linuxkpi_usb.c usb_submit_urb, the live-urb guard
 * (urb->_qstate != IDLE/CALLBACK -> -EBUSY, the "submit of live urb" latch).
 */
static int
cell_b1_2(void)
{
	struct stress_urb *s = row_bulk(1, 0, 1, 8);
	int rc1, rc2, ok;

	cell_open("B1.2");
	rc1 = env_submit(s);
	rc2 = env_submit(s);
	judge_expect(rc1 == 0 && rc2 == -EBUSY, "first submit accepted, double "
	    "submit refused with -EBUSY", s->u, -EBUSY, rc2);
	cell_settle();
	judge_expect(s->cb_count == 1 && s->cb_last == 0, "the first "
	    "submission was unharmed (one completion, status 0)", s->u, 1,
	    s->cb_count);
	ok = rc1 == 0 && rc2 == -EBUSY && s->cb_count == 1 && s->cb_last == 0;
	judge_note("rc1=%d rc2=%d cb=%d", rc1, rc2, s->cb_count);
	s->submits = 1;		/* the refused double submit owes nothing */
	return cell_close(ok);
}

/*
 * B1.3 a submit of a dead urb is named and refused with -ENODEV.
 * Truth: linuxkpi_usb.c usb_submit_urb, the dead-urb guard: a released urb
 * carries LKPI_URB_ID_POISON and is refused with -ENODEV before any field is
 * read; the seam counts its release-path hits in lkpi_usb_mem_stats.  The
 * harness writes the poison identity into LIVE storage (the marker): freeing
 * the block and then submitting it would make the guard read a released block,
 * which is precisely the access the guard exists to refuse on the target - and
 * which this sanitizer build must not manufacture.  The counter evidence is
 * taken from the second release of the same identity, which the seam names as a
 * double free (linuxkpi_usb.c usb_free_urb: freed_hit -> double_free++).
 */
static int
cell_b1_3(void)
{
	struct stress_urb *s = row_bulk(1, 0, 1, 8);
	struct urb *up = s->u;
	struct lkpi_usb_mem_stats before, after;
	int rc, ok;

	cell_open("B1.3");
	env_mark_dead_id(s);		/* the poison identity: see the note */
	rc = env_submit(s);
	judge_expect(rc == -ENODEV, "a submit of a dead urb is refused with "
	    "-ENODEV (poison-id guard)", s->u, -ENODEV, rc);
	env_clear_dead_id(s);		/* restore the identity */
	env_stats(&before);
	env_free_urb(s);		/* the real release */
	usb_free_urb(up);		/* the named second release: guard first */
	env_stats(&after);
	g_want_df += 1;
	/* the free guard names a released pointer through TWO counters: the
	 * double-free tally and the use-after-free write tally it records for the
	 * refused caller (linuxkpi_usb.c usb_free_urb: freed_hit -> double_free++
	 * + lkpi_usb_uaf_hit) */
	g_want_uaf += 1;
	judge_rule_uaf_named(s->u, &before, &after, 1, 0, 1, 0);
	ok = rc == -ENODEV && after.double_free == before.double_free + 1 &&
	    lkpi_usb_urb_is_freed(up) == 1;
	judge_note("submit_refusal=-ENODEV named_double_free=%u",
	    after.double_free - before.double_free);
	return cell_close(ok);
}

/*
 * B1.4 submit with a NULL device pointer returns -ENODEV.
 * Truth: linuxkpi_usb.c usb_submit_urb: hport = lkpi_usb_hport(urb->dev); a
 * NULL device resolves to no hub port -> -ENODEV, the Linux "no such device"
 * refusal for a submission with no backing device.
 */
static int
cell_b1_4(void)
{
	struct stress_urb *s = row_new(1, LAWFUL_ANY);
	u8 *buf = env_buf((unsigned int)(s - g_row), 8);
	int rc, ok;

	cell_open("B1.4");
	usb_fill_bulk_urb(s->u, NULL,
	    usb_sndbulkpipe((struct usb_device *)NULL, 1), buf, 8, NULL,
	    NULL);
	rc = usb_submit_urb(s->u, GFP_KERNEL);
	env_op_bump();
	judge_expect(rc == -ENODEV, "a submit with a NULL device pointer "
	    "returns -ENODEV", s->u, -ENODEV, rc);
	judge_expect(env_pending_live() == 0, "the refusal touched no transfer "
	    "slot", NULL, 0, (long)env_pending_live());
	ok = rc == -ENODEV && env_pending_live() == 0;
	judge_note("rc=%d pending=%u", rc, env_pending_live());
	return cell_close(ok);
}

/*
 * B1.5 a control submit with a missing setup packet returns -EINVAL.
 * Truth: linuxkpi_usb.c: the blocking lane (lkpi_usb_submit_sync) answers
 * -EINVAL when setup_packet == NULL; the asynchronous arm (lkpi_usb_arm)
 * refuses the same shape, but the submission was already accepted, so the
 * refusal arrives as its one completion - laundered through the backend errno
 * map (lkpi_usb_errno has no case for a raw -EINVAL), so the measured async
 * status is -EIO, not -EINVAL.  The cell asserts the sync face (-EINVAL, the
 * required refusal) and records the async face as measured.
 */
static int
cell_b1_5(void)
{
	struct stress_urb *s = row_new(1, LAWFUL_ANY);
	u8 *buf = env_buf((unsigned int)(s - g_row), 8);
	int rc, rc2, ok;

	cell_open("B1.5");
	/* the synchronous lane: refused by the engine before any transfer
	 * (the missing completion callback is what selects the sync lane) */
	env_force_sync(1);
	usb_fill_control_urb(s->u, env_device(), usb_rcvctrlpipe(env_device(),
	    0), NULL, buf, 8, NULL, NULL);
	rc = usb_submit_urb(s->u, GFP_KERNEL);
	env_op_bump();
	judge_expect(rc == -EINVAL, "a sync control submit without a setup "
	    "packet returns -EINVAL", s->u, -EINVAL, rc);
	/* the asynchronous lane: accepted, then refused through the completion
	 * (the callback is present, the setup packet is not) */
	env_force_sync(0);
	usb_fill_control_urb(s->u, env_device(), usb_rcvctrlpipe(env_device(),
	    0), NULL, buf, 8, NULL, NULL);
	env_attach_cb(s);
	rc2 = env_submit(s);
	cell_settle();
	judge_note("sync_refusal=%d async_submit=%d async_status=%d "
	    "(measured: the arm refusal is laundered to -EIO)", rc, rc2,
	    s->cb_last);
	ok = rc == -EINVAL && rc2 == 0 && s->cb_count == 1;
	row_check_once(s);
	return cell_close(ok);
}

/* ------------------------------------------------------------------ */
/* B2 - completion semantics                                           */
/* ------------------------------------------------------------------ */

/*
 * B2.1 exactly one completion per urb over a batch, with the accounting bit.
 * Truth: lkpi_usb_backend.h: "the backend calls complete(arg, nbytes) exactly
 * once ... a synchronous submit ... never calls complete"; the engine's drain
 * delivers each claimed urb once (linuxkpi_usb.c lkpi_usb_drain_one).
 */
static int
cell_b2_1(void)
{
	unsigned int n = 6, i, bits = 0;
	int ok = 1;

	cell_open("B2.1");
	for (i = 0; i < n; i++) {
		int in = (int)(i % 2u);		/* alternate directions */
		struct stress_urb *s = row_bulk(1, 0, in, 8 + i);

		if (s == NULL || env_submit(s) != 0)
			ok = 0;
	}
	cell_settle();
	for (i = 0; i < n; i++) {
		struct stress_urb *s = &g_row[i];

		if (s->cb_count == 1)
			bits |= 1u << i;	/* the per-urb accounting bit */
	}
	judge_expect(bits == ((1u << n) - 1u), "one completion per urb over "
	    "the batch (the per-urb accounting bit)", NULL,
	    (long)((1u << n) - 1u), (long)bits);
	g_bits = bits;
	cell_check_all();
	ok = ok && bits == ((1u << n) - 1u);
	judge_note("batch=%u bits=%u", n, bits);
	return cell_close(ok);
}

/*
 * B2.2 the callback observes one of the lawful statuses.
 * Truth: the engine's seam-to-errno map (linuxkpi_usb.c lkpi_usb_errno):
 * 0->0, STALL->-EPIPE, SHORT->-EREMOTEIO, CANCELLED->-ENOENT,
 * SHUTDOWN/NODEV/NOTCONN->-ENODEV.  -ECONNRESET is unreachable in this engine
 * (no seam code maps to it; the kill code is -ENOENT), which the cell records
 * as the measured fact rather than pretending to observe it.
 */
static int
cell_b2_2(void)
{
	struct stress_urb *s0, *s1, *s2, *s3, *s4;
	u32 seen = 0;
	int ok;

	cell_open("B2.2");
	/* 0: an ordinary transfer, settled before anything else is injected */
	s0 = row_bulk(1, 0, 1, 8);
	(void)env_submit(s0);
	cell_settle();

	/* -ENOENT: the cancellation code of a killed transfer */
	env_knob_park();
	s1 = row_bulk(1, -ENOENT, 1, 8);
	(void)env_submit(s1);
	usb_kill_urb(s1->u);
	env_op_bump();
	env_reset_knobs();
	cell_settle();

	/* -EPIPE: the scripted stall on the endpoint-descriptor read */
	mock_hcd_script(env_backend(), MOCK_SCRIPT_STALL_EP_DESC, 1);
	env_knob_passthrough();
	s2 = row_ctrl(1, -EPIPE, 0x06, 0x80, 0x0500, 0, 7);
	(void)env_submit(s2);
	cell_settle();

	/* -EREMOTEIO: the scripted short device read */
	mock_hcd_script(env_backend(), MOCK_SCRIPT_STALL_EP_DESC, 0);
	mock_hcd_script(env_backend(), MOCK_SCRIPT_SHORT_DEVICE, 1);
	s3 = row_ctrl(1, -EREMOTEIO, 0x06, 0x80, 0x0100, 0, 1024);
	(void)env_submit(s3);
	cell_settle();
	mock_hcd_script(env_backend(), MOCK_SCRIPT_SHORT_DEVICE, 0);

	/* -ENODEV: a transfer against a detached port */
	mock_hcd_disconnect(env_backend());
	s4 = row_bulk(1, -ENODEV, 1, 8);
	(void)env_submit(s4);
	cell_settle();
	mock_hcd_connect(env_backend());

	seen |= 1u << 0;
	if (s1->cb_last == -ENOENT) seen |= 1u << 1;
	if (s2->cb_last == -EPIPE) seen |= 1u << 2;
	if (s3->cb_last == -EREMOTEIO) seen |= 1u << 3;
	if (s4->cb_last == -ENODEV) seen |= 1u << 4;
	judge_expect((seen & 0x1fu) == 0x1fu, "the lawful status set was "
	    "covered: 0, -ENOENT, -EPIPE, -EREMOTEIO, -ENODEV", NULL, 0x1fu,
	    (long)seen);
	judge_expect(s0->cb_last == 0, "status 0 observed for the ordinary "
	    "transfer", s0->u, 0, s0->cb_last);
	cell_check_all();
	ok = (seen & 0x1fu) == 0x1fu && s0->cb_last == 0;
	judge_note("statuses 0=%d enoent=%d epipe=%d remote=%d nodev=%d "
	    "econnreset=unreachable-in-this-engine", s0->cb_last, s1->cb_last,
	    s2->cb_last, s3->cb_last, s4->cb_last);
	return cell_close(ok);
}

/*
 * B2.3 the callback never runs on the submitter's stack.
 * Truth: the engine's own contract note (linuxkpi_usb.c header): completions
 * "are deferred through a done-list to the drain thread, so ... the completion
 * callback never runs in the submitting context (the Linux
 * __usb_hcd_giveback_urb split)".
 */
static int
cell_b2_3(void)
{
	struct stress_urb *s = row_bulk(1, 0, 1, 8);
	osThreadId_t submitter;
	int ok;

	cell_open("B2.3");
	submitter = osThreadGetId();
	(void)env_submit(s);
	cell_settle();
	ok = s->cb_tid != NULL && s->cb_tid != submitter;
	judge_expect(ok, "the completion callback ran on a different thread "
	    "than the submitter (deferred drain, not the submitter's stack)",
	    s->u, 1, ok);
	judge_note("submitter=%p callback=%p", (void *)submitter,
	    (void *)s->cb_tid);
	return cell_close(ok);
}

/* B2.4's observation: the urb was already unanchored when the callback ran */
static struct stress_urb *g_anchor_watch;
static int g_anchor_in_cb_null;

static void
observe_anchor(struct stress_urb *s, struct urb *u)
{

	(void)s;
	if (u == g_anchor_watch->u && u->anchor == NULL)
		g_anchor_in_cb_null++;
}

/*
 * B2.4 the completion of an anchored urb has already unanchored it.
 * Truth: linuxkpi_usb.c lkpi_usb_deliver: "the anchor's reference is dropped
 * BEFORE the completion callback runs" (usb_unanchor_urb; the Linux giveback
 * contract).  Observed from the driver face: the callback sees
 * urb->anchor == NULL, and afterwards usb_kill_anchored_urbs finds nothing to
 * kill, so no further completion is manufactured.
 */
static int
cell_b2_4(void)
{
	struct usb_anchor anchor;
	struct stress_urb *s;
	unsigned int seam_before, cb_before;
	int ok;

	cell_open("B2.4");
	init_usb_anchor(&anchor);
	s = row_bulk(1, 0, 1, 8);
	s->observe = observe_anchor;
	g_anchor_watch = s;
	g_anchor_in_cb_null = 0;
	usb_anchor_urb(s->u, &anchor);
	(void)env_submit(s);
	cell_settle();
	seam_before = env_seam_total();
	cb_before = env_cb_total();
	usb_kill_anchored_urbs(&anchor);
	env_op_bump();
	ok = g_anchor_in_cb_null == 1 && env_seam_total() == seam_before &&
	    env_cb_total() == cb_before && s->cb_count == 1;
	judge_expect(ok, "the completion unanchored the urb before the callback "
	    "(anchor==NULL in the callback) and left nothing for the anchor "
	    "kill to cancel", s->u, 1, g_anchor_in_cb_null);
	judge_note("anchor_null_in_cb=%d extra_completions=%u",
	    g_anchor_in_cb_null, env_cb_total() - cb_before);
	return cell_close(ok);
}

/* ------------------------------------------------------------------ */
/* B3 - kill / unlink interleave                                       */
/* ------------------------------------------------------------------ */

/*
 * B3.1 kill of a queued (worker-held) urb delivers -ENOENT exactly once.
 * Truth: lkpi_usb_backend.h: "Kill cancels a submitted transfer, delivers that
 * one completion itself and returns 0 when it owned the transfer"; the
 * controller's seam_kill reports -LKPI_USB_BE_ERR_CANCELLED and the engine maps
 * CANCELLED -> -ENOENT (linuxkpi_usb.c lkpi_usb_errno) - the Linux
 * usb_kill_urb cancellation code.  Two positions are measured: the transfer the
 * backend holds (the kill claims through the backend) and the transfer the
 * endpoint FIFO still holds (the engine delivers it inline in the killer's
 * context) - both synchronous for the killer (rule 3).
 */
static int
cell_b3_1(void)
{
	struct stress_urb *s1, *s2;
	osThreadId_t killer;
	int ok;

	cell_open("B3.1");
	env_knob_park();		/* the kill window: the backend holds it */
	s1 = row_bulk(1, -ENOENT, 1, 8);
	(void)env_submit(s1);
	s2 = row_bulk(1, -ENOENT, 1, 8);	/* queues in the endpoint FIFO */
	(void)env_submit(s2);
	killer = osThreadGetId();
	usb_kill_urb(s1->u);		/* backend-held: cancel + deliver */
	env_op_bump();
	judge_rule_kill_sync(s1->u, env_kill_ran_sync(s1));
	judge_rule_error_code(s1->u, -ENOENT, s1->cb_last);
	usb_kill_urb(s2->u);		/* FIFO-held: delivered in the killer */
	env_op_bump();
	judge_rule_kill_sync(s2->u, env_kill_ran_sync(s2));
	judge_rule_error_code(s2->u, -ENOENT, s2->cb_last);
	judge_rule_exactly_once(s1->u, 1, s1->cb_count, 1, s1->seam_count);
	judge_rule_exactly_once(s2->u, 1, s2->cb_count, 1, s2->submits);
	ok = s1->cb_count == 1 && s2->cb_count == 1;
	cell_settle();
	judge_note("backend_held status=%d cb_thread=%s; fifo_held status=%d "
	    "cb_thread=%s", s1->cb_last,
	    s1->cb_tid == killer ? "killer" : "drain", s2->cb_last,
	    s2->cb_tid == killer ? "killer" : "drain");
	return cell_close(ok);
}

/*
 * B3.2 an unlink of a queued urb behaves exactly like a kill: it IS the same
 * call.  Truth: linuxkpi_usb.c usb_unlink_urb() { usb_kill_urb(urb); } -
 * recorded here as the cell's observed fact, not a failure.
 */
static int
cell_b3_2(void)
{
	struct stress_urb *s;
	int ok;

	cell_open("B3.2");
	env_knob_park();
	s = row_bulk(1, -ENOENT, 1, 8);
	(void)env_submit(s);
	usb_unlink_urb(s->u);
	env_op_bump();
	ok = s->cb_count == 1 && s->cb_last == -ENOENT;
	judge_rule_kill_sync(s->u, env_kill_ran_sync(s));
	judge_rule_error_code(s->u, -ENOENT, s->cb_last);
	cell_settle();
	judge_note("unlink==kill in this engine (the same call): status=%d "
	    "count=%d", s->cb_last, s->cb_count);
	return cell_close(ok);
}

/*
 * B3.3 a double kill is idempotent and the completions stay exactly one.
 * Truth: linuxkpi_usb.c usb_kill_urb: the killer pins the urb, quiesces by
 * actual ownership and returns; a second kill finds the urb idle and delivers
 * nothing (the delivery chain manufactured one completion, not two).
 */
static int
cell_b3_3(void)
{
	struct stress_urb *s;
	int ok;

	cell_open("B3.3");
	env_knob_park();
	s = row_bulk(1, -ENOENT, 1, 8);
	(void)env_submit(s);
	usb_kill_urb(s->u);
	env_op_bump();
	usb_kill_urb(s->u);		/* the idempotent second kill */
	env_op_bump();
	cell_settle();
	ok = s->cb_count == 1 && s->seam_count == 1;
	judge_rule_exactly_once(s->u, 1, s->cb_count, 1, s->seam_count);
	judge_note("double_kill callbacks=%d seam=%d", s->cb_count,
	    s->seam_count);
	return cell_close(ok);
}

/* ------------------------------------------------------------------ */
/* B3.4: the killer is a second task so the barrier window is real      */
/* ------------------------------------------------------------------ */

static osSemaphoreId_t g_kill_go;
static volatile int g_kill_done;
static volatile int g_kill_cb_at_return;
static struct stress_urb *g_kill_target;

static void
killer_task(void *arg)
{
	(void)arg;
	for (;;) {
		(void)osSemaphoreAcquire(g_kill_go, osWaitForever);
		usb_kill_urb(g_kill_target->u);
		env_op_bump();
		g_kill_cb_at_return = g_kill_target->cb_count;
		g_kill_done = 1;
	}
}

static int
wait_flag(volatile int *flag, unsigned int ms)
{
	unsigned int i;

	for (i = 0; i < ms; i++) {
		if (*flag)
			return 0;
		(void)osDelay(1u);
	}
	return -1;
}

/*
 * B3.4 a resubmit while the kill barrier is up is refused with -EPERM.
 * Truth: linuxkpi_usb.c usb_submit_urb: a submission whose urb carries
 * _kill_barrier is refused with -EPERM ("rule 4 (kill_count, source
 * :425-428)"); the barrier is set before the kill examines any queue or
 * controller state, and it is cleared only after the urb quiesced.
 *
 * Window discipline (why this is deterministic): the killer is a task above
 * the harness band, so while it runs the harness does not; the harness first
 * yields to it (bounded poll for the barrier), then probes in a tight,
 * non-yielding loop.  Nothing else can run during that loop - the engine's
 * drain and the controller's worker sit below - so the barrier cannot fall
 * until the loop is done, and every attempt in it answers -EPERM.  If the
 * window was missed (the kill finished before the harness was scheduled
 * again), the scenario retries with one more parked submission, so the cell
 * cannot fail on scheduler luck.  The killer's own sample at its return
 * proves the kill drained synchronously (rule 3).
 */
static int
cell_b3_4(void)
{
	osThreadAttr_t attr = { 0 };
	osSemaphoreAttr_t sattr = { 0 };
	static osThreadId_t killer;
	struct stress_urb *s;
	int ok, i, attempt, eperm = 0, accepted_in_window = 0, other = 0;
	int barrier_seen = 0, rc_post = 0;

	cell_open("B3.4");
	if (g_kill_go == NULL) {
		sattr.name = "kill_go";
		g_kill_go = osSemaphoreNew(4u, 0u, &sattr);
		attr.name = "killer";
		attr.stack_size = 4096u;
		/* above the harness band: releasing its go-semaphore readies it and
		 * it raises the barrier; the harness then yields to it below */
		attr.priority = osPriorityHigh;
		killer = osThreadNew(killer_task, NULL, &attr);
		if (g_kill_go == NULL || killer == NULL)
			judge_expect(0, "the killer task was created", NULL, 1,
			    0);
	}
	s = row_bulk(4, -ENOENT, 1, 8);
	g_kill_target = s;
	for (attempt = 0; attempt < 4 && eperm == 0; attempt++) {
		env_knob_park();
		if (env_submit(s) != 0)
			judge_expect(0, "the parked submission was accepted", s->u,
			    0, -1);
		env_reset_knobs();
		g_kill_done = 0;
		g_kill_cb_at_return = 0;
		(void)osSemaphoreRelease(g_kill_go);
		/* yield to the killer until it has raised the barrier (or the
		 * kill already finished, in which case the attempt is retried) */
		for (i = 0; i < 400; i++) {
			if (s->u->_kill_barrier != 0 || g_kill_done)
				break;
			(void)osDelay(1u);
		}
		if (s->u->_kill_barrier == 0)
			continue;	/* missed: retry with a new submission */
		barrier_seen = 1;
		/* the barrier cannot fall while this loop spins (no yield) */
		for (i = 0; i < 16; i++) {
			int rc = usb_submit_urb(s->u, GFP_KERNEL);

			env_op_bump();
			if (rc == -EPERM)
				eperm++;
			else if (rc == 0)
				accepted_in_window++;
			else
				other++;
		}
	}
	judge_expect(barrier_seen && eperm > 0 && accepted_in_window == 0,
	    "a resubmit during the kill barrier is refused with -EPERM and is "
	    "never accepted inside the window", s->u, 1, eperm);
	ok = barrier_seen && eperm > 0 && accepted_in_window == 0;
	/* yield: the drain delivers, the killer returns */
	ok = (wait_flag(&g_kill_done, 4000) == 0) && ok;
	judge_rule_kill_sync(s->u, env_kill_ran_sync(s));
	/* the post-barrier half: the very same submit is accepted again */
	env_knob_passthrough();
	rc_post = env_submit(s);
	cell_settle();
	ok = ok && rc_post == 0 && s->cb_count == s->submits;
	judge_rule_exactly_once(s->u, s->submits, s->cb_count, s->submits,
	    s->seam_count);
	judge_rule_error_code(s->u, -ENOENT, s->cb_first);
	judge_rule_error_code(s->u, 0, s->cb_last);
	judge_note("barrier_seen=%d eperm=%d accepted_in_window=%d other=%d "
	    "attempts=%d killer_returned=%d post_barrier_rc=%d completions=%d",
	    barrier_seen, eperm, accepted_in_window, other, attempt,
	    g_kill_done, rc_post, s->cb_count);
	return cell_close(ok);
}

/*
 * B3.5 kill after free is a named refusal.
 * Truth: linuxkpi_usb.c usb_kill_urb: a released urb (the freed ring or the
 * poison identity) is named ("PROBE kill of DEAD urb ... refused") and refused
 * before any field is walked; the refusal manufactures no completion.  As in
 * B1.3 the identity is written into live storage.
 */
static int
cell_b3_5(void)
{
	struct stress_urb *s = row_bulk(1, LAWFUL_ANY, 1, 8);
	struct urb *up = s->u;
	unsigned int seam_before, cb_before;
	int named, ok;

	cell_open("B3.5");
	env_free_urb(s);		/* the real release */
	seam_before = env_seam_total();
	cb_before = env_cb_total();
	usb_kill_urb(up);		/* the released-ring guard runs first */
	env_op_bump();
	named = lkpi_usb_urb_is_freed(up);
	judge_expect(named == 1 && env_seam_total() == seam_before &&
	    env_cb_total() == cb_before, "a kill of a released urb is named "
	    "(lkpi_usb_urb_is_freed) and manufactures no completion", up, 1,
	    named);
	ok = named == 1 && env_cb_total() == cb_before;
	judge_note("named=%d completions_added=%u (the freed-ring check runs "
	    "before any field of the released block is read)", named,
	    env_cb_total() - cb_before);
	return cell_close(ok);
}

/*
 * B3.6 kill of an urb whose completion already ran is harmless.
 * Truth: the engine's kill quiesces by actual ownership; a delivered urb is
 * idle, so the kill has nothing to cancel (linuxkpi_usb.c usb_kill_urb ->
 * lkpi_usb_kill_quiesce: qs == IDLE -> return).
 */
static int
cell_b3_6(void)
{
	struct stress_urb *s = row_bulk(1, 0, 1, 8);
	unsigned int seam_before, cb_before;
	int ok;

	cell_open("B3.6");
	(void)env_submit(s);
	cell_settle();
	seam_before = env_seam_total();
	cb_before = env_cb_total();
	usb_kill_urb(s->u);
	env_op_bump();
	ok = s->cb_count == 1 && env_seam_total() == seam_before &&
	    env_cb_total() == cb_before;
	judge_rule_exactly_once(s->u, 1, s->cb_count, 1, s->seam_count);
	judge_note("post-completion kill: extra_completions=%u",
	    env_cb_total() - cb_before);
	return cell_close(ok);
}

/* ------------------------------------------------------------------ */
/* B4 - the anchor family                                              */
/* ------------------------------------------------------------------ */

/*
 * B4.1 N anchored urbs, then the batch kill delivers each exactly once with
 * the cancellation code.
 * Truth: linuxkpi_usb.c usb_kill_anchored_urbs: a head-unlink loop, each urb
 * killed outside the lock; the kill delivers -ENOENT for a submission that
 * never reached the controller (the Linux urb.c cancellation code).
 */
static int
cell_b4_1(void)
{
	struct usb_anchor anchor;
	struct stress_urb *s[3];
	unsigned int i, n = 3, cancelled = 0;

	cell_open("B4.1");
	init_usb_anchor(&anchor);
	env_knob_park();
	for (i = 0; i < n; i++) {
		s[i] = row_bulk(1, -ENOENT, (int)(i % 2u), 8 + i);
		usb_anchor_urb(s[i]->u, &anchor);
		(void)env_submit(s[i]);
	}
	usb_kill_anchored_urbs(&anchor);
	env_op_bump();
	for (i = 0; i < n; i++) {
		judge_rule_kill_sync(s[i]->u, env_kill_ran_sync(s[i]));
		judge_rule_error_code(s[i]->u, -ENOENT, s[i]->cb_last);
		judge_rule_exactly_once(s[i]->u, 1, s[i]->cb_count, 1,
		    s[i]->submits);
		if (s[i]->cb_last == -ENOENT && s[i]->cb_count == 1)
			cancelled++;
	}
	judge_expect(cancelled == n, "every anchored urb was cancelled "
	    "exactly once with the cancellation code", NULL, (long)n,
	    (long)cancelled);
	cell_settle();
	judge_note("anchored=%u cancelled=%u", n, cancelled);
	return cell_close(cancelled == n);
}

/*
 * B4.2 anchoring a new urb into a killed anchor: the measured behaviour.
 * Truth (measured this run): usb_anchor_urb() (linuxkpi_usb.c) takes the anchor
 * reference and links the urb without consulting anchor->killed - this port has
 * no poisoned-anchor concept, so a killed anchor keeps accepting members and a
 * later batch kill cancels them too.  The cell asserts only the invariants (no
 * crash, exactly-once) and records the measured rule; a driver must gate its own
 * submissions on anchor->killed.
 */
static int
cell_b4_2(void)
{
	struct usb_anchor anchor;
	struct stress_urb *s1, *s2;
	int ok;

	cell_open("B4.2");
	init_usb_anchor(&anchor);
	env_knob_park();
	s1 = row_bulk(1, -ENOENT, 1, 8);
	usb_anchor_urb(s1->u, &anchor);
	(void)env_submit(s1);
	usb_kill_anchored_urbs(&anchor);	/* the anchor is now killed */
	judge_rule_error_code(s1->u, -ENOENT, s1->cb_last);
	/* measured: the killed anchor still accepts a member */
	s2 = row_bulk(1, -ENOENT, 1, 8);
	usb_anchor_urb(s2->u, &anchor);
	(void)env_submit(s2);
	usb_kill_anchored_urbs(&anchor);
	env_op_bump();
	ok = s1->cb_count == 1 && s2->cb_count == 1 && s2->cb_last == -ENOENT;
	judge_rule_exactly_once(s2->u, 1, s2->cb_count, 1, s2->submits);
	judge_note("poisoned-anchor concept=absent (measured): the killed "
	    "anchor accepted a member and the batch kill cancelled it exactly "
	    "once (status=%d)", s2->cb_last);
	cell_settle();
	return cell_close(ok);
}

/* B4.3's observation: the anchor was already empty inside the callback */
static int g_unanchor_in_cb;

static void
observe_unanchor(struct stress_urb *s, struct urb *u)
{

	(void)s;
	if (u == s->u && u->anchor == NULL)
		g_unanchor_in_cb++;
}

/*
 * B4.3 a natural completion unanchors the urb: the anchor empties itself and a
 * later batch kill finds nothing (no phantom completions).
 * Truth: linuxkpi_usb.c lkpi_usb_deliver drops the anchor reference before the
 * callback (usb_unanchor_urb).
 */
static int
cell_b4_3(void)
{
	struct usb_anchor anchor;
	struct stress_urb *s[3];
	unsigned int i, seam_before, cb_before;
	int ok;

	cell_open("B4.3");
	init_usb_anchor(&anchor);
	g_unanchor_in_cb = 0;
	for (i = 0; i < 3; i++) {
		s[i] = row_bulk(1, 0, 1, 8 + i);
		s[i]->observe = observe_unanchor;
		usb_anchor_urb(s[i]->u, &anchor);
		(void)env_submit(s[i]);
	}
	cell_settle();
	seam_before = env_seam_total();
	cb_before = env_cb_total();
	usb_kill_anchored_urbs(&anchor);
	env_op_bump();
	ok = g_unanchor_in_cb == 3 && env_cb_total() == cb_before &&
	    env_seam_total() == seam_before;
	judge_expect(ok, "all three completions unanchored before their "
	    "callbacks and the anchor kill found nothing to cancel", NULL, 3,
	    g_unanchor_in_cb);
	judge_note("unanchored_in_cb=%u phantom_completions=%u",
	    g_unanchor_in_cb, env_cb_total() - cb_before);
	return cell_close(ok);
}

/*
 * B4.4 unanchoring an unanchored urb is harmless.
 * Truth: linuxkpi_usb.c usb_unanchor_urb: a NULL anchor returns immediately; a
 * second unanchor of an already unanchored urb is the same no-op.
 */
static int
cell_b4_4(void)
{
	struct usb_anchor anchor;
	struct stress_urb *s = row_bulk(1, 0, 1, 8);
	unsigned int seam_before, cb_before;
	int ok;

	cell_open("B4.4");
	init_usb_anchor(&anchor);
	seam_before = env_seam_total();
	cb_before = env_cb_total();
	usb_unanchor_urb(s->u);		/* never anchored */
	usb_anchor_urb(s->u, &anchor);
	usb_unanchor_urb(s->u);
	usb_unanchor_urb(s->u);		/* the harmless second unanchor */
	env_op_bump();
	(void)env_submit(s);
	cell_settle();
	ok = s->cb_count == 1 && s->cb_last == 0 &&
	    env_seam_total() == seam_before + 1 &&
	    env_cb_total() == cb_before + 1;
	judge_rule_exactly_once(s->u, 1, s->cb_count, 1, s->seam_count);
	judge_note("unanchor_noop completions=%u", s->cb_count);
	return cell_close(ok);
}

/* ------------------------------------------------------------------ */
/* B5 - injection combinations                                         */
/* ------------------------------------------------------------------ */

/*
 * B5.1 stall, then usb_clear_halt, then a successful transfer.
 * Truth: the controller's one-shot MOCK_FAULT_STALL answers the next transfer
 * with -LKPI_USB_BE_ERR_STALL (mock_hcd.c bulk_transfer), which the engine
 * reports as -EPIPE (Linux EPIPE = endpoint stalled); usb_clear_halt drives
 * CLEAR_FEATURE(ENDPOINT_HALT) through the control lane.  Measured fact: the
 * device model's halt flag is only ever cleared (the scripted stall is one-shot
 * and does not latch the endpoint), so the cell proves the recovery sequence -
 * the stall surfaces as -EPIPE, clear_halt is accepted, and the next transfer
 * on the same pipe succeeds.
 */
static int
cell_b5_1(void)
{
	struct stress_urb *s1, *s2;
	int rc_clear, ok;

	cell_open("B5.1");
	env_knob_passthrough();
	mock_hcd_fault_set(env_backend(), MOCK_FAULT_STALL);
	s1 = row_bulk(1, -EPIPE, 1, 8);
	(void)env_submit(s1);
	cell_settle();
	judge_rule_error_code(s1->u, -EPIPE, s1->cb_last);

	rc_clear = usb_clear_halt(env_device(),
	    usb_rcvbulkpipe(env_device(), 1));
	env_op_bump();
	judge_expect(rc_clear == 0, "usb_clear_halt is accepted after the "
	    "stall", NULL, 0, rc_clear);

	s2 = row_bulk(1, 0, 1, 8);
	(void)env_submit(s2);
	cell_settle();
	judge_rule_error_code(s2->u, 0, s2->cb_last);
	ok = s1->cb_last == -EPIPE && rc_clear == 0 && s2->cb_last == 0;
	judge_note("stall_status=%d clear_halt=%d next_status=%d", s1->cb_last,
	    rc_clear, s2->cb_last);
	return cell_close(ok);
}

/*
 * B5.2 a synchronous control transfer against a never-answering device returns
 * -ETIMEDOUT.
 * Truth: Linux usb_control_msg reports -ETIMEDOUT when the transfer did not
 * complete inside its timeout, and the engine maps the seam's
 * LKPI_USB_BE_ERR_TIMEOUT to -ETIMEDOUT (linuxkpi_usb.c lkpi_usb_errno).
 * Knob: the harness's never-answering control lane (engine_env.c
 * env_knob_control_timeout).  The honest alternative is measured too: a control
 * transfer against a detached port answers -ENODEV immediately - the round-A
 * controller is synchronous and cannot hang a transfer.
 */
static int
cell_b5_2(void)
{
	u8 buf[8];
	int rc_timeout, rc_detached, ok;

	cell_open("B5.2");
	env_knob_control_timeout(1);
	rc_timeout = usb_control_msg(env_device(), usb_rcvctrlpipe(env_device(),
	    0), 0x06, 0x80, 0x0100, 0, buf, 8, 1000);
	env_op_bump();
	judge_expect(rc_timeout == -ETIMEDOUT, "a never-answering control "
	    "transfer returns -ETIMEDOUT", NULL, -ETIMEDOUT, rc_timeout);
	env_knob_control_timeout(0);
	mock_hcd_disconnect(env_backend());
	rc_detached = usb_control_msg(env_device(), usb_rcvctrlpipe(env_device(),
	    0), 0x06, 0x80, 0x0100, 0, buf, 8, 1000);
	env_op_bump();
	mock_hcd_connect(env_backend());
	ok = rc_timeout == -ETIMEDOUT;
	judge_note("never-answer knob -> %d; detached port -> %d (the model "
	    "cannot hang a control transfer)", rc_timeout, rc_detached);
	return cell_close(ok);
}

/*
 * B5.3 a disconnect with N transfers in flight and M queued drains all of them
 * with named codes and balanced accounting.
 * Truth: the controller answers a detached port with
 * -LKPI_USB_BE_ERR_SHUTDOWN (mock_hcd.c bulk_transfer), and the engine maps
 * SHUTDOWN -> -ENODEV (linuxkpi_usb.c lkpi_usb_errno); the engine's endpoint
 * FIFO keeps the queued transfers and re-arms each after a drain
 * (lkpi_usb_kick), so they take the same -ENODEV.
 */
static int
cell_b5_3(void)
{
	struct stress_urb *s[4];
	unsigned int i, n_enodev = 0;
	int ok;

	cell_open("B5.3");
	env_knob_passthrough();
	/* N = 2 at the controller (bulk IN and bulk OUT), M = 2 queued behind
	 * them in the engine's per-endpoint FIFOs */
	s[0] = row_bulk(1, -ENODEV, 1, 8);
	s[1] = row_bulk(1, -ENODEV, 0, 8);
	s[2] = row_bulk(1, -ENODEV, 1, 8);
	s[3] = row_bulk(1, -ENODEV, 0, 8);
	for (i = 0; i < 4; i++)
		(void)env_submit(s[i]);
	mock_hcd_disconnect(env_backend());
	cell_settle();
	mock_hcd_connect(env_backend());
	for (i = 0; i < 4; i++) {
		if (s[i]->cb_count == 1 && s[i]->cb_last == -ENODEV)
			n_enodev++;
	}
	judge_expect(n_enodev == 4, "all N in flight + M queued drained with "
	    "-ENODEV, exactly once each", NULL, 4, (long)n_enodev);
	cell_check_all();
	ok = n_enodev == 4;
	judge_note("inflight=2 queued=2 drained=%u", n_enodev);
	return cell_close(ok);
}

/*
 * B5.4 the allocation seam's alloc-fail path.
 * Truth: the seam counts a failed allocation (linuxkpi_usb.h:
 * lkpi_usb_mem_stats.alloc_fail) and both allocation faces return NULL
 * (linuxkpi_usb.c usb_alloc_urb / lkpi_usb_alloc_coherent -> kzalloc).
 * Reachability through the public face: a size above any host allocator's
 * ceiling makes the host allocator fail, which is the seam's alloc_fail case.
 * The urb face is driven the same way, but its size is derived from an iso
 * packet count that stays below the host ceiling even at INT_MAX, so only the
 * coherent face can be driven into a hard failure here - recorded, not faked:
 * the cell exercises the coherent face with two absurd sizes and takes the
 * seam's named counter as the evidence.
 */
static int
cell_b5_4(void)
{
	struct lkpi_usb_mem_stats before, after;
	void *p, *q;
	int ok;

	cell_open("B5.4");
	env_stats(&before);
	p = lkpi_usb_alloc_coherent((size_t)-1 / 2u, GFP_KERNEL);
	q = lkpi_usb_alloc_coherent((size_t)-1 - 4095u, GFP_KERNEL);
	env_stats(&after);
	g_want_af += 2;
	judge_rule_uaf_named(NULL, &before, &after, 0, 0, 0, 2);
	judge_expect(p == NULL && q == NULL, "the coherent allocator refuses "
	    "absurd sizes and names each in alloc_fail", NULL, 0,
	    (p != NULL) + (q != NULL));
	ok = p == NULL && q == NULL;
	judge_note("coherent=%p,%p alloc_fail_delta=%u (the urb face's size stays "
	    "below the host ceiling even at INT_MAX iso packets - unreachable "
	    "here)", p, q, after.alloc_fail - before.alloc_fail);
	return cell_close(ok);
}

/*
 * B5.5 scripted per-transfer error codes surface with the matching Linux errno
 * through the completion status.
 * Truth: the engine's seam-to-errno map (linuxkpi_usb.c lkpi_usb_errno):
 * STALL->-EPIPE, SHORT->-EREMOTEIO, NOMEM->-ENOMEM, TIMEOUT->-ETIMEDOUT,
 * SHUTDOWN->-ENODEV, CANCELLED->-ENOENT.  The scripted device model produces
 * STALL (the endpoint-descriptor read) and SHORT (a device read shorter than
 * wLength); the harness's error knob programs the remaining four codes (and the
 * two scripted ones again, so the mapping is checked on both routes).
 */
static int
cell_b5_5(void)
{
	struct stress_urb *s;
	static const int seam[6] = {
		-LKPI_USB_BE_ERR_STALL, -LKPI_USB_BE_ERR_SHORT,
		-LKPI_USB_BE_ERR_NOMEM, -LKPI_USB_BE_ERR_TIMEOUT,
		-LKPI_USB_BE_ERR_SHUTDOWN, -LKPI_USB_BE_ERR_CANCELLED,
	};
	static const int want[6] = {
		-EPIPE, -EREMOTEIO, -ENOMEM, -ETIMEDOUT, -ENODEV, -ENOENT,
	};
	unsigned int i, matched = 0;
	int ok;

	cell_open("B5.5");
	mock_hcd_script(env_backend(), MOCK_SCRIPT_STALL_EP_DESC, 1);
	env_knob_passthrough();
	s = row_ctrl(1, -EPIPE, 0x06, 0x80, 0x0500, 0, 7);
	(void)env_submit(s);
	cell_settle();
	judge_rule_error_code(s->u, -EPIPE, s->cb_last);
	if (s->cb_last == -EPIPE)
		matched++;
	mock_hcd_script(env_backend(), MOCK_SCRIPT_STALL_EP_DESC, 0);
	mock_hcd_script(env_backend(), MOCK_SCRIPT_SHORT_DEVICE, 1);
	s = row_ctrl(1, -EREMOTEIO, 0x06, 0x80, 0x0100, 0, 1024);
	(void)env_submit(s);
	cell_settle();
	judge_rule_error_code(s->u, -EREMOTEIO, s->cb_last);
	if (s->cb_last == -EREMOTEIO)
		matched++;
	mock_hcd_script(env_backend(), MOCK_SCRIPT_SHORT_DEVICE, 0);
	for (i = 0; i < 6; i++) {
		env_knob_error(seam[i]);
		s = row_bulk(1, want[i], 1, 8);
		(void)env_submit(s);
		cell_settle();
		env_knob_error(0);
		if (s->cb_last == want[i])
			matched++;
		judge_rule_error_code(s->u, want[i], s->cb_last);
	}
	ok = matched == 8;
	judge_note("scripted=2 knob=6 matched=%u", matched);
	return cell_close(ok);
}

/* ------------------------------------------------------------------ */
/* B6 - timing and reentrancy (measured behaviour + invariants)        */
/* ------------------------------------------------------------------ */

/*
 * B6.1 out-of-order completion keeps one-to-one pairing.
 * The harness completes the batch in reverse submit order (its deterministic
 * order knob over the transfers due in the same step); the engine's
 * per-endpoint single flight bounds one batch to the three distinct endpoints.
 * Truth for the pairing: lkpi_usb_backend.h's one-completion-per-transfer
 * contract holds whatever order the transfers finish in.
 */
static int
cell_b6_1(void)
{
	struct stress_urb *s[3];
	unsigned int i, ok_order = 0;
	int ok;

	cell_open("B6.1");
	env_knob_order(ENV_ORDER_REVERSE);
	s[0] = row_bulk(1, 0, 1, 8);
	s[1] = row_bulk(1, 0, 0, 8);
	s[2] = row_ctrl(1, 0, 0x06, 0x80, 0x0100, 0, 18);
	for (i = 0; i < 3; i++)
		(void)env_submit(s[i]);
	env_step();
	(void)env_wait_cb(env_seam_total());
	/* the completion sequence numbers are process-wide: the batch is checked
	 * for a strict reverse order, not against absolute values */
	if (s[2]->cb_seq < s[1]->cb_seq && s[1]->cb_seq < s[0]->cb_seq)
		ok_order = 1;
	judge_note("seq=%u,%u,%u", s[0]->cb_seq, s[1]->cb_seq, s[2]->cb_seq);
	judge_expect(ok_order == 1, "the completions arrived in reverse submit "
	    "order and each urb completed exactly once", s[0]->u, 1, ok_order);
	cell_check_all();
	ok = ok_order == 1;
	judge_note("completion_order=%u,%u,%u", s[0]->cb_seq, s[1]->cb_seq,
	    s[2]->cb_seq);
	env_knob_order(ENV_ORDER_FIFO);
	cell_settle();
	return cell_close(ok);
}

/*
 * B6.2 kill inside a completion callback.
 * Measured this run (both shapes, invariants only):
 *   - killing the completing urb from its own callback is a bounded no-op: the
 *     engine's drain set the CALLBACK state before invoking the callback, the
 *     kill finds nothing to cancel, waits for the state to change, gives up on
 *     its bounded wait (its own "kill wait timeout" latch prints) and returns.
 *     The callback is not reentered, no second completion is manufactured, no
 *     deadlock.
 *   - killing a different queued urb from inside the callback drains that urb
 *     inline on the calling (drain) thread with the cancellation code.
 */
static struct stress_urb *g_kill_cb_victim;
static int g_kill_self_done;
static int g_kill_victim_done;
static int g_kill_victim_noted;

static void
observe_kill_in_cb(struct stress_urb *s, struct urb *u)
{
	(void)s;
	if (!g_kill_victim_noted && g_kill_cb_victim != NULL) {
		/* the victim is still parked at the backend: the kill cancels it
		 * from this context */
		usb_kill_urb(g_kill_cb_victim->u);
		env_op_bump();
		g_kill_victim_done = 1;
		g_kill_victim_noted = 1;
	}
	usb_kill_urb(u);		/* the completing urb itself */
	env_op_bump();
	g_kill_self_done = 1;
}

static int
cell_b6_2(void)
{
	struct stress_urb *self, *v1, *v2;
	unsigned int steps;
	int ok;

	cell_open("B6.2");
	/* the OUT endpoint: v1 parks at the backend (the kill window stays open),
	 * v2 queues in the engine's endpoint FIFO behind it */
	env_knob_park();
	v1 = row_bulk(1, -ENOENT, 0, 8);
	(void)env_submit(v1);
	env_reset_knobs();
	v2 = row_bulk(1, -ENOENT, 0, 8);
	(void)env_submit(v2);
	g_kill_cb_victim = v2;
	g_kill_victim_done = 0;
	g_kill_victim_noted = 0;
	g_kill_self_done = 0;
	/* the IN endpoint: this transfer completes and its callback runs */
	self = row_bulk(1, 0, 1, 8);
	self->observe = observe_kill_in_cb;
	(void)env_submit(self);
	steps = env_settle();
	judge_rule_no_deadlock(steps, ENV_STEP_BUDGET);
	judge_rule_exactly_once(self->u, 1, self->cb_count, 1, self->seam_count);
	judge_rule_error_code(self->u, 0, self->cb_last);
	judge_rule_error_code(v2->u, -ENOENT, v2->cb_last);
	ok = g_kill_self_done == 1 && self->cb_count == 1 &&
	    self->cb_last == 0 && v2->cb_count == 1 && v2->cb_last == -ENOENT;
	judge_expect(ok, "kill inside the callback: the self-kill was a bounded "
	    "no-op (no deadlock, no second completion) and the other queued "
	    "urb was cancelled exactly once", self->u, 1, ok);
	judge_note("kill_self=measured bounded no-op (engine kill-wait latch), "
	    "no deadlock; kill_other_queued=%s status=%d same_thread_as_self=%s",
	    g_kill_victim_done ? "drained inline" : "not observed",
	    v2->cb_last,
	    v2->cb_count == 1 && v2->cb_tid == self->cb_tid ? "yes" : "no");
	self->want_count = 1;
	v2->want_count = 1;
	v2->want_status = -ENOENT;
	return cell_close(ok);
}

/* B6.3's observation */
static void
observe_free_in_cb(struct stress_urb *s, struct urb *u)
{

	lkpi_usb_mem_stats_get(&g_stats_in_cb);
	usb_free_urb(u);		/* the caller's transport-reference drop */
	env_note_released(s);		/* the block is gone: no later read of it */
	g_free_in_cb_seen = 1;
}

/*
 * B6.3 free inside a completion callback.
 * Truth: linuxkpi_usb.c lkpi_usb_drain_one calls usb_free_urb(urb) after the
 * callback ("the transport reference: the urb may go away here if the caller
 * already dropped its own reference"), and usb_free_urb drops plain references
 * before it releases.  Measured this run: the callback's free only drops one
 * reference - the release happens in the drain, after the callback returned - so
 * freeing inside the callback is safe and the seam pairing stays balanced.  The
 * cell asserts the invariants and records the deferred-release measurement.
 */
static int
cell_b6_3(void)
{
	struct stress_urb *s;
	struct lkpi_usb_mem_stats before, after;
	unsigned int freed_in_cb, freed_after;
	int ok;

	cell_open("B6.3");
	s = row_bulk(1, 0, 1, 8);
	s->observe = observe_free_in_cb;
	g_free_in_cb_seen = 0;
	memset(&g_stats_in_cb, 0, sizeof(g_stats_in_cb));
	env_stats(&before);
	(void)env_submit(s);
	cell_settle();
	env_stats(&after);
	freed_in_cb = g_stats_in_cb.n_urb_free - before.n_urb_free;
	freed_after = after.n_urb_free - g_stats_in_cb.n_urb_free;
	ok = g_free_in_cb_seen == 1 && s->cb_count == 1 && freed_in_cb == 0 &&
	    freed_after == 1;
	judge_expect(ok, "freeing the urb inside its callback is safe and the "
	    "release is deferred to the drain (pairing stays balanced)", s->u,
	    1, g_free_in_cb_seen);
	judge_rule_exactly_once(s->u, 1, s->cb_count, 1, s->seam_count);
	judge_note("free_inside_cb=measured safe; freed_in_cb=%u "
	    "freed_by_drain=%u", freed_in_cb, freed_after);
	/* the block was already released by the drain: drop the row only */
	env_forget(s);
	return cell_close(ok);
}

/* B6.4's observation */
static struct stress_urb *g_resubmit_cb_row;
static int g_resubmit_cb_rc;
static int g_resubmit_cb_tries;

static void
observe_resubmit_in_cb(struct stress_urb *s, struct urb *u)
{

	(void)u;
	if (s != g_resubmit_cb_row)
		return;
	if (s->cb_count == 1 && g_resubmit_cb_tries == 0) {
		g_resubmit_cb_tries++;
		g_resubmit_cb_rc = env_submit(s);
	}
}

/*
 * B6.4 a resubmit inside a completion callback is legal and lands.
 * Truth: linuxkpi_usb.c usb_submit_urb accepts a urb in the CALLBACK state
 * ("unless it is the legal in-callback rearm: the drain set CALLBACK before
 * running the driver's completion"), and the drain's tail re-arms it after the
 * callback ("if the callback resubmitted this urb it is QUEUED again").  The
 * cell asserts the rearm landed: exactly one new completion for the one new
 * accepted submission.
 */
static int
cell_b6_4(void)
{
	struct stress_urb *s = row_bulk(2, 0, 1, 8);
	int ok;

	cell_open("B6.4");
	s->observe = observe_resubmit_in_cb;
	g_resubmit_cb_row = s;
	g_resubmit_cb_rc = -999;
	g_resubmit_cb_tries = 0;
	(void)env_submit(s);
	cell_settle();
	ok = g_resubmit_cb_rc == 0 && s->submits == 2 && s->cb_count == 2 &&
	    s->cb_last == 0 && s->seam_count == 2;
	judge_expect(ok, "the in-callback rearm was accepted and completed "
	    "exactly once more", s->u, 2, s->cb_count);
	judge_rule_exactly_once(s->u, 2, s->cb_count, 2, s->seam_count);
	judge_note("in_callback_rearm rc=%d submits=%d completions=%d",
	    g_resubmit_cb_rc, s->submits, s->cb_count);
	return cell_close(ok);
}

/* ------------------------------------------------------------------ */
/* the flow-control family (B7)                                        */
/*                                                                     */
/* The firmware-download shape: a long burst of submissions on one     */
/* bulk-out endpoint while the controller's own pools run short.  The  */
/* driver must see every submission accepted and every transfer        */
/* completed exactly once with success - a spent pool is flow control, */
/* not an outcome.  Truth: the board adapter reports a spent pool as   */
/* -LKPI_USB_BE_ERR_NOMEM (ports/cherryusb/lkpi_usb_cherryusb.c:468),  */
/* the EHCI controller as -USB_ERR_NOMEM when its QH pool is spent,    */
/* and Linux's usb_submit_urb contract is that an error return means   */
/* the submission was not accepted and no completion will follow.      */
/*                                                                     */
/* Two shapes, both deterministic: (A) one transfer at a time with the */
/* shortage landing on the submit's own kick - the endpoint is idle,   */
/* so the park is observable inside the submit path itself; (B) eight  */
/* transfers per phase with the shortage on the FIRST submit, so seven */
/* queue behind a parked head, which is the queue depth a firmware     */
/* burst builds.                                                       */
/* ------------------------------------------------------------------ */

#define FLOW_SINGLE	600	/* phase A: one transfer at a time */
#define FLOW_PHASES	12	/* phase B: 12 x 8 = 96 submissions */
#define FLOW_ROWS	8
/* 600 + 96 = 696 submissions; one shortage in every ten, so the total is the
 * firmware burst's ~690 with 72 absorbed shortages */

static unsigned int g_flow_shortages;	/* injected transient refusals */

/* one submission on the shared arena slot */
static int
flow_submit(struct stress_urb *s, int arm_shortage)
{
	u8 *buf = env_buf(0, 16);

	if (buf == NULL)
		return -1;
	memset(buf, 0, 16);
	if (env_alloc(s, 1, 0) == NULL)
		return -1;
	env_fill_bulk(s, ENV_EP_BULK_OUT, buf, 16, 0);
	if (arm_shortage) {
		env_knob_submit_shortage(1);
		g_flow_shortages++;
	}
	return 0;
}

/*
 * Drive a phase to completion with bounded steps and yields.  The engine hands
 * its queued transfers over one at a time, so the wrapper's "all rows settled"
 * gate (env_settle) would wait on transfers the engine has not handed over yet;
 * stepping plus yielding drives exactly the queue the engine holds.
 */
static void
flow_drive(struct stress_urb *row[FLOW_ROWS], unsigned int n)
{
	unsigned int i, guard;

	for (guard = 0; guard < 512; guard++) {
		unsigned int done = 0;

		for (i = 0; i < n; i++)
			if (row[i]->cb_count != 0)
				done++;
		if (done == n)
			return;
		env_step();
		env_yield();
	}
}

/* phase A: the shortage lands on the submit's own kick */
static void
flow_single_round(struct stress_urb *s, int inject,
    unsigned int *bad_submits, unsigned int *bad_status)
{
	unsigned int parks0 = lkpi_usb_park_count();

	if (flow_submit(s, inject) != 0) {
		(*bad_submits)++;
		return;
	}
	if (env_submit(s) != 0)
		(*bad_submits)++;
	if (inject) {
		/* the endpoint was idle, so the refusal parked the head right
		 * here and nothing is in flight on it */
		if (lkpi_usb_parked_now() == 0 || lkpi_usb_park_count() == parks0)
			(*bad_submits)++;
		/* the driver's register-poll shape: one synchronous control
		 * transfer, which runs the parked-endpoint retry pass */
		(void)env_control_poke();
	}
	flow_drive(&s, 1);
	if (s->cb_count != 1 || s->cb_first != 0)
		(*bad_status)++;
	env_free_urb(s);
	env_forget(s);
}

/* phase B: seven transfers queue behind the parked head */
static void
flow_queue_phase(struct stress_urb *row[FLOW_ROWS], unsigned int *bad_submits,
    unsigned int *bad_status)
{
	unsigned int i, guard;
	unsigned int parks0 = lkpi_usb_park_count();

	/* The shortage is armed on the LAST submit: the phase's first transfer is
	 * already in flight, so the refusal lands on the drain's kick once that
	 * transfer completes, and the seven behind it stay queued.  With nothing
	 * in flight on the endpoint afterwards, only the retry pass can resume it -
	 * which is the case a firmware burst's tail really is. */
	for (i = 0; i < FLOW_ROWS; i++) {
		row[i] = &g_row[i];
		if (flow_submit(row[i], i == FLOW_ROWS - 1) != 0) {
			(*bad_submits)++;
			return;
		}
		if (env_submit(row[i]) != 0)
			(*bad_submits)++;
	}
	/* let the refusal land: the park is observable in the counter (the retry
	 * pass may resume it inside the same drain batch, which is the engine
	 * healing itself - the evidence is that it parked and resumed at all) */
	for (guard = 0; guard < 64 && lkpi_usb_park_count() == parks0; guard++) {
		env_step();
		env_yield();
	}
	if (lkpi_usb_park_count() != parks0 + 1)
		(*bad_submits)++;
	(void)env_control_poke();
	flow_drive(row, FLOW_ROWS);
	for (i = 0; i < FLOW_ROWS; i++) {
		if (row[i]->cb_count != 1 || row[i]->cb_first != 0)
			(*bad_status)++;
		env_free_urb(row[i]);
		env_forget(row[i]);
	}
}

/* B7.1 flow-return: the burst absorbs every transient shortage */
static int
cell_b7_1(void)
{
	struct stress_urb *row[FLOW_ROWS];
	unsigned int i, phase, bad_submits = 0, bad_status = 0;
	unsigned int parks0, resumes0, parks, resumes;
	int ok;

	cell_open("B7.1");
	parks0 = lkpi_usb_park_count();
	resumes0 = lkpi_usb_resume_count();
	g_flow_shortages = 0;
	for (i = 0; i < FLOW_SINGLE; i++)
		flow_single_round(&g_row[0], (i % 10) == 9, &bad_submits,
		    &bad_status);
	for (phase = 0; phase < FLOW_PHASES; phase++)
		flow_queue_phase(row, &bad_submits, &bad_status);
	parks = lkpi_usb_park_count() - parks0;
	resumes = lkpi_usb_resume_count() - resumes0;
	ok = bad_submits == 0 && bad_status == 0 && g_flow_shortages != 0 &&
	    parks == g_flow_shortages && resumes == g_flow_shortages &&
	    lkpi_usb_parked_now() == 0;
	judge_expect(bad_submits == 0, "every submit of the burst is accepted "
	    "(a transient shortage is flow control, never -ENOMEM to the driver)",
	    NULL, 0, (long)bad_submits);
	judge_expect(bad_status == 0, "every transfer of the burst completes "
	    "exactly once with success", NULL, 0, (long)bad_status);
	judge_expect(parks == g_flow_shortages,
	    "every injected shortage parked the endpoint (the shortage was "
	    "absorbed, not absent)", NULL, (long)g_flow_shortages, (long)parks);
	judge_expect(resumes == g_flow_shortages,
	    "every park was resumed by the retry pass", NULL,
	    (long)g_flow_shortages, (long)resumes);
	judge_expect(lkpi_usb_parked_now() == 0, "no endpoint is left parked",
	    NULL, 0, (long)lkpi_usb_parked_now());
	judge_note("submits=%u shortages=%u parks=%u resumes=%u",
	    FLOW_SINGLE + FLOW_PHASES * FLOW_ROWS, g_flow_shortages, parks,
	    resumes);
	return cell_close(ok);
}

/* B7.2 the live control for B7.1's "no failure surfaced" assertion */
static int
cell_b7_2(void)
{
	struct stress_urb *row[FLOW_ROWS];
	unsigned int i, bad_status = 0, seen = 0;
	int ok;

	cell_open("B7.2");
	/* A HARD refusal (the device is gone) must reach the driver as a failed
	 * completion.  If this cell cannot produce a nonzero status, B7.1's
	 * check is dead code and would pass on any engine - so this is the
	 * control that keeps it live. */
	for (i = 0; i < FLOW_ROWS; i++) {
		row[i] = &g_row[i];
		if (flow_submit(row[i], 0) != 0)
			break;
		if (i == FLOW_ROWS - 1)
			env_knob_submit_refuse(LKPI_USB_BE_ERR_SHUTDOWN);
		(void)env_submit(row[i]);
	}
	flow_drive(row, FLOW_ROWS);
	for (i = 0; i < FLOW_ROWS; i++) {
		if (row[i] == NULL || row[i]->u == NULL)
			break;
		if (row[i]->cb_count == 1 && row[i]->cb_first != 0)
			bad_status++;
		seen += (unsigned int)row[i]->cb_count;
		env_free_urb(row[i]);
		env_forget(row[i]);
	}
	ok = bad_status >= 1;
	judge_expect(bad_status >= 1, "a hard refusal reaches the driver as a "
	    "failed completion (the detector B7.1 relies on is live)", NULL, 1,
	    (long)bad_status);
	judge_note("hard_refusal_visible=%u callbacks=%u", bad_status, seen);
	return cell_close(ok);
}

/* B7.3 submit-refused-no-completion: an unaccepted submit owes nothing */
static int
cell_b7_3(void)
{
	struct stress_urb *s;
	unsigned int seam0;
	int rc, ok;

	cell_open("B7.3");
	/* A submit the engine refuses at its own API: the pipe resolves to no
	 * endpoint of the device, so usb_submit_urb reports -EINVAL before the
	 * transfer is accepted.  An unaccepted submission owes no completion at
	 * all and never reaches the seam (Linux usb_submit_urb; the shape whose
	 * absence let a late giveback double-free a driver's txcb). */
	s = row_bulk(0, LAWFUL_ANY, 0, 8);
	s->u->pipe = usb_sndbulkpipe(env_device(), 7);
	seam0 = env_seam_total();
	rc = env_submit(s);
	flow_drive(&s, 1);
	ok = rc == -EINVAL && s->cb_count == 0 && s->seam_count == 0 &&
	    env_seam_total() == seam0;
	judge_expect(rc == -EINVAL, "a submit with no endpoint behind the pipe is "
	    "refused with -EINVAL", s->u, -EINVAL, rc);
	judge_expect(s->cb_count == 0 && s->seam_count == 0 &&
	    env_seam_total() == seam0, "a refused submit owes no completion and "
	    "never reaches the seam", s->u, 0, (long)s->cb_count);
	judge_note("refusal=%d cb=%d seam=%d", rc, s->cb_count, s->seam_count);
	return cell_close(ok);
}

/* B7.4 the late giveback after a refused submit is absorbed */
static int
cell_b7_4(void)
{
	struct stress_urb *s;
	int rc, poke, ok;

	cell_open("B7.4");
	/* The accepted-then-refused shape: the submission was accepted, so
	 * exactly one completion is owed and it carries the device error.  A
	 * LATE giveback for the same transfer - a completion for a submission
	 * that was refused - is the shim-contract violation that once
	 * double-freed a driver's txcb; the engine's claim guard must absorb it
	 * and the driver face must stay at one callback. */
	s = row_bulk(1, LAWFUL_ANY, 0, 8);
	env_knob_submit_refuse(LKPI_USB_BE_ERR_SHUTDOWN);
	rc = env_submit(s);
	flow_drive(&s, 1);
	poke = env_late_giveback_poke();
	flow_drive(&s, 1);
	ok = rc == 0 && poke == 0 && s->cb_count == 1 && s->cb_first == -ENODEV;
	judge_expect(rc == 0, "the submission is accepted (the refusal is the "
	    "controller's, not the API's)", s->u, 0, rc);
	judge_expect(s->cb_count == 1 && s->cb_first == -ENODEV,
	    "the accepted submission gets exactly one completion, carrying the "
	    "device error", s->u, 1, (long)s->cb_count);
	judge_expect(poke == 0 && s->cb_count == 1,
	    "the late giveback is absorbed: the driver face stays at one "
	    "callback", s->u, 1, (long)s->cb_count);
	judge_note("refused_submit rc=%d cb=%d status=%d late_giveback=%d",
	    rc, s->cb_count, s->cb_first, poke);
	return cell_close(ok);
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static const struct {
	const char *id;
	int (*run)(void);
} g_matrix[] = {
	{ "B1.1", cell_b1_1 }, { "B1.2", cell_b1_2 }, { "B1.3", cell_b1_3 },
	{ "B1.4", cell_b1_4 }, { "B1.5", cell_b1_5 },
	{ "B2.1", cell_b2_1 }, { "B2.2", cell_b2_2 }, { "B2.3", cell_b2_3 },
	{ "B2.4", cell_b2_4 },
	{ "B3.1", cell_b3_1 }, { "B3.2", cell_b3_2 }, { "B3.3", cell_b3_3 },
	{ "B3.4", cell_b3_4 }, { "B3.5", cell_b3_5 }, { "B3.6", cell_b3_6 },
	{ "B4.1", cell_b4_1 }, { "B4.2", cell_b4_2 }, { "B4.3", cell_b4_3 },
	{ "B4.4", cell_b4_4 },
	{ "B5.1", cell_b5_1 }, { "B5.2", cell_b5_2 }, { "B5.3", cell_b5_3 },
	{ "B5.4", cell_b5_4 }, { "B5.5", cell_b5_5 },
	{ "B6.1", cell_b6_1 }, { "B6.2", cell_b6_2 }, { "B6.3", cell_b6_3 },
	{ "B6.4", cell_b6_4 },
	{ "B7.1", cell_b7_1 }, { "B7.2", cell_b7_2 }, { "B7.3", cell_b7_3 },
	{ "B7.4", cell_b7_4 },
};

#define MATRIX_N	((unsigned int)(sizeof(g_matrix) / sizeof(g_matrix[0])))

unsigned int
matrix_cell_count(void)
{

	return MATRIX_N;
}

static int
matrix_run_one(unsigned int i)
{

	return g_matrix[i].run();
}

int
matrix_run_all(void)
{
	unsigned int i;

	env_stats(&g_base);
	g_want_df = 0;
	g_want_sf = 0;
	g_want_uaf = 0;
	g_want_af = 0;
	judge_repro_set("linuxkpi-usb-urb-stress");
	for (i = 0; i < MATRIX_N; i++) {
		env_op_bump();
		(void)matrix_run_one(i);	/* the cell reports its verdict */
	}
	judge_summary();
	return judge_fail_count() == 0 ? 0 : 1;
}

int
matrix_run_cell(const char *id)
{
	unsigned int i;

	env_stats(&g_base);
	g_want_df = g_want_sf = g_want_uaf = g_want_af = 0;
	for (i = 0; i < MATRIX_N; i++) {
		if (strcmp(g_matrix[i].id, id) == 0) {
			char repro[128];

			snprintf(repro, sizeof(repro),
			    "usb-urb-stress --cell %s", id);
			judge_repro_set(repro);
			(void)matrix_run_one(i);
			judge_summary();
			return judge_fail_count() == 0 ? 0 : 1;
		}
	}
	printf("URBSTRESS: unknown cell %s\n", id);
	return 2;
}

void
matrix_list_cells(void)
{
	unsigned int i;

	for (i = 0; i < MATRIX_N; i++)
		printf("URBSTRESS: cell %s\n", g_matrix[i].id);
	printf("URBSTRESS: matrix cells=%u\n", MATRIX_N);
}

/*
 * --fault double-complete: the must-go-red self-proof.
 * The knob gives one transfer a second giveback at the seam.  The engine's own
 * completion identity absorbs the duplicate (its endpoint is idle again), so the
 * driver face still sees one callback - the violation is exactly the seam's
 * exactly-once rule, and the run must name only the programmed urb and exit
 * non-zero.
 */
int
matrix_run_double_complete_fault(void)
{
	struct stress_urb *s;

	env_stats(&g_base);
	g_want_df = g_want_sf = g_want_uaf = g_want_af = 0;
	judge_repro_set("linuxkpi-usb-urb-stress --fault double-complete");
	cell_open("B6.0-fault");
	env_knob_double_complete();
	s = row_bulk(1, 0, 1, 8);
	(void)env_submit(s);
	cell_settle();
	/* the seam gave this one transfer back twice: the exactly-once rule must
	 * catch it, and only this urb may be named */
	judge_rule_exactly_once(s->u, 1, s->cb_count, 1, s->seam_count);
	judge_note("double_complete giveback absorbed by the engine (cb=%d) and "
	    "named by the seam rule (seam=%d)", s->cb_count, s->seam_count);
	return judge_cell_end(1);
}
