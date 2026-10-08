/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * fuzz.c - the seeded operation stream.
 *
 * A xorshift64* generator drives a stream of urb operations over the harness's
 * environment: allocate, allocate-coherent, fill bulk/control, submit, kill,
 * unlink, the anchor family, a control message, free - plus misuse operators
 * (free-freed, submit-freed, kill-freed, double-kill, submit-live) injected
 * with a declared probability.  The stream is a pure function of the seed:
 * same seed + iters -> the same digest, across runs and across binaries.
 *
 * The stream is cut into blocks of FUZZ_RING operations.  At the end of every
 * block the harness quiesces the world (kill what is in flight, settle, wait
 * for every callback), applies the six judgement rules, releases everything the
 * block allocated and prints one result line.  A violation stops the run at the
 * first witness through the judge's fixed witness format, naming the operation
 * index - which is also what --replay reproduces.
 *
 * Misuse-operator safety (the sanitizer build is the reason): the engine's
 * free and kill guards run a *pointer comparison* against the released-pointer
 * ring before touching any field, so misusing a genuinely released block is
 * safe on those two paths - and the harness still holds the stale pointer only
 * in a field it never dereferences.  The submit guard reads the block's identity
 * field before the pointing check, so a genuine use-after-free there would be an
 * access of released storage; that operator marks live storage with the same
 * poison identity instead, exactly as matrix cell B1.3 documents.
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

#define FUZZ_RING	4096u
#define FUZZ_ROWS	16u
#define FUZZ_BUF	64u
#define LAWFUL_ANY	0x7fffffff

/* the operator kinds (the declared stream vocabulary) */
enum fuzz_op {
	FZ_ALLOC = 0,
	FZ_ALLOC_COHERENT,
	FZ_FILL_BULK,
	FZ_FILL_CTRL,
	FZ_SUBMIT,
	FZ_KILL,
	FZ_UNLINK,
	FZ_ANCHOR,
	FZ_UNANCHOR,
	FZ_KILL_ANCHORED,
	FZ_CONTROL_MSG,
	FZ_FREE,
	FZ_MIS_FREE_FREED,
	FZ_MIS_SUBMIT_FREED,
	FZ_MIS_KILL_FREED,
	FZ_MIS_DOUBLE_KILL,
	FZ_MIS_SUBMIT_LIVE,
	FZ_OP_N,
};

/* one row's life: FREE (nothing) -> LIVE (allocated) -> SUBMITTED (owes a
 * completion) -> RELEASED (the block is gone; the row keeps the stale pointer
 * for the misuse operators only) */
enum fuzz_row_state {
	FRS_FREE = 0,
	FRS_LIVE,
	FRS_SUBMITTED,
	FRS_RELEASED,
};

struct fuzz_row {
	struct urb	*u;		/* the pointer in hand (stale when released) */
	u8		*buf;
	u8		setup[8];
	unsigned int	state;
	unsigned int	rel_seq;	/* the free counter at release time */
	int		in_anchor;
	struct stress_urb s;
};

struct fuzz_ring_ent {
	unsigned int	op;		/* the operation index */
	unsigned int	kind;
	unsigned int	row;
	int		rc;		/* the operator's own return */
};

static struct fuzz_row g_fr[FUZZ_ROWS];
static struct fuzz_ring_ent g_ring[FUZZ_RING];
static unsigned int g_ring_at;
static uint64_t g_rng;
static unsigned int g_op_index;
static struct usb_anchor g_anchor;
static char g_cell_id[64];
extern int g_replay_mode;	/* set by the entry point (main.c) */

/* the misuse tallies (printed with the block line, asserted to have moved) */
static unsigned int g_n_free_freed, g_n_submit_freed, g_n_kill_freed,
		    g_n_double_kill, g_n_submit_live;

/* ------------------------------------------------------------------ */
/* the generator (xorshift64*; the state is the whole determinism)      */
/* ------------------------------------------------------------------ */

static uint64_t
rng_next(void)
{
	uint64_t x = g_rng;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	g_rng = x;
	return x * 0x2545f4914f6cdd1dULL;
}

