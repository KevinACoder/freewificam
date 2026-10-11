/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * fuzz.c - the seeded random layer of the skbuff contract harness.
 *
 * One run is a pure function of (profile, seed, iteration count): the PRNG is
 * xorshift64*, every choice is drawn from it, the pool is reset first and
 * nothing in the digest depends on host addresses.  The stream mixes the
 * legal operations (alloc/free/put/push/pull/trim/reserve/enqueue/dequeue)
 * with the named misuses (double free, stray free, use-after-free write,
 * oversize allocation, a deliberately corrupted released slot plus its fence
 * scan).
 *
 * The layer carries two oracles, both written from the production headers'
 * text rather than from the double:
 *
 *   - a shadow model of every live skb's geometry (cap/dataoff/tail/len) and
 *     of the pool counters.  It is updated with the Linux rules the headers
 *     document (put appends, push prepends, pull consumes from the head, trim
 *     shortens the tail, reserve moves data forward) and it predicts, before
 *     each operation, whether that operation may succeed and what it must do
 *     to the state.  The double's observable state is then compared with the
 *     prediction, so a divergence shows up as a model witness rather than as
 *     a silent pass.
 *   - the named-counter ledger: the stream knows which operations are the
 *     named misuses, so judge_post() can require each of
 *     double_free/stray_free/uaf_write/fence_bad to move by exactly that
 *     amount and by nothing else.
 *
 * The stream also keeps the ordered 4096-entry operation ring the witness
 * report prints, and skb_fuzz_run() doubles as the replay driver: replaying
 * the first N operations of a seed executes exactly the same prefix (the
 * loop state is rebuilt from zero each run).
 */

#include <stdarg.h>

#include "judge.h"

enum skb_op_kind {
	SKOP_ALLOC = 0,
	SKOP_DEV_ALLOC,
	SKOP_ALLOC_OVERSIZE,
	SKOP_FREE,
	SKOP_PUT,
	SKOP_PUSH,
	SKOP_PULL,
	SKOP_TRIM,
	SKOP_RESERVE,
	SKOP_ENQUEUE,
	SKOP_DEQUEUE,
	SKOP_DOUBLE_FREE,
	SKOP_STRAY_FREE,
	SKOP_UAF,
	SKOP_CORRUPT_SCAN,
	SKOP_N
};

static const char *const op_names[SKOP_N] = {
	"alloc", "dev_alloc", "alloc_oversize", "free", "put", "push", "pull",
	"trim", "reserve", "enqueue", "dequeue", "double_free", "stray_free",
	"uaf", "corrupt_scan",
};

#define FX_MAX_LIVE	128
#define FX_FREE_RING	32
#define FX_OP_RING	4096

struct fx_rec {
	struct sk_buff	*p;
	unsigned int	id;
	unsigned int	cap;		/* end, from the allocation request */
	unsigned int	dataoff;	/* data - head */
	unsigned int	tail, len;
	unsigned char	hdr_pool;	/* came from a static header slot */
	unsigned char	data_pool;	/* holds a fixed data slot */
	unsigned char	queued;
};

struct fx_freed {
	struct sk_buff	*p;
	unsigned char	hdr_pool;	/* came from a static header slot */
	unsigned char	data_pool;	/* held a fixed data slot */
};

struct fx_op_rec {
	uint32_t	op, a1, a2, id;
};

/* the self-proof knob: judge the named double free as if it must not count */
static int must_red;
static int suppress_recent;

static struct fx {
	struct fx_rec	rec[FX_MAX_LIVE];
	unsigned int	n;
	unsigned int	next_id;
	struct fx_freed	freed[FX_FREE_RING];
	unsigned int	freed_head;
	unsigned int	freed_n;
	struct sk_buff	*fifo[FX_MAX_LIVE];
	unsigned int	fifo_head;
	unsigned int	fifo_n;
	unsigned long	enq, deq, unl;
	struct sk_buff_head	q;
	struct lkpi_skb_pool_stats est;	/* the stream's expected counters */
	unsigned int	est_geo_bad;
	uint64_t	rng;
	struct fx_op_rec ring[FX_OP_RING];
	unsigned int	ring_head;
	unsigned int	ring_n;
	unsigned long	op_index;
	int		failed;
	int		exp_df, exp_sf, exp_uaf, exp_fb;
	unsigned char	scratch[SKB_DOUBLE_MAX_DATA_SZ];
	unsigned char	foreign_static[64];
	unsigned char	*foreign_heap;
	struct sk_buff	*foreign_interior;
} fx;

/* ------------------------------------------------------------------ */
/* prng and the operation ring                                         */
/* ------------------------------------------------------------------ */