static unsigned int
rng_below(unsigned int n)
{

	return (unsigned int)((rng_next() >> 32) % n);
}

static void
ring_put(unsigned int kind, unsigned int row, int rc)
{

	g_ring[g_ring_at % FUZZ_RING].op = g_op_index;
	g_ring[g_ring_at % FUZZ_RING].kind = kind;
	g_ring[g_ring_at % FUZZ_RING].row = row;
	g_ring[g_ring_at % FUZZ_RING].rc = rc;
	g_ring_at++;
}

/* ------------------------------------------------------------------ */
/* row bookkeeping                                                     */
/* ------------------------------------------------------------------ */

static struct fuzz_row *released_pick(void);

static struct fuzz_row *
row_pick(unsigned int state_mask)
{
	unsigned int i, n = 0, pick;

	pick = rng_below(FUZZ_ROWS);	/* an index, so the pick is stable */
	for (i = 0; i < FUZZ_ROWS; i++) {
		struct fuzz_row *r = &g_fr[(pick + i) % FUZZ_ROWS];

		if ((state_mask & (1u << r->state)) == 0)
			continue;
		if (n++ == 0)
			return r;
	}
	/* empty pool: fall through with a plain scan so a first call still works */
	for (i = 0; i < FUZZ_ROWS; i++) {
		if (state_mask & (1u << g_fr[i].state))
			return &g_fr[i];
	}
	return NULL;
}

static void
row_reset(struct fuzz_row *r)
{

	r->u = NULL;
	r->buf = NULL;
	r->state = FRS_FREE;
	r->rel_seq = 0;
	r->in_anchor = 0;
	env_row_reset(&r->s);
}

/* ------------------------------------------------------------------ */
/* the operators                                                       */
/* ------------------------------------------------------------------ */

static void
op_alloc(void)
{
	struct fuzz_row *r = row_pick((1u << FRS_FREE) | (1u << FRS_RELEASED));

	if (r == NULL)
		return;
	row_reset(r);
	r->u = env_alloc(&r->s, 0, LAWFUL_ANY);
	if (r->u == NULL) {
		ring_put(FZ_ALLOC, FUZZ_ROWS, -ENOMEM);
		return;
	}
	r->state = FRS_LIVE;
	r->buf = env_buf((unsigned int)(r - g_fr), FUZZ_BUF);
	if (r->buf != NULL)
		memset(r->buf, (int)(r - g_fr), FUZZ_BUF);
	/* an allocated urb is a usable urb: the fill gives it its device and pipe
	 * identity, so a later submit operator always has something to submit */
	env_fill_bulk(&r->s, ENV_EP_BULK_IN, r->buf, 16u,
	    (int)(rng_next() & 1u));
	ring_put(FZ_ALLOC, (unsigned int)(r - g_fr), 0);
}

static void
op_alloc_coherent(void)
{
	/* the coherent pair must balance inside the operation itself; the seam's
	 * counters are the evidence read at the block boundary */
	void *p = lkpi_usb_alloc_coherent(FUZZ_BUF, GFP_KERNEL);
	int rc = p == NULL ? -ENOMEM : 0;

	if (p != NULL)
		lkpi_usb_free_coherent(p, FUZZ_BUF);
	ring_put(FZ_ALLOC_COHERENT, FUZZ_ROWS, rc);
}

static void
op_fill(struct fuzz_row *r, int ctrl)
{

	if (r == NULL)
		return;
	if (!ctrl) {
		int in = (rng_next() & 1u) != 0;
		unsigned int len = 8u + rng_below(FUZZ_BUF - 8u);

		memset(r->buf, 0x5a, len);
		env_fill_bulk(&r->s, ENV_EP_BULK_IN, r->buf, len, in);
		ring_put(FZ_FILL_BULK, (unsigned int)(r - g_fr), 0);
	} else {
		u16 value = (u16)(rng_next() & 0xffffu);
		unsigned int len = (rng_next() & 1u) != 0 ? 0u : 18u;

		memset(r->setup, 0, 8);
		r->setup[0] = 0x80;
		r->setup[1] = 0x06;
		r->setup[2] = (u8)value;
		r->setup[3] = (u8)(value >> 8);
		r->setup[6] = (u8)len;
		r->setup[7] = (u8)(len >> 8);
		env_fill_control(&r->s, r->setup, len != 0 ? r->buf : NULL, len);
		ring_put(FZ_FILL_CTRL, (unsigned int)(r - g_fr), 0);
	}
}

static void
op_submit(struct fuzz_row *r)
{
	int rc;

	if (r == NULL || r->state != FRS_LIVE)
		return;
	rc = env_submit(&r->s);
	if (rc == 0)
		r->state = FRS_SUBMITTED;
	ring_put(FZ_SUBMIT, (unsigned int)(r - g_fr), rc);
}

static void
op_cancel(struct fuzz_row *r, int unlink)
{

	if (r == NULL)
		return;
	if (unlink)
		usb_unlink_urb(r->u);
	else
		usb_kill_urb(r->u);
	env_op_bump();
	/* the kill is synchronous for the caller (judgement rule 3): the one
	 * completion this submission owed has run by the time it returns */
	if (r->state == FRS_SUBMITTED) {
		judge_rule_kill_sync(r->u, env_kill_ran_sync(&r->s));
		r->state = FRS_LIVE;
	}
	ring_put(unlink ? FZ_UNLINK : FZ_KILL, (unsigned int)(r - g_fr), 0);
}

/* anchor membership is read from the engine's own field (urb->anchor), never
 * from a harness flag: a natural completion unanchors the urb before its
 * callback (lkpi_usb_deliver), so a cached flag would go stale */
static void
op_anchor(struct fuzz_row *r)
{

	if (r == NULL || r->state == FRS_RELEASED || r->u->anchor != NULL)
		return;
	usb_anchor_urb(r->u, &g_anchor);
	r->in_anchor = 1;
	ring_put(FZ_ANCHOR, (unsigned int)(r - g_fr), 0);
}

static void
op_unanchor(struct fuzz_row *r)
{

	if (r != NULL && r->state != FRS_RELEASED && r->u->anchor != NULL)
		usb_unanchor_urb(r->u);
	if (r != NULL)
		r->in_anchor = 0;
	ring_put(FZ_UNANCHOR, r != NULL ? (unsigned int)(r - g_fr) : FUZZ_ROWS,
	    0);
}

static void
op_kill_anchored(void)
{
	unsigned int i;
	int members[FUZZ_ROWS];
	unsigned int n = 0, k;

	/* snapshot the members from the engine's own linkage: exactly the urbs
	 * this batch kill owns (a harness flag would go stale, because a natural
	 * completion unanchors the urb before its callback) */
	for (i = 0; i < FUZZ_ROWS; i++) {
		if (g_fr[i].state != FRS_RELEASED && g_fr[i].u != NULL &&
		    g_fr[i].u->anchor == &g_anchor)
			members[n++] = (int)i;
	}
	usb_kill_anchored_urbs(&g_anchor);
	env_op_bump();
	for (k = 0; k < n; k++) {
		struct fuzz_row *r = &g_fr[members[k]];

		r->in_anchor = 0;
		if (r->state != FRS_SUBMITTED)
			continue;
		/* a member that owed a completion got it exactly once before the
		 * batch kill returned (the engine's kill is synchronous for its
		 * caller) */
		judge_rule_kill_sync(r->u, env_kill_ran_sync(&r->s));
		r->state = FRS_LIVE;
	}
	ring_put(FZ_KILL_ANCHORED, FUZZ_ROWS, 0);
}

/* the control-lane operator: one GET_DESCRIPTOR through the synchronous
 * message helper (the class-control face every lawful answer is accepted on;
 * the value is recorded for the digest) */
static void
op_control_msg(void)
{
	u8 buf[18];
	u16 value = (u16)(rng_next() & 0x03ffu);
	int rc;

	memset(buf, 0, sizeof(buf));
	rc = usb_control_msg(env_device(), usb_rcvctrlpipe(env_device(), 0),
	    0x06, 0x80, value, 0, buf, 8, 1000);
	env_op_bump();
	ring_put(FZ_CONTROL_MSG, FUZZ_ROWS, rc);
}