static uint64_t
rng_next(struct fx *f)
{
	uint64_t x = f->rng;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	f->rng = x;
	return x * 0x2545f4914f6cdd1dULL;
}

static void
fx_ring_push(struct fx *f, unsigned int op, unsigned int a1, unsigned int a2,
    unsigned int id)
{

	f->ring[f->ring_head].op = op;
	f->ring[f->ring_head].a1 = a1;
	f->ring[f->ring_head].a2 = a2;
	f->ring[f->ring_head].id = id;
	f->ring_head = (f->ring_head + 1u) % FX_OP_RING;
	if (f->ring_n < FX_OP_RING)
		f->ring_n++;
}

static void
fx_dump_recent(struct fx *f, unsigned long op_index)
{
	unsigned int n = f->ring_n < 8 ? f->ring_n : 8;
	unsigned int k;

	for (k = n; k > 0; k--) {
		const struct fx_op_rec *r = &f->ring[(f->ring_head +
		    FX_OP_RING - k) % FX_OP_RING];
		unsigned long idx = op_index + 1ul - k;

		printf("SKBST: recent op=%lu %s arg1=%u arg2=%u id=%u\n", idx,
		    op_names[r->op], r->a1, r->a2, r->id);
	}
	fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* witnesses                                                           */
/* ------------------------------------------------------------------ */

static int
fx_fail(struct fx *f, const char *kind, const char *fmt, ...)
{
	char detail[240];
	va_list ap;

	if (!f->failed) {
		va_start(ap, fmt);
		vsnprintf(detail, sizeof(detail), fmt, ap);
		va_end(ap);
		judge_witness(f->op_index, kind, "%s", detail);
		f->failed = 1;
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* model bookkeeping                                                   */
/* ------------------------------------------------------------------ */

static int
fx_find(struct fx *f, const struct sk_buff *p)
{
	unsigned int i;

	for (i = 0; i < f->n; i++)
		if (f->rec[i].p == p)
			return (int)i;
	return -1;
}

static void
fx_del(struct fx *f, unsigned int idx)
{

	f->rec[idx] = f->rec[f->n - 1];
	f->n--;
}

static void
fx_freed_push(struct fx *f, struct sk_buff *p, unsigned char hdr_pool,
    unsigned char data_pool)
{

	f->freed[f->freed_head].p = p;
	f->freed[f->freed_head].hdr_pool = hdr_pool;
	f->freed[f->freed_head].data_pool = data_pool;
	f->freed_head = (f->freed_head + 1u) % FX_FREE_RING;
	if (f->freed_n < FX_FREE_RING)
		f->freed_n++;
}

/* i == 0 is the most recently freed entry */
static const struct fx_freed *
fx_freed_at(struct fx *f, unsigned int i)
{
	unsigned int idx = (f->freed_head + FX_FREE_RING - 1u - i) % FX_FREE_RING;

	return &f->freed[idx];
}

static int
fx_fifo_push(struct fx *f, struct sk_buff *p)
{
	unsigned int idx;

	if (f->fifo_n >= FX_MAX_LIVE)
		return 0;
	idx = (f->fifo_head + f->fifo_n) % FX_MAX_LIVE;
	f->fifo[idx] = p;
	f->fifo_n++;
	return 1;
}

static struct sk_buff *
fx_fifo_pop(struct fx *f)
{

	while (f->fifo_n > 0) {
		struct sk_buff *p = f->fifo[f->fifo_head];

		f->fifo_head = (f->fifo_head + 1u) % FX_MAX_LIVE;
		f->fifo_n--;
		if (p != NULL)
			return p;
	}
	return NULL;
}

/* an skb freed while still queued leaves a hole in the expected order */
static int
fx_fifo_punch(struct fx *f, struct sk_buff *p)
{
	unsigned int i;

	for (i = 0; i < f->fifo_n; i++) {
		unsigned int idx = (f->fifo_head + i) % FX_MAX_LIVE;

		if (f->fifo[idx] == p) {
			f->fifo[idx] = NULL;
			return 1;
		}
	}
	return 0;
}

static struct fx_rec *
fx_rec_of(struct fx *f, struct sk_buff *p)
{
	int idx = fx_find(f, p);

	return idx >= 0 ? &f->rec[idx] : NULL;
}

static void
fx_paint(const struct fx_rec *r, unsigned char *p, unsigned int len)
{
	unsigned int i;
	unsigned int base = (unsigned int)((uintptr_t)p - (uintptr_t)r->p->head);

	for (i = 0; i < len; i++)
		p[i] = (unsigned char)(0x5au ^
		    (unsigned char)(r->id * 31u + base + i));
}

/* the double's observable geometry against the model's prediction */
static int
fx_check_geom(struct fx *f, unsigned int idx, const char *what)
{
	const struct fx_rec *r = &f->rec[idx];
	unsigned int off = (unsigned int)((uintptr_t)r->p->data -
	    (uintptr_t)r->p->head);

	if (r->p->_gen == 0 || r->p->_gen == LKPI_SKB_FREED_MAGIC)
		return fx_fail(f, "model-generation",
		    "%s skb#%u carries no owner generation", what, r->id);
	if (off != r->dataoff || r->p->tail != r->tail ||
	    r->p->len != r->len || r->p->end != r->cap)
		return fx_fail(f, "model-geometry",
		    "%s skb#%u: dataoff=%u tail=%u len=%u end=%u, model says dataoff=%u tail=%u len=%u cap=%u",
		    what, r->id, off, r->p->tail, r->p->len, r->p->end,
		    r->dataoff, r->tail, r->len, r->cap);
	return 0;
}

static void
fx_count_static(struct fx *f, unsigned int *static_live, unsigned int *pool_live)
{
	unsigned int i;

	*static_live = 0;
	*pool_live = 0;
	for (i = 0; i < f->n; i++) {
		if (f->rec[i].hdr_pool)
			(*static_live)++;
		if (f->rec[i].data_pool)
			(*pool_live)++;
	}
}

/* the first operation this stream may legally use as a named misuse: the
 * newest released entry that the pool still reports as released */
/*
 * The newest released entry usable for a named misuse.  want & 1 requires a
 * static header slot (so the double's generation check is the thing under
 * test), want & 2 requires its linear buffer to be a pool slot (so the fence
 * and the payload-write check have a buffer to look at).
 */
static const struct fx_freed *
fx_pick_released(struct fx *f, unsigned int want)
{
	unsigned int i;

	for (i = 0; i < f->freed_n; i++) {
		const struct fx_freed *e = fx_freed_at(f, i);

		if ((want & 1u) != 0 && !e->hdr_pool)
			continue;
		if ((want & 2u) != 0 && !e->data_pool)
			continue;
		if (skb_double_is_released(e->p))
			return e;
	}
	return NULL;
}

/* the request-size domain: the exact pool boundary, both sides of it and a
 * few ordinary sizes, drawn from the stream's PRNG */
static unsigned int
fx_pick_size(struct fx *f, unsigned int oversize)
{
	const struct skb_pool_cfg *cfg = skb_double_cfg();
	unsigned int ds = cfg->data_sz;
	uint64_t r = rng_next(f);

	if (oversize)
		return ds + 1u + (unsigned int)(r % 256u);
	switch (r % 8u) {
	case 0:
		return 0;
	case 1:
		return 1;
	case 2:
		return 64;
	case 3:
		return ds;
	case 4:
		return ds + 1u;
	case 5:
		return 2u + (unsigned int)((r >> 8) % 128u);
	case 6:
		return ds / 2u;
	default:
		return (unsigned int)((r >> 16) % (uint64_t)(ds + 2u));
	}
}

/* the length domain: 0, 1, exactly the room available, one past it, and a
 * couple of plain values */
static unsigned int
fx_pick_len(uint64_t r, unsigned int cur)
{

	switch (r % 6u) {
	case 0:
		return 0;
	case 1:
		return 1;
	case 2:
		return cur;
	case 3:
		return cur + 1u;
	case 4:
		return (unsigned int)((r >> 8) % 96u);
	default:
		return (unsigned int)((r >> 16) % 300u);
	}
}

/* ------------------------------------------------------------------ */
/* the operations                                                      */
/* ------------------------------------------------------------------ */

static void
fx_do_alloc(struct fx *f, int dev, unsigned int size)
{
	const struct skb_pool_cfg *cfg = skb_double_cfg();
	unsigned int need = size + (dev ? (unsigned int)NET_SKB_PAD : 0u);
	unsigned int static_live, pool_live;
	int hdr_ok, dat_ok, ok;
	struct sk_buff *p;
	struct fx_rec *r;

	fx_count_static(f, &static_live, &pool_live);
	hdr_ok = static_live < cfg->n_header;
	dat_ok = need <= cfg->data_sz && pool_live < cfg->n_data;
	ok = (hdr_ok || cfg->heap_fallback) && (dat_ok || cfg->heap_fallback);

	fx_ring_push(f, dev ? SKOP_DEV_ALLOC : SKOP_ALLOC, size, need,
	    f->next_id);
	p = dev ? lkpi_dev_alloc_skb(size, GFP_KERNEL)
		: lkpi_skb_alloc(size, GFP_KERNEL);

	/* dyalloc_oversize is a request counter (skbuff_pool.h line 89) and is
	 * taken whether or not the request is then served */
	if (need > cfg->data_sz)
		f->est.dyalloc_oversize++;

	if (!ok) {
		f->est.alloc_fail++;
		if (p != NULL)
			fx_fail(f, "model-alloc",
			    "%s(%u) had to fail (fixed geometry exhausted, no heap fallback) but returned an skb",
			    dev ? "dev_alloc_skb" : "alloc_skb", size);
		return;
	}
	if (p == NULL) {
		fx_fail(f, "model-alloc", "%s(%u) had to succeed but returned NULL",
		    dev ? "dev_alloc_skb" : "alloc_skb", size);
		return;
	}
	if (f->n >= FX_MAX_LIVE) {
		fx_fail(f, "model-live", "live table overflow at %u records",
		    f->n);
		return;
	}

	f->est.n_alloc++;
	if (hdr_ok) {
		f->est.skb_used++;
		if (f->est.skb_used > f->est.skb_high)
			f->est.skb_high = f->est.skb_used;
	} else {
		f->est.slot_overflow++;
	}
	if (dat_ok) {
		f->est.data_used++;
		if (f->est.data_used > f->est.data_high)
			f->est.data_high = f->est.data_used;
	} else {
		if (need <= cfg->data_sz)
			f->est.data_overflow++;
		f->est.dyalloc_live++;
	}

	r = &f->rec[f->n++];
	r->p = p;
	r->id = f->next_id++;
	r->cap = need;
	r->dataoff = dev ? (unsigned int)NET_SKB_PAD : 0u;
	r->tail = r->dataoff;
	r->len = 0;
	r->hdr_pool = (unsigned char)hdr_ok;
	r->data_pool = (unsigned char)dat_ok;
	r->queued = 0;
	(void)fx_check_geom(f, f->n - 1u, "fresh allocation");
}

static void
fx_do_free(struct fx *f)
{
	unsigned int idx;
	struct fx_rec *r;

	if (f->n == 0) {
		fx_ring_push(f, SKOP_FREE, 0, 0, 0);
		return;
	}
	idx = (unsigned int)(rng_next(f) % f->n);
	r = &f->rec[idx];
	fx_ring_push(f, SKOP_FREE, r->queued, 0, r->id);
	if (r->queued) {
		/* Linux callers unlink before the last reference goes away */
		lkpi_skb_do_unlink(r->p, &f->q);
		if (!fx_fifo_punch(f, r->p)) {
			fx_fail(f, "model-queue",
			    "skb#%u was queued but not in the expected order", r->id);
			return;
		}
		r->queued = 0;
		f->unl++;
	}
	kfree_skb(r->p);
	f->est.n_free++;
	if (r->hdr_pool)
		f->est.skb_used--;
	if (r->data_pool)
		f->est.data_used--;
	else
		f->est.dyalloc_live--;
	fx_freed_push(f, r->p, r->hdr_pool, r->data_pool);
	fx_del(f, idx);
}

/* put/push/pull/trim/reserve against one live skb */
static void
fx_do_geom(struct fx *f, unsigned int kind)
{
	struct fx_rec *r;
	unsigned int idx, arg, new_off, new_tail, new_len, exp_off = 0;
	unsigned int counts_geo;
	int ok;
	uint64_t rr;

	if (f->n == 0) {
		fx_ring_push(f, kind, 0, 0, 0);
		return;
	}
	idx = (unsigned int)(rng_next(f) % f->n);
	r = &f->rec[idx];
	rr = rng_next(f);

	switch (kind) {
	case SKOP_PUSH:
		arg = fx_pick_len(rr, r->dataoff);
		break;
	case SKOP_PULL:
	case SKOP_TRIM:
		arg = fx_pick_len(rr, r->len);
		break;
	default:			/* put and reserve take tailroom */
		arg = fx_pick_len(rr, r->cap - r->tail);
		break;
	}
	fx_ring_push(f, kind, arg, 0, r->id);

	/* the Linux rules, applied to the model first */
	switch (kind) {
	case SKOP_PUT:
		ok = arg <= r->cap - r->tail;
		exp_off = r->tail;
		new_off = r->dataoff;
		new_tail = ok ? r->tail + arg : r->tail;
		new_len = ok ? r->len + arg : r->len;
		break;
	case SKOP_PUSH:
		ok = arg <= r->dataoff;
		exp_off = r->dataoff - arg;
		new_off = ok ? r->dataoff - arg : r->dataoff;
		new_tail = r->tail;
		new_len = ok ? r->len + arg : r->len;
		break;
	case SKOP_PULL:
		ok = arg <= r->len;
		exp_off = r->dataoff + arg;
		new_off = ok ? r->dataoff + arg : r->dataoff;
		new_tail = r->tail;
		new_len = ok ? r->len - arg : r->len;
		break;
	case SKOP_TRIM:
		ok = 1;
		new_off = r->dataoff;
		new_len = arg < r->len ? arg : r->len;
		new_tail = arg < r->len ? r->dataoff + arg : r->tail;
		break;
	default:			/* reserve */
		ok = arg <= r->cap - r->tail;
		new_off = ok ? r->dataoff + arg : r->dataoff;
		new_tail = ok ? r->tail + arg : r->tail;
		new_len = r->len;
		break;
	}
	counts_geo = kind != SKOP_PULL && kind != SKOP_TRIM;

	/* run it */
	switch (kind) {
	case SKOP_PUT: {
		unsigned char *p = skb_put(r->p, arg);

		if ((p != NULL) != ok) {
			fx_fail(f, "model-put", "skb#%u put(%u) returned %s, the model says %s",
			    r->id, arg, p != NULL ? "a pointer" : "NULL",
			    ok ? "success" : "refusal");
			return;
		}
		if (p != NULL) {
			if ((unsigned int)(p - r->p->head) != exp_off) {
				fx_fail(f, "model-put", "skb#%u put(%u) returned offset %u, the old tail was %u",
				    r->id, arg,
				    (unsigned int)(p - r->p->head), exp_off);
				return;
			}
			fx_paint(r, p, arg);
		}
		break;
	}
	case SKOP_PUSH: {
		void *p = skb_push(r->p, arg);

		if ((p != NULL) != ok) {
			fx_fail(f, "model-push", "skb#%u push(%u) returned %s, the model says %s",
			    r->id, arg, p != NULL ? "a pointer" : "NULL",
			    ok ? "success" : "refusal");
			return;
		}
		if (p != NULL) {
			if ((unsigned int)((unsigned char *)p - r->p->head) != exp_off) {
				fx_fail(f, "model-push", "skb#%u push(%u) returned offset %u, the model says %u",
				    r->id, arg,
				    (unsigned int)((unsigned char *)p - r->p->head),
				    exp_off);
				return;
			}
			fx_paint(r, (unsigned char *)p, arg);
		}
		break;
	}
	case SKOP_PULL: {
		unsigned char *p = skb_pull(r->p, arg);

		if ((p != NULL) != ok) {
			fx_fail(f, "model-pull", "skb#%u pull(%u) returned %s, the model says %s",
			    r->id, arg, p != NULL ? "a pointer" : "NULL",
			    ok ? "success" : "refusal");
			return;
		}
		if (p != NULL && (unsigned int)(p - r->p->head) != exp_off) {
			fx_fail(f, "model-pull", "skb#%u pull(%u) returned offset %u, the model says %u",
			    r->id, arg, (unsigned int)(p - r->p->head), exp_off);
			return;
		}
		break;
	}
	case SKOP_TRIM:
		skb_trim(r->p, arg);
		break;
	default:
		lkpi_skb_reserve(r->p, arg);
		break;
	}

	r->dataoff = new_off;
	r->tail = new_tail;
	r->len = new_len;
	if (!ok && counts_geo)
		f->est_geo_bad++;
	(void)fx_check_geom(f, idx, op_names[kind]);
}

static void
fx_do_enqueue(struct fx *f)
{
	unsigned int i, start = 0;
	int found = 0;
	uint64_t r = rng_next(f);

	for (i = 0; i < f->n; i++) {
		unsigned int idx = (i + (unsigned int)(r % (uint64_t)f->n)) %
		    f->n;

		if (!f->rec[idx].queued) {
			start = idx;
			found = 1;
			break;
		}
	}
	if (!found) {
		fx_ring_push(f, SKOP_ENQUEUE, 0, 0, 0);
		return;
	}
	fx_ring_push(f, SKOP_ENQUEUE, 0, 0, f->rec[start].id);
	__skb_queue_tail(&f->q, f->rec[start].p);
	if (f->rec[start].p->list != &f->q) {
		fx_fail(f, "model-queue", "enqueued skb#%u does not name its queue",
		    f->rec[start].id);
		return;
	}
	if (!fx_fifo_push(f, f->rec[start].p)) {
		fx_fail(f, "model-queue", "expected-order table overflow");
		return;
	}
	f->rec[start].queued = 1;
	f->enq++;
}

static void
fx_do_dequeue(struct fx *f)
{
	struct sk_buff *want = fx_fifo_pop(f);
	struct sk_buff *got;
	struct fx_rec *r;
	unsigned int id = 0;

	if (want != NULL) {
		r = fx_rec_of(f, want);
		if (r != NULL)
			id = r->id;
	}
	fx_ring_push(f, SKOP_DEQUEUE, 0, 0, id);
	got = __skb_dequeue(&f->q);
	if (got != want) {
		fx_fail(f, "model-queue",
		    "dequeue returned %s but the enqueue order says %s (id %u)",
		    got != NULL ? "an skb" : "NULL",
		    want != NULL ? "an skb" : "NULL", id);
		return;
	}
	if (got != NULL) {
		r = fx_rec_of(f, got);
		if (r == NULL) {
			fx_fail(f, "model-queue", "dequeued pointer names no live skb");
			return;
		}
		r->queued = 0;
		f->deq++;
	}
}

static void
fx_do_double_free(struct fx *f)
{
	const struct fx_freed *e = fx_pick_released(f, 1);

	if (e == NULL) {
		fx_ring_push(f, SKOP_DOUBLE_FREE, 0, 0, 0);
		return;
	}
	fx_ring_push(f, SKOP_DOUBLE_FREE, 0, 0, 0);
	kfree_skb(e->p);
	/* the self-proof stream asks the judge for an expectation that the
	 * operation cannot meet, so the counter witness is reported */
	f->exp_df = must_red ? 0 : 1;
	f->est.double_free++;
}

static void
fx_do_stray_free(struct fx *f)
{
	struct sk_buff *p;
	uint64_t r = rng_next(f);
	unsigned int which = (unsigned int)(r % 3u);

	if (which == 1 && f->foreign_heap == NULL)
		which = 0;
	switch (which) {
	case 0:
		p = (struct sk_buff *)(void *)f->foreign_static;
		break;
	case 1:
		p = (struct sk_buff *)(void *)f->foreign_heap;
		break;
	default:
		p = f->foreign_interior;
		break;
	}
	fx_ring_push(f, SKOP_STRAY_FREE, which, 0, 0);
	kfree_skb(p);
	f->est.stray_free++;
}

static void
fx_do_uaf(struct fx *f)
{
	const struct fx_freed *e = fx_pick_released(f, 3);
	const struct skb_pool_cfg *cfg = skb_double_cfg();
	struct sk_buff before;
	unsigned char *buf;
	unsigned int sub, len;

	if (e == NULL) {
		fx_ring_push(f, SKOP_UAF, 0, 0, 0);
		return;
	}
	sub = (unsigned int)(rng_next(f) % 3u);
	len = 1u + (unsigned int)(rng_next(f) % 8u);

	/*
	 * The guard only fails a static slot: the picker already required one,
	 * and a static slot's declaration is not tied to the buffer's fate.
	 */
	fx_ring_push(f, SKOP_UAF, len, sub, 0);
	if (!skb_double_is_pool_slot(e->p)) {
		fx_fail(f, "model-uaf", "the picker handed out a non-pool slot");
		return;
	}
	buf = skb_double_released_buffer(e->p);
	if (buf == NULL) {
		/* the buffer was recycled: nothing to inspect on this one */
		return;
	}
	memcpy(f->scratch, buf, cfg->data_sz);
	before = *e->p;

	switch (sub) {
	case 0: {
		unsigned char *p = skb_put(e->p, len);

		if (p != NULL) {
			fx_fail(f, "model-uaf", "put on a released slot returned a pointer");
			return;
		}
		break;
	}
	case 1: {
		void *p = skb_push(e->p, len);

		if (p != NULL) {
			fx_fail(f, "model-uaf", "push on a released slot returned a pointer");
			return;
		}
		break;
	}
	default:
		lkpi_skb_reserve(e->p, len);
		break;
	}
	f->exp_uaf = 1;
	f->est.uaf_write++;
	if (memcmp(buf, f->scratch, cfg->data_sz) != 0)
		fx_fail(f, "model-uaf",
		    "a missed write on a released slot touched its payload");
	else if (memcmp(&before, e->p, sizeof(before)) != 0)
		fx_fail(f, "model-uaf",
		    "a missed write on a released slot touched its header");
}

static void
fx_do_corrupt_scan(struct fx *f)
{
	const struct fx_freed *e = fx_pick_released(f, 3);
	unsigned char *buf;

	if (e == NULL) {
		fx_ring_push(f, SKOP_CORRUPT_SCAN, 0, 0, 0);
		return;
	}
	buf = skb_double_released_buffer(e->p);
	if (buf == NULL) {
		fx_ring_push(f, SKOP_CORRUPT_SCAN, 0, 0, 0);
		return;
	}
	fx_ring_push(f, SKOP_CORRUPT_SCAN, 7, 0, 0);
	buf[7] = 0x00;		/* no fence pattern carries a zero byte */
	lkpi_skb_fence_scan();
	f->exp_fb = 1;
	f->est.fence_bad++;
}

static void
fx_step(struct fx *f, unsigned int kind)
{

	f->exp_df = f->exp_sf = f->exp_uaf = f->exp_fb = 0;
	switch (kind) {
	case SKOP_ALLOC:
		fx_do_alloc(f, 0, fx_pick_size(f, 0));
		break;
	case SKOP_DEV_ALLOC:
		fx_do_alloc(f, 1, fx_pick_size(f, 0));
		break;
	case SKOP_ALLOC_OVERSIZE:
		fx_do_alloc(f, 0, fx_pick_size(f, 1));
		break;
	case SKOP_FREE:
		fx_do_free(f);
		break;
	case SKOP_PUT:
	case SKOP_PUSH:
	case SKOP_PULL:
	case SKOP_TRIM:
	case SKOP_RESERVE:
		fx_do_geom(f, kind);
		break;
	case SKOP_ENQUEUE:
		fx_do_enqueue(f);
		break;
	case SKOP_DEQUEUE:
		fx_do_dequeue(f);
		break;
	case SKOP_DOUBLE_FREE:
		fx_do_double_free(f);
		break;
	case SKOP_STRAY_FREE:
		fx_do_stray_free(f);
		f->exp_sf = 1;
		break;
	case SKOP_UAF:
		fx_do_uaf(f);
		break;
	case SKOP_CORRUPT_SCAN:
		fx_do_corrupt_scan(f);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* selection and the counter ledger                                    */
/* ------------------------------------------------------------------ */

static unsigned int
fx_pick_kind(struct fx *f)
{
	static const struct {
		unsigned char	kind;
		unsigned char	w;
	} table[] = {
		{ SKOP_ALLOC, 34 },
		{ SKOP_DEV_ALLOC, 10 },
		{ SKOP_ALLOC_OVERSIZE, 6 },
		{ SKOP_FREE, 46 },
		{ SKOP_PUT, 30 },
		{ SKOP_PUSH, 20 },
		{ SKOP_PULL, 22 },
		{ SKOP_TRIM, 18 },
		{ SKOP_RESERVE, 12 },
		{ SKOP_ENQUEUE, 14 },
		{ SKOP_DEQUEUE, 16 },
		{ SKOP_DOUBLE_FREE, 4 },
		{ SKOP_STRAY_FREE, 3 },
		{ SKOP_UAF, 5 },
		{ SKOP_CORRUPT_SCAN, 4 },
	};
	const struct skb_pool_cfg *cfg = skb_double_cfg();
	unsigned int total = 0, pick, i;
	uint64_t r = rng_next(f);

	if (f->n >= (unsigned int)cfg->n_header + 2u)
		return SKOP_FREE;	/* steer back toward the pool's size */
	if (f->n == 0)
		return (r % 4u) == 0 ? SKOP_DEV_ALLOC : SKOP_ALLOC;
	for (i = 0; i < ARRAY_SIZE(table); i++)
		total += table[i].w;
	pick = (unsigned int)(r % total);
	for (i = 0; i < ARRAY_SIZE(table); i++) {
		if (pick < table[i].w)
			return table[i].kind;
		pick -= table[i].w;
	}
	return SKOP_ALLOC;
}

static int
fx_check_counters(struct fx *f)
{
	struct lkpi_skb_pool_stats now;
	unsigned int geo = lkpi_skb_geo_bad_count();
	int bad = 0;

	lkpi_skb_pool_stats(&now);
#define FX_CMP(field)							\
	do {								\
		if (now.field != f->est.field) {			\
			fx_fail(f, "counters",				\
			    "stats." #field " = %u but the stream expects %u", \
			    now.field, f->est.field);			\
			bad = 1;					\
		}							\
	} while (0)
	FX_CMP(skb_used);
	FX_CMP(skb_high);
	FX_CMP(data_used);
	FX_CMP(data_high);
	FX_CMP(dyalloc_live);
	FX_CMP(dyalloc_oversize);
	FX_CMP(slot_overflow);
	FX_CMP(data_overflow);
	FX_CMP(alloc_fail);
	FX_CMP(n_alloc);
	FX_CMP(n_free);
	FX_CMP(double_free);
	FX_CMP(stray_free);
	FX_CMP(uaf_write);
	FX_CMP(fence_bad);
#undef FX_CMP
	if (geo != f->est_geo_bad) {
		fx_fail(f, "counters", "geo_bad = %u but the stream expects %u",
		    geo, f->est_geo_bad);
		bad = 1;
	}
	if (skb_double_live_count() != f->n) {
		fx_fail(f, "counters",
		    "the pool holds %u live skbs but the stream holds %u",
		    skb_double_live_count(), f->n);
		bad = 1;
	}
	return bad ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static uint64_t
fx_mix64(uint64_t h, uint64_t v)
{
	unsigned int i;

	for (i = 0; i < 8; i++) {
		h ^= (v >> (8u * i)) & 0xffu;
		h *= 0x100000001b3ULL;
	}
	return h;
}

static uint64_t
fx_digest(struct fx *f)
{
	uint64_t h = skb_double_digest();
	unsigned int i;

	h = fx_mix64(h, f->next_id);
	h = fx_mix64(h, f->n);
	h = fx_mix64(h, f->enq);
	h = fx_mix64(h, f->deq);
	h = fx_mix64(h, f->unl);
	for (i = 0; i < f->n; i++) {
		h = fx_mix64(h, f->rec[i].id);
		h = fx_mix64(h, f->rec[i].cap);
		h = fx_mix64(h, f->rec[i].dataoff);
		h = fx_mix64(h, f->rec[i].tail);
		h = fx_mix64(h, f->rec[i].len);
		h = fx_mix64(h, f->rec[i].queued);
	}
	return h;
}

/* release everything the stream still holds, in model order */
static void
fx_drain(struct fx *f)
{
	unsigned int i;

	while (__skb_dequeue(&f->q) != NULL)
		f->deq++;
	for (i = 0; i < f->n; i++)
		f->rec[i].queued = 0;
	while (f->n > 0) {
		struct fx_rec *r = &f->rec[0];

		if (r->queued) {
			lkpi_skb_do_unlink(r->p, &f->q);
			(void)fx_fifo_punch(f, r->p);
			r->queued = 0;
			f->unl++;
		}
		kfree_skb(r->p);
		f->est.n_free++;
		if (r->hdr_pool)
			f->est.skb_used--;
		if (r->data_pool)
			f->est.data_used--;
		else
			f->est.dyalloc_live--;
		fx_del(f, 0);
	}
}

int
skb_fuzz_selftest_must_red(uint64_t seed, unsigned long budget,
    unsigned long *fail_op, int replay_phase)
{
	struct skb_fuzz_result res;
	int rc;

	must_red = 1;
	suppress_recent = 1;
	judge_set_quiet(replay_phase);
	rc = skb_fuzz_run(seed, budget, &res);
	judge_set_quiet(0);
	must_red = 0;
	suppress_recent = 0;
	if (rc == 0)
		return 0;	/* the wrong expectation was not exercised */
	*fail_op = res.ops - 1ul;
	return 1;
}

int
skb_fuzz_run(uint64_t seed, unsigned long iters, struct skb_fuzz_result *out)
{
	struct skb_bad bad;
	unsigned long i;

	memset(out, 0, sizeof(*out));
	memset(&fx, 0, sizeof(fx));
	fx.rng = seed * 0x9e3779b97f4a7c15ULL + 0x2545f4914f6cdd1dULL;
	if (fx.rng == 0)
		fx.rng = 0x9e3779b97f4a7c15ULL;
	fx.foreign_heap = (unsigned char *)malloc(64);
	fx.foreign_interior = (struct sk_buff *)(void *)
	    ((unsigned char *)skb_double_slot_at(1) + 4);

	skb_double_reset();
	judge_reset();
	skb_queue_head_init(&fx.q);
	judge_attach(&fx.q, &fx.enq, &fx.deq, &fx.unl);

	for (i = 0; i < iters; i++) {
		unsigned int kind;

		fx.op_index = i;
		judge_pre();
		kind = fx_pick_kind(&fx);
		fx_step(&fx, kind);
		if (fx.failed)
			goto witness;
		if (fx_check_counters(&fx) != 0)
			goto witness;
		if (judge_post(i, op_names[kind], fx.exp_df, fx.exp_sf,
		    fx.exp_uaf, fx.exp_fb) != 0)
			goto witness;
	}
	out->ops = iters;
	out->digest = fx_digest(&fx);

	fx_drain(&fx);
	if (fx_check_counters(&fx) != 0)
		goto witness;
	if (judge_check_pairing(&bad) != 0 ||
	    judge_check_queue(&bad) != 0) {
		judge_witness(iters, bad.kind, "end of run: %s", bad.detail);
		out->ops = iters;
		free(fx.foreign_heap);
		return -1;
	}
	if (fx.est.n_alloc != fx.est.n_free) {
		judge_witness(iters, "pairing",
		    "end of run: n_alloc = %u, n_free = %u",
		    fx.est.n_alloc, fx.est.n_free);
		out->ops = iters;
		free(fx.foreign_heap);
		return -1;
	}
	free(fx.foreign_heap);
	return 0;

witness:
	if (!suppress_recent)
		fx_dump_recent(&fx, fx.op_index);
	out->ops = fx.op_index + 1ul;
	/* release what the aborted run still holds: the failure is the witness,
	 * not an allocator report */
	{
		unsigned int k;

		for (k = 0; k < fx.n; k++)
			kfree_skb(fx.rec[k].p);
	}
	free(fx.foreign_heap);
	return -1;
}