/* the ordinary release.  Contract before the release: the urb must not stay in
 * an anchor list (the engine's anchor iterates its node, so releasing an
 * anchored urb would leave the list pointing into the freed block). */
static void
op_free(struct fuzz_row *r)
{
	struct lkpi_usb_mem_stats st;

	if (r == NULL)
		return;
	if (r->state != FRS_RELEASED && r->u->anchor != NULL) {
		usb_unanchor_urb(r->u);
		r->in_anchor = 0;
	}
	/* the engine kills a submission that still owes a completion before it
	 * releases the block */
	env_free_urb(&r->s);
	env_stats(&st);
	r->rel_seq = st.n_urb_free;
	r->state = FRS_RELEASED;
	r->in_anchor = 0;
	ring_put(FZ_FREE, (unsigned int)(r - g_fr), 0);
}

/* a released row whose pointer is still ring-safe; when none exists, one is
 * manufactured (allocated and released in place) so the misuse operators always
 * have a deterministic target */
static struct fuzz_row *released_pick(void);

static void
op_mis_free_freed(void)
{
	/* a second release of a released pointer: the engine's free path checks
	 * the released-pointer ring first (a pointer comparison, no field of the
	 * block is read) and names the hit in lkpi_usb_mem_stats.double_free */
	struct lkpi_usb_mem_stats before, after;
	struct fuzz_row *r = released_pick();

	if (r == NULL)
		return;
	env_stats(&before);
	usb_free_urb(r->u);
	env_stats(&after);
	g_n_free_freed++;
	/* the guard names it in the double-free tally and records the refused
	 * use-after-free write (linuxkpi_usb.c usb_free_urb: freed_hit ->
	 * double_free++ + lkpi_usb_uaf_hit) */
	judge_rule_uaf_named(r->u, &before, &after, 1, 0, 1, 0);
	ring_put(FZ_MIS_FREE_FREED, (unsigned int)(r - g_fr), 0);
}

static void
op_mis_kill_freed(void)
{
	/* a kill of a released pointer: the kill path checks the released ring
	 * first, names it and refuses; it manufactures no completion */
	struct fuzz_row *r = released_pick();
	unsigned int seam_before, cb_before;

	if (r == NULL)
		return;
	seam_before = env_seam_total();
	cb_before = env_cb_total();
	usb_kill_urb(r->u);
	env_op_bump();
	g_n_kill_freed++;
	judge_expect(lkpi_usb_urb_is_freed(r->u) == 1 &&
	    env_seam_total() == seam_before && env_cb_total() == cb_before,
	    "a kill of a released urb is named and refused", r->u, 1,
	    lkpi_usb_urb_is_freed(r->u));
	ring_put(FZ_MIS_KILL_FREED, (unsigned int)(r - g_fr), 0);
}

static void
op_mis_submit_freed(void)
{
	/* a submit of a dead urb: the submit guard reads the identity field
	 * first, so the harness marks live storage with the same poison identity
	 * (see the file header) and asserts the named refusal */
	struct fuzz_row *r = row_pick((1u << FRS_LIVE) | (1u << FRS_SUBMITTED));
	int rc;

	if (r == NULL)
		return;
	env_mark_dead_id(&r->s);
	rc = usb_submit_urb(r->u, GFP_KERNEL);
	env_op_bump();
	env_clear_dead_id(&r->s);
	g_n_submit_freed++;
	judge_expect(rc == -ENODEV, "a submit of a dead urb is refused with "
	    "-ENODEV", r->u, -ENODEV, rc);
	ring_put(FZ_MIS_SUBMIT_FREED, (unsigned int)(r - g_fr), rc);
}

static void
op_mis_double_kill(void)
{
	struct fuzz_row *r = row_pick((1u << FRS_SUBMITTED) | (1u << FRS_LIVE));
	int cb_before;

	if (r == NULL)
		return;
	cb_before = r->s.cb_count;
	usb_kill_urb(r->u);
	env_op_bump();
	usb_kill_urb(r->u);		/* the idempotent second kill */
	env_op_bump();
	g_n_double_kill++;
	judge_expect(r->s.cb_count <= cb_before + 1 &&
	    r->s.cb_count == r->s.submits,
	    "the double kill is idempotent (no second completion)",
	    r->u, (long)r->s.submits, (long)r->s.cb_count);
	if (r->state == FRS_SUBMITTED)
		r->state = FRS_LIVE;
	ring_put(FZ_MIS_DOUBLE_KILL, (unsigned int)(r - g_fr), 0);
}

/* the misuse, made deterministic: submit a fresh transfer and issue the second
 * submit immediately - the harness has not yielded since the first, so the
 * engine still owns the urb (QUEUED or INFLIGHT) and the guard must answer
 * -EBUSY */
static void
op_mis_submit_live(void)
{
	struct fuzz_row *r = row_pick(1u << FRS_LIVE);
	int rc1, rc2;

	if (r == NULL)
		return;
	rc1 = env_submit(&r->s);
	if (rc1 == 0)
		r->state = FRS_SUBMITTED;
	rc2 = env_submit(&r->s);
	g_n_submit_live++;
	judge_expect(rc1 == 0 && rc2 == -EBUSY, "a second submit of a live urb "
	    "is refused with -EBUSY", r->u, -EBUSY, rc2);
	ring_put(FZ_MIS_SUBMIT_LIVE, (unsigned int)(r - g_fr), rc2);
}

/* The misuse target: a row released *just now*, allocated and released in place
 * so the misuse operators always have a deterministic target.
 *
 * Manufacturing the release at the point of use is what keeps the digest a
 * function of (seed, iters) alone. Every other way of arming a misuse reads
 * allocator behaviour: a row released earlier is only a lawful target while its
 * address has not been handed out again (the engine clears that address's ring
 * entry when it is, usb_alloc_urb), and *whether* the address was recycled is a
 * property of the heap's free-list state - which differs between a process that
 * ran the matrix first and a process that replays the stream alone (measured
 * after the service bodies moved onto the kernel heap: the replay path's digest
 * drifted from the stream's for exactly this reason). With the release made
 * here, no allocation can intervene between the release and the block above it,
 * so the guard's released-pointer ring always holds the target and the outcome
 * no longer depends on the allocator's history. */
static struct fuzz_row *
released_pick(void)
{
	struct fuzz_row *r = row_pick(1u << FRS_FREE);

	if (r == NULL)
		return NULL;
	row_reset(r);
	r->u = env_alloc(&r->s, 0, LAWFUL_ANY);
	if (r->u == NULL)
		return NULL;
	r->state = FRS_LIVE;
	op_free(r);
	return r;
}

/* one operation from the stream.  The misuse share is a declared probability:
 * one in eight operations draws from the misuse set (1/8 overall); the normal
 * share is the other 7/8, weighted by the ring of operator kinds. */
static void
fuzz_op(void)
{
	uint64_t r = rng_next();
	unsigned int kind;
	int misuse = (r % 8u) == 0u;

	g_op_index++;
	if (misuse)
		kind = FZ_MIS_FREE_FREED +
		    (unsigned int)((r >> 32) %
		    (FZ_OP_N - FZ_MIS_FREE_FREED));
	else
		kind = (unsigned int)((r >> 32) % FZ_FREE);

	switch (kind) {
	case FZ_ALLOC:
		op_alloc();
		break;
	case FZ_ALLOC_COHERENT:
		op_alloc_coherent();
		break;
	case FZ_FILL_BULK:
	case FZ_FILL_CTRL:
		/* only a urb with no submission in flight may be refilled: mutating
		 * a queued urb (its pipe/endpoint identity) is outside the declared
		 * operator vocabulary and outside what a Linux driver may do */
		op_fill(row_pick(1u << FRS_LIVE), kind == FZ_FILL_CTRL);
		break;
	case FZ_SUBMIT:
		op_submit(row_pick((1u << FRS_LIVE) | (1u << FRS_SUBMITTED)));
		break;
	case FZ_KILL:
		op_cancel(row_pick((1u << FRS_LIVE) | (1u << FRS_SUBMITTED)),
		    0);
		break;
	case FZ_UNLINK:
		op_cancel(row_pick((1u << FRS_LIVE) | (1u << FRS_SUBMITTED)),
		    1);
		break;
	case FZ_ANCHOR:
		op_anchor(row_pick((1u << FRS_LIVE) | (1u << FRS_SUBMITTED)));
		break;
	case FZ_UNANCHOR:
		op_unanchor(row_pick((1u << FRS_LIVE) | (1u << FRS_SUBMITTED) |
		    (1u << FRS_RELEASED)));
		break;
	case FZ_KILL_ANCHORED:
		op_kill_anchored();
		break;
	case FZ_CONTROL_MSG:
		op_control_msg();
		break;
	case FZ_FREE:
		op_free(row_pick((1u << FRS_LIVE) | (1u << FRS_SUBMITTED)));
		break;
	case FZ_MIS_FREE_FREED:
		op_mis_free_freed();
		break;
	case FZ_MIS_SUBMIT_FREED:
		op_mis_submit_freed();
		break;
	case FZ_MIS_KILL_FREED:
		op_mis_kill_freed();
		break;
	case FZ_MIS_DOUBLE_KILL:
		op_mis_double_kill();
		break;
	case FZ_MIS_SUBMIT_LIVE:
		op_mis_submit_live();
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* the block boundary                                                  */
/* ------------------------------------------------------------------ */

static int g_anchor_ready;

static void
block_begin(unsigned int seed)
{
	char seed_txt[32];

	/* the anchor's wait-queue primitives are kernel objects: initialize them
	 * once per process, never per block (a per-block init would leak two
	 * objects into the heap the runtime lives on) */
	if (!g_anchor_ready) {
		init_usb_anchor(&g_anchor);
		g_anchor_ready = 1;
	}
	memset(g_fr, 0, sizeof(g_fr));
	memset(g_ring, 0, sizeof(g_ring));
	g_ring_at = 0;
	snprintf(seed_txt, sizeof(seed_txt), "%u", seed);
	snprintf(g_cell_id, sizeof(g_cell_id), "fuzz-%s", seed_txt);
	judge_begin(g_cell_id);
}

static int
block_check(void)
{
	struct lkpi_usb_mem_stats st;
	unsigned int i, steps;

	/* quiesce: kill what still owes a completion, settle, wait for the
	 * callbacks (this is the block's no-deadlock fence) */
	steps = env_drain_all();
	judge_rule_no_deadlock(steps, ENV_STEP_BUDGET);
	(void)env_settle();
	if (env_wait_cb(env_seam_total()) != 0)
		judge_rule_no_deadlock(ENV_STEP_BUDGET, ENV_STEP_BUDGET);

	/* the exactly-once rule over every live row: every accepted submission
	 * ended in exactly one completion, and the seam never delivered more
	 * than was submitted */
	for (i = 0; i < FUZZ_ROWS; i++) {
		struct fuzz_row *r = &g_fr[i];

		if (r->state == FRS_FREE || r->u == NULL)
			continue;
		judge_rule_exactly_once(r->u, r->s.submits, r->s.cb_count,
		    r->s.submits, r->s.seam_count);
	}
	/* release everything the block still holds */
	for (i = 0; i < FUZZ_ROWS; i++) {
		struct fuzz_row *r = &g_fr[i];

		if (r->state == FRS_FREE || r->state == FRS_RELEASED)
			continue;
		/* the API contract before a release: no anchor list may keep
		 * pointing at the block (the anchor's own reference would also
		 * survive the free, so the release would never happen) */
		if (r->u != NULL && r->u->anchor != NULL)
			usb_unanchor_urb(r->u);
		r->in_anchor = 0;
		env_free_urb(&r->s);
		env_note_released(&r->s);
		r->state = FRS_RELEASED;
	}
	(void)env_settle();
	(void)env_wait_cb(env_seam_total());

	/* the misuse operators were exercised: at least one of each group must
	 * have happened across a block (the declared probability guarantee) */
	env_stats(&st);
	judge_rule_balance(&st);
	judge_note("ops=%u released=%u mis free-freed=%u submit-freed=%u "
	    "kill-freed=%u double-kill=%u submit-live=%u | alloc=%u free=%u "
	    "df=%u sf=%u uaf=%u af=%u", g_op_index, g_ring_at, g_n_free_freed,
	    g_n_submit_freed, g_n_kill_freed, g_n_double_kill, g_n_submit_live,
	    st.n_urb_alloc, st.n_urb_free, st.double_free, st.stray_free,
	    st.uaf_write, st.alloc_fail);
	fflush(stdout);
	return 1;
}

/* ------------------------------------------------------------------ */
/* the digest and the run                                              */
/* ------------------------------------------------------------------ */

/* the digest is a function of (seed, iters) alone: the seam counters are mixed
 * as deltas against the state this seed's run started from, so a preceding
 * matrix run or an earlier seed in the same process cannot change it */
static struct lkpi_usb_mem_stats g_digest_base;

static uint64_t
digest_state(void)
{
	struct lkpi_usb_mem_stats st;
	uint64_t h = 1469598103934665603ULL;	/* FNV-1a offset basis */
	unsigned int i;

	env_stats(&st);
#define MIX(v) do { h ^= (uint64_t)(v); h *= 1099511628211ULL; } while (0)
	MIX(g_rng);
	MIX(g_op_index);
	MIX(st.n_urb_alloc - g_digest_base.n_urb_alloc);
	MIX(st.n_urb_free - g_digest_base.n_urb_free);
	MIX(st.n_coherent_alloc - g_digest_base.n_coherent_alloc);
	MIX(st.n_coherent_free - g_digest_base.n_coherent_free);
	MIX(st.alloc_fail - g_digest_base.alloc_fail);
	MIX(st.double_free - g_digest_base.double_free);
	MIX(st.stray_free - g_digest_base.stray_free);
	MIX(st.uaf_write - g_digest_base.uaf_write);
	for (i = 0; i < FUZZ_RING; i++) {
		MIX(g_ring[i].op);
		MIX(g_ring[i].kind);
		MIX(g_ring[i].row);
		MIX((uint64_t)g_ring[i].rc);
	}
#undef MIX
	return h;
}

int
fuzz_run(unsigned int seed, unsigned int iters)
{
	unsigned int blocks = 0, ops = 0;
	uint64_t digest;

	if (g_replay_mode)
		printf("URBSTRESS: replay seed=%u n=%u start\n", seed, iters);

	g_rng = 0x9e3779b97f4a7c15ULL ^
	    ((uint64_t)seed * 0xbf58476d1ce4e5b9ULL);
	if (g_rng == 0)
		g_rng = 0x2545f4914f6cdd1dULL;
	g_op_index = 0;
	/*
	 * The controller knobs are sticky by design (a cell sets one and the next
	 * submit obeys); the stream, whose digest claims to be a function of
	 * (seed, iters) alone, starts from their declared defaults so a preceding
	 * matrix run cannot change its outcomes. This is also what makes --replay
	 * reproduce a run taken inside a --fuzz process.
	 */
	env_reset_knobs();
	env_stats(&g_digest_base);

	while (ops < iters) {
		unsigned int n = iters - ops;

		if (n > FUZZ_RING)
			n = FUZZ_RING;
		block_begin(seed);
		while (n-- > 0) {
			fuzz_op();
			ops++;
		}
		(void)block_check();
		blocks++;
	}
	digest = digest_state();
	printf("URBSTRESS: fuzz seed=%u iters=%u ops=%u digest=%016llx ok=1 "
	    "blocks=%u\n", seed, iters, g_op_index,
	    (unsigned long long)digest, blocks);
	if (g_replay_mode)
		printf("URBSTRESS: replay seed=%u ok=1 first-witness=none "
		    "digest=%016llx\n", seed, (unsigned long long)digest);
	fflush(stdout);
	return 0;
}
