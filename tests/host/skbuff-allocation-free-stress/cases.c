/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * cases.c - the scenario matrix of the skbuff contract harness.
 *
 * Nineteen cells in four families.  Each cell is an operation sequence with
 * its own parameter domain and its own invariant; expectations are read off
 * the production headers (skbuff_pool.h, linuxkpi_core.h) and off the Linux
 * semantics those headers mirror, never off the double's own code.  A cell
 * prints exactly one line
 *
 *   SKBST: <cell-id> PASS|FAIL <details>
 *
 * and the run counts the failures.
 *
 *   A1 lifecycle pairing   4 cells: alloc/free pairs, capacity + recycle under
 *                          a fixed geometry, the realloc-after-free path, a
 *                          shuffled drain.  Every cell ends with
 *                          n_alloc - n_free == live slots.
 *   A2 geometry boundaries 7 cells: put/push/pull/trim/reserve at the exact
 *                          boundary, one past it and far past it, the
 *                          reserve/headroom interaction, zero-length ops and
 *                          the far-out-of-range refusals.
 *   A3 queue/sentinel      4 cells: tail enqueue, peek on an empty queue (the
 *                          documented strengthened contract), FIFO dequeue
 *                          order, freeing a queued skb.
 *   A4 named defects       4 cells: double free, stray free, use-after-free
 *                          write, corrupted free slot caught by the fence.
 */

#include <stdarg.h>

#include "judge.h"

/* ------------------------------------------------------------------ */
/* cell scaffolding                                                    */
/* ------------------------------------------------------------------ */

struct cell_ctx {
	const char			*id;
	struct sk_buff_head		q;
	unsigned long			enq, deq, unl;
	char				why[220];
	struct lkpi_skb_pool_stats	s0;
};

static unsigned long zero_enq, zero_deq, zero_unl;

static void
c_fail(struct cell_ctx *c, const char *fmt, ...)
{
	va_list ap;

	if (c->why[0] != '\0')
		return;
	va_start(ap, fmt);
	vsnprintf(c->why, sizeof(c->why), fmt, ap);
	va_end(ap);
}

/* snapshot for the "a refusal changes nothing" assertions */
struct geo_snap {
	uintptr_t	data;
	unsigned int	tail;
	unsigned int	len;
	unsigned int	end;
};

static void
c_snap(struct geo_snap *s, const struct sk_buff *skb)
{

	s->data = (uintptr_t)skb->data;
	s->tail = skb->tail;
	s->len = skb->len;
	s->end = skb->end;
}

static int
c_unchanged(const struct geo_snap *s, const struct sk_buff *skb)
{

	return s->data == (uintptr_t)skb->data && s->tail == skb->tail &&
	    s->len == skb->len && s->end == skb->end;
}

static void
c_setup(struct cell_ctx *c, const char *id, enum skb_profile profile,
    const struct skb_pool_cfg *cfg)
{

	c->id = id;
	c->why[0] = '\0';
	c->enq = c->deq = c->unl = 0;
	if (cfg != NULL)
		skb_double_init_cfg(cfg);
	else
		skb_double_init(profile);
	skb_queue_head_init(&c->q);
	judge_reset();
	judge_attach(&c->q, &c->enq, &c->deq, &c->unl);
	lkpi_skb_pool_stats(&c->s0);
}

/* the four families, plus the named-counter ledger, at the end of a cell */
static void
c_end(struct cell_ctx *c, int df, int sf, int uaf, int fb)
{
	struct lkpi_skb_pool_stats s1;
	struct skb_bad bad;

	lkpi_skb_pool_stats(&s1);
	if (judge_check_geometry(&bad) != 0)
		c_fail(c, "geometry: %s", bad.detail);
	else if (judge_check_pairing(&bad) != 0)
		c_fail(c, "pairing: %s", bad.detail);
	else if (judge_check_named(&bad, &c->s0, &s1, df, sf, uaf, fb) != 0)
		c_fail(c, "counters: %s", bad.detail);
	else if (judge_check_queue(&bad) != 0)
		c_fail(c, "queue: %s", bad.detail);
}

static unsigned int
c_geo_bad(void)
{

	return lkpi_skb_geo_bad_count();
}

/* the unused zero counters satisfy the queue attach when a cell runs no
 * queue bookkeeping of its own */
static void __attribute__((unused))
c_noop(void)
{

	(void)zero_enq;
	(void)zero_deq;
	(void)zero_unl;
}

/* ------------------------------------------------------------------ */
/* A1 lifecycle pairing                                                */
/* ------------------------------------------------------------------ */

static void
cell_a1_1(struct cell_ctx *c)
{
	struct sk_buff *p;
	struct lkpi_skb_pool_stats s;

	c_setup(c, "A1.1", SKB_PROFILE_HOST, NULL);

	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "alloc_skb(64) returned NULL");
		return;
	}
	if (p->len != 0 || p->tail != 0 || p->end != 64)
		c_fail(c, "a fresh allocation must be empty with end == 64 (len=%u tail=%u end=%u)",
		    p->len, p->tail, p->end);
	if (skb_headroom(p) != 0)
		c_fail(c, "alloc_skb must not reserve headroom (headroom=%u)",
		    skb_headroom(p));
	if (skb_tailroom(p) != 64)
		c_fail(c, "a fresh 64-byte allocation must offer 64 bytes of tailroom (got %d)",
		    skb_tailroom(p));
	if (p->_gen == 0 || p->_gen == LKPI_SKB_FREED_MAGIC)
		c_fail(c, "a handed-out skb must carry an owner generation (0x%08x)",
		    p->_gen);

	kfree_skb(p);
	lkpi_skb_pool_stats(&s);
	if (s.skb_used != 0)
		c_fail(c, "the slot must be released again (skb_used=%u)", s.skb_used);
	if (s.n_alloc != 1 || s.n_free != 1)
		c_fail(c, "one alloc and one free expected (n_alloc=%u n_free=%u)",
		    s.n_alloc, s.n_free);
	if (s.n_alloc - s.n_free != 0)
		c_fail(c, "conservation fails: n_alloc - n_free = %u",
		    s.n_alloc - s.n_free);

	c_end(c, 0, 0, 0, 0);
}

static void
cell_a1_2(struct cell_ctx *c)
{
	const struct skb_pool_cfg fixed = {
		.n_header = 4, .n_data = 4, .data_sz = 64, .heap_fallback = 0
	};
	struct sk_buff *p[6];
	struct lkpi_skb_pool_stats s;
	unsigned int i;
	int distinct = 1;

	c_setup(c, "A1.2", SKB_PROFILE_HOST, &fixed);

	for (i = 0; i < fixed.n_header; i++) {
		p[i] = alloc_skb(32, GFP_KERNEL);
		if (p[i] == NULL) {
			c_fail(c, "allocation %u of a %u-slot pool returned NULL",
			    i, fixed.n_header);
			return;
		}
	}
	for (i = 0; i < fixed.n_header && distinct; i++) {
		unsigned int j;

		for (j = i + 1; j < fixed.n_header; j++)
			if (p[i] == p[j]) {
				c_fail(c, "two live allocations share the same slot");
				distinct = 0;
			}
	}
	lkpi_skb_pool_stats(&s);
	if (s.skb_used != fixed.n_header)
		c_fail(c, "skb_used must be %u at capacity (got %u)",
		    fixed.n_header, s.skb_used);

	/* one past the fixed geometry, with no heap behind the pool */
	p[4] = alloc_skb(32, GFP_KERNEL);
	if (p[4] != NULL)
		c_fail(c, "an exhausted fixed pool without a heap fallback must fail");
	lkpi_skb_pool_stats(&s);
	if (s.alloc_fail != 1)
		c_fail(c, "the refused allocation must be counted (alloc_fail=%u)",
		    s.alloc_fail);
	if (s.slot_overflow != 0 || s.data_overflow != 0)
		c_fail(c, "a pool with no heap fallback must not report overflow paths (slot=%u data=%u)",
		    s.slot_overflow, s.data_overflow);

	/* an oversize request on a no-fallback pool fails instead of falling
	 * back to the heap, and the request is still recorded */
	p[5] = alloc_skb(fixed.data_sz + 1, GFP_KERNEL);
	if (p[5] != NULL)
		c_fail(c, "an oversize request without a heap fallback must fail");
	lkpi_skb_pool_stats(&s);
	if (s.alloc_fail != 2)
		c_fail(c, "the refused oversize request must be counted (alloc_fail=%u)",
		    s.alloc_fail);
	if (s.dyalloc_oversize != 1)
		c_fail(c, "the oversize request must be recorded (dyalloc_oversize=%u)",
		    s.dyalloc_oversize);
	if (s.dyalloc_live != 0)
		c_fail(c, "nothing may be left in flight on the heap (dyalloc_live=%u)",
		    s.dyalloc_live);

	for (i = 0; i < fixed.n_header; i++)
		kfree_skb(p[i]);
	lkpi_skb_pool_stats(&s);
	if (s.skb_used != 0 || s.data_used != 0)
		c_fail(c, "draining must release every slot (skb_used=%u data_used=%u)",
		    s.skb_used, s.data_used);

	/* recycle: the released slots must be handable out again */
	for (i = 0; i < fixed.n_header; i++) {
		p[i] = alloc_skb(32, GFP_KERNEL);
		if (p[i] == NULL) {
			c_fail(c, "recycled allocation %u failed", i);
			return;
		}
	}
	for (i = 0; i < fixed.n_header; i++)
		kfree_skb(p[i]);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a1_3(struct cell_ctx *c)
{
	struct sk_buff *a, *b;
	uint32_t gen_a;
	int slot_a;
	struct lkpi_skb_pool_stats s;

	c_setup(c, "A1.3", SKB_PROFILE_HOST, NULL);

	a = alloc_skb(128, GFP_KERNEL);
	if (a == NULL) {
		c_fail(c, "alloc_skb(128) returned NULL");
		return;
	}
	gen_a = a->_gen;
	slot_a = skb_double_slot_index(a);
	if (slot_a < 0) {
		c_fail(c, "the host profile must hand out a static slot");
		return;
	}
	if (skb_put(a, 16) == NULL)
		c_fail(c, "put(16) into a fresh 128-byte allocation must succeed");

	kfree_skb(a);
	if (a->_gen != LKPI_SKB_FREED_MAGIC)
		c_fail(c, "a released slot must read back the release magic (0x%08x)",
		    a->_gen);

	/* realloc after free: a released slot is handable out again */
	b = alloc_skb(128, GFP_KERNEL);
	if (b == NULL) {
		c_fail(c, "the realloc-after-free allocation returned NULL");
		return;
	}
	if (skb_double_slot_index(b) != slot_a)
		c_fail(c, "the single released slot must be the one reissued");
	if (b->_gen == LKPI_SKB_FREED_MAGIC || b->_gen == 0)
		c_fail(c, "the reissued slot must carry an owner generation (0x%08x)",
		    b->_gen);
	if (b->_gen == gen_a)
		c_fail(c, "the reissue must advance the generation (still 0x%08x)",
		    gen_a);
	if (b->len != 0 || b->tail != 0 || b->end != 128)
		c_fail(c, "the reissued slot must be empty with fresh geometry (len=%u tail=%u end=%u)",
		    b->len, b->tail, b->end);
	if (skb_tailroom(b) != 128)
		c_fail(c, "the reissued slot must offer the full tailroom again (got %d)",
		    skb_tailroom(b));

	kfree_skb(b);
	lkpi_skb_pool_stats(&s);
	if (s.n_alloc != 2 || s.n_free != 2 || s.skb_used != 0)
		c_fail(c, "pairing across the reissue is wrong (n_alloc=%u n_free=%u used=%u)",
		    s.n_alloc, s.n_free, s.skb_used);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a1_4(struct cell_ctx *c)
{
	static const unsigned int order[6] = { 3, 0, 5, 1, 4, 2 };
	struct sk_buff *p[6];
	struct lkpi_skb_pool_stats s;
	unsigned int i;

	c_setup(c, "A1.4", SKB_PROFILE_HOST, NULL);

	for (i = 0; i < 6; i++) {
		p[i] = alloc_skb(16u + i, GFP_KERNEL);
		if (p[i] == NULL) {
			c_fail(c, "allocation %u returned NULL", i);
			return;
		}
	}
	for (i = 0; i < 6; i++)
		kfree_skb(p[order[i]]);

	lkpi_skb_pool_stats(&s);
	if (s.skb_used != 0 || s.data_used != 0)
		c_fail(c, "a shuffled drain must release everything (skb_used=%u data_used=%u)",
		    s.skb_used, s.data_used);
	if (s.n_alloc != 6 || s.n_free != 6)
		c_fail(c, "six pairs expected (n_alloc=%u n_free=%u)", s.n_alloc,
		    s.n_free);
	if (s.skb_high != 6)
		c_fail(c, "skb_high must record the peak of 6 (got %u)", s.skb_high);

	for (i = 0; i < 6; i++) {
		p[i] = alloc_skb(16u + i, GFP_KERNEL);
		if (p[i] == NULL) {
			c_fail(c, "the refill allocation %u failed", i);
			return;
		}
	}
	for (i = 0; i < 6; i++)
		kfree_skb(p[i]);
	c_end(c, 0, 0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* A2 geometry boundaries                                              */
/* ------------------------------------------------------------------ */

static void
cell_a2_1(struct cell_ctx *c)
{
	struct sk_buff *p;
	struct geo_snap s0;
	unsigned char *r;
	unsigned int g0;

	c_setup(c, "A2.1", SKB_PROFILE_HOST, NULL);
	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "alloc_skb(64) returned NULL");
		return;
	}
	g0 = c_geo_bad();

	r = skb_put(p, 64);
	if (r == NULL)
		c_fail(c, "put(64) exactly into 64 bytes of tailroom must succeed");
	else if (r != p->head)
		c_fail(c, "put must return the old tail (offset %u)",
		    (unsigned int)(r - p->head));
	if (p->len != 64 || p->tail != 64 || skb_tailroom(p) != 0)
		c_fail(c, "after put(64): len=%u tail=%u tailroom=%d (want 64/64/0)",
		    p->len, p->tail, skb_tailroom(p));

	c_snap(&s0, p);
	if (skb_put(p, 1) != NULL)
		c_fail(c, "put(1) past a full buffer must return NULL");
	if (!c_unchanged(&s0, p))
		c_fail(c, "a refused put must leave the geometry alone");
	if (c_geo_bad() != g0 + 1)
		c_fail(c, "the refused put must be counted (geo_bad %u -> %u)",
		    g0, c_geo_bad());

	c_snap(&s0, p);
	if (skb_put(p, 4096) != NULL)
		c_fail(c, "put(4096) far past the boundary must return NULL");
	if (!c_unchanged(&s0, p))
		c_fail(c, "a far-past put must leave the geometry alone");
	if (c_geo_bad() != g0 + 2)
		c_fail(c, "the second refusal must be counted (geo_bad=%u)",
		    c_geo_bad());

	r = skb_put(p, 0);
	if (r == NULL || (unsigned int)(r - p->head) != 64)
		c_fail(c, "put(0) on a full buffer is legal and returns the tail");
	if (p->len != 64 || p->tail != 64)
		c_fail(c, "put(0) must not change the geometry");

	kfree_skb(p);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a2_2(struct cell_ctx *c)
{
	struct sk_buff *p;
	struct geo_snap s0;
	void *r;
	unsigned int g0;

	c_setup(c, "A2.2", SKB_PROFILE_HOST, NULL);
	p = dev_alloc_skb(64);
	if (p == NULL) {
		c_fail(c, "dev_alloc_skb(64) returned NULL");
		return;
	}
	if (skb_headroom(p) != NET_SKB_PAD)
		c_fail(c, "dev_alloc_skb must reserve NET_SKB_PAD headroom (got %u)",
		    skb_headroom(p));
	if (p->len != 0 || p->tail != NET_SKB_PAD || p->end != 64 + NET_SKB_PAD)
		c_fail(c, "a fresh dev allocation must be empty with end == size + pad (len=%u tail=%u end=%u)",
		    p->len, p->tail, p->end);
	if (skb_tailroom(p) != 64)
		c_fail(c, "a fresh dev allocation must offer the full tailroom (got %d)",
		    skb_tailroom(p));

	g0 = c_geo_bad();
	r = skb_push(p, NET_SKB_PAD);
	if (r == NULL)
		c_fail(c, "push(NET_SKB_PAD) into the reserved headroom must succeed");
	else if ((unsigned char *)r != p->head)
		c_fail(c, "push must return the new head (offset %u)",
		    (unsigned int)((unsigned char *)r - p->head));
	if (p->len != NET_SKB_PAD || skb_headroom(p) != 0)
		c_fail(c, "after push: len=%u headroom=%u (want %u/0)", p->len,
		    skb_headroom(p), NET_SKB_PAD);

	c_snap(&s0, p);
	if (skb_push(p, 1) != NULL)
		c_fail(c, "push(1) with no headroom left must return NULL");
	if (!c_unchanged(&s0, p))
		c_fail(c, "a refused push must leave the geometry alone");
	if (c_geo_bad() != g0 + 1)
		c_fail(c, "the refused push must be counted (geo_bad=%u)",
		    c_geo_bad());

	c_snap(&s0, p);
	if (skb_push(p, 4096) != NULL)
		c_fail(c, "push(4096) far past the boundary must return NULL");
	if (!c_unchanged(&s0, p))
		c_fail(c, "a far-past push must leave the geometry alone");
	if (c_geo_bad() != g0 + 2)
		c_fail(c, "the second refusal must be counted (geo_bad=%u)",
		    c_geo_bad());

	r = skb_push(p, 0);
	if (r == NULL || (unsigned char *)r != p->head)
		c_fail(c, "push(0) is legal and returns the current head");

	kfree_skb(p);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a2_3(struct cell_ctx *c)
{
	struct sk_buff *p;
	struct geo_snap s0;
	unsigned char *r;

	c_setup(c, "A2.3", SKB_PROFILE_HOST, NULL);
	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "alloc_skb(64) returned NULL");
		return;
	}
	if (skb_put(p, 64) == NULL) {
		c_fail(c, "the setup put(64) failed");
		return;
	}

	r = skb_pull(p, 32);
	if (r == NULL || (unsigned int)(r - p->head) != 32)
		c_fail(c, "pull(32) must return data advanced by 32");
	if (p->len != 32 || skb_headroom(p) != 32 || p->tail != 64)
		c_fail(c, "after pull(32): len=%u headroom=%u tail=%u (want 32/32/64)",
		    p->len, skb_headroom(p), p->tail);

	r = skb_pull(p, 32);
	if (r == NULL || p->len != 0)
		c_fail(c, "pull of the remaining length must succeed and empty the skb (len=%u)",
		    p->len);

	c_snap(&s0, p);
	if (skb_pull(p, 1) != NULL)
		c_fail(c, "pull(1) past the end of the payload must return NULL");
	if (!c_unchanged(&s0, p))
		c_fail(c, "a refused pull must leave the geometry alone");

	r = skb_pull(p, 0);
	if (r == NULL || r != p->data)
		c_fail(c, "pull(0) is legal and returns data unchanged");

	kfree_skb(p);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a2_4(struct cell_ctx *c)
{
	struct sk_buff *p;

	c_setup(c, "A2.4", SKB_PROFILE_HOST, NULL);
	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "alloc_skb(64) returned NULL");
		return;
	}
	if (skb_put(p, 64) == NULL) {
		c_fail(c, "the setup put(64) failed");
		return;
	}

	skb_trim(p, 32);
	if (p->len != 32 || p->tail != 32)
		c_fail(c, "trim(32) must shorten len and tail (len=%u tail=%u)",
		    p->len, p->tail);
	skb_trim(p, 0);
	if (p->len != 0 || p->tail != 0)
		c_fail(c, "trim(0) must empty the skb (len=%u tail=%u)", p->len,
		    p->tail);
	skb_trim(p, 0);
	if (p->len != 0 || p->tail != 0)
		c_fail(c, "trimming an empty skb must not change it");

	/* trim only shrinks: a larger length is a no-op (header, line 1855) */
	if (skb_put(p, 16) == NULL) {
		c_fail(c, "the refill put(16) failed");
		return;
	}
	skb_trim(p, 64);
	if (p->len != 16 || p->tail != 16)
		c_fail(c, "trim to a larger length must be a no-op (len=%u tail=%u)",
		    p->len, p->tail);

	/* the room a trim released is usable again */
	if (skb_put(p, 48) == NULL || p->len != 64)
		c_fail(c, "the tailroom released by trim must be usable again (len=%u)",
		    p->len);

	kfree_skb(p);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a2_5(struct cell_ctx *c)
{
	struct sk_buff *a, *b;
	struct geo_snap s0;
	unsigned char *r;
	unsigned int g0;

	c_setup(c, "A2.5", SKB_PROFILE_HOST, NULL);

	a = alloc_skb(64, GFP_KERNEL);
	if (a == NULL) {
		c_fail(c, "alloc_skb(64) returned NULL");
		return;
	}
	g0 = c_geo_bad();
	skb_reserve(a, 64);
	if (skb_headroom(a) != 64 || skb_tailroom(a) != 0 || a->len != 0)
		c_fail(c, "reserve(64) must move data and tail to 64 (headroom=%u tailroom=%d len=%u)",
		    skb_headroom(a), skb_tailroom(a), a->len);
	c_snap(&s0, a);
	skb_reserve(a, 1);
	if (!c_unchanged(&s0, a))
		c_fail(c, "a refused reserve must leave the geometry alone");
	if (c_geo_bad() != g0 + 1)
		c_fail(c, "the refused reserve must be counted (geo_bad=%u)",
		    c_geo_bad());
	r = skb_put(a, 0);
	if (r == NULL || (unsigned int)(r - a->head) != 64)
		c_fail(c, "put(0) after a full reserve is legal");
	if (skb_put(a, 1) != NULL)
		c_fail(c, "put(1) after a full reserve has no room and must fail");
	if (c_geo_bad() != g0 + 2)
		c_fail(c, "the refused put after reserve must be counted");
	kfree_skb(a);

	/* reserve(0) is a no-op, and headroom taken by reserve is what push
	 * later consumes */
	b = dev_alloc_skb(64);
	if (b == NULL) {
		c_fail(c, "dev_alloc_skb(64) returned NULL");
		return;
	}
	skb_reserve(b, 1);
	if (skb_headroom(b) != NET_SKB_PAD + 1 || skb_tailroom(b) != 63)
		c_fail(c, "reserve(1) on a dev allocation: headroom=%u tailroom=%d (want %u/63)",
		    skb_headroom(b), skb_tailroom(b), NET_SKB_PAD + 1);
	c_snap(&s0, b);
	skb_reserve(b, 0);
	if (!c_unchanged(&s0, b))
		c_fail(c, "reserve(0) must not change the geometry");
	if (skb_put(b, 63) == NULL)
		c_fail(c, "put(63) must fill the remaining 63 bytes exactly");
	if (skb_put(b, 1) != NULL)
		c_fail(c, "the byte past that boundary must be refused");
	if (skb_push(b, NET_SKB_PAD + 1) == NULL)
		c_fail(c, "push must be able to consume the whole headroom");
	if (skb_push(b, 1) != NULL)
		c_fail(c, "push past the headroom must be refused");

	kfree_skb(b);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a2_6(struct cell_ctx *c)
{
	struct sk_buff *p;
	struct geo_snap s0;
	unsigned int g0;

	c_setup(c, "A2.6", SKB_PROFILE_HOST, NULL);
	p = dev_alloc_skb(64);
	if (p == NULL) {
		c_fail(c, "dev_alloc_skb(64) returned NULL");
		return;
	}
	g0 = c_geo_bad();
	c_snap(&s0, p);

	if (skb_put(p, 0) == NULL)
		c_fail(c, "put(0) must succeed");
	if (skb_push(p, 0) == NULL)
		c_fail(c, "push(0) must succeed");
	if (skb_pull(p, 0) == NULL)
		c_fail(c, "pull(0) must succeed");
	skb_trim(p, 0);
	skb_reserve(p, 0);

	if (!c_unchanged(&s0, p))
		c_fail(c, "zero-length operations must leave the geometry alone");
	if (c_geo_bad() != g0)
		c_fail(c, "zero-length operations must not be counted as violations (%u -> %u)",
		    g0, c_geo_bad());

	kfree_skb(p);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a2_7(struct cell_ctx *c)
{
	struct sk_buff *p;
	struct geo_snap s0;
	unsigned int g0;

	c_setup(c, "A2.7", SKB_PROFILE_HOST, NULL);
	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "alloc_skb(64) returned NULL");
		return;
	}
	g0 = c_geo_bad();
	c_snap(&s0, p);

	if (skb_put(p, 0x7fffffffu) != NULL)
		c_fail(c, "a put of half the address space must be refused");
	if (skb_push(p, 0xffffffffu) != NULL)
		c_fail(c, "a push of UINT_MAX must be refused");
	skb_reserve(p, 0xffffffffu);
	if (!c_unchanged(&s0, p))
		c_fail(c, "the far-out-of-range refusals must leave the geometry alone");
	if (c_geo_bad() != g0 + 3)
		c_fail(c, "the three refusals must be counted (geo_bad=%u)",
		    c_geo_bad());

	/* the skb is still fully usable afterwards */
	if (skb_put(p, 64) == NULL || p->len != 64)
		c_fail(c, "the skb must stay usable after the refusals");
	if (skb_put(p, 1) != NULL)
		c_fail(c, "the boundary must still hold after the refusals");

	kfree_skb(p);
	c_end(c, 0, 0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* A3 queue/sentinel                                                   */
/* ------------------------------------------------------------------ */

static void
cell_a3_1(struct cell_ctx *c)
{
	struct sk_buff *a, *b, *d, *it;
	unsigned int n = 0;

	c_setup(c, "A3.1", SKB_PROFILE_HOST, NULL);
	a = alloc_skb(32, GFP_KERNEL);
	b = alloc_skb(32, GFP_KERNEL);
	d = alloc_skb(32, GFP_KERNEL);
	if (a == NULL || b == NULL || d == NULL) {
		c_fail(c, "the setup allocations failed");
		return;
	}

	__skb_queue_tail(&c->q, a);
	__skb_queue_tail(&c->q, b);
	__skb_queue_tail(&c->q, d);
	c->enq += 3;

	if (skb_queue_len(&c->q) != 3 || skb_queue_empty(&c->q))
		c_fail(c, "three enqueues must give qlen 3 (got %u)", skb_queue_len(&c->q));
	if (skb_peek(&c->q) != a)
		c_fail(c, "peek must return the first enqueued skb");
	if (skb_peek_tail(&c->q) != d)
		c_fail(c, "peek_tail must return the last enqueued skb");
	skb_queue_walk(&c->q, it)
		n++;
	if (n != 3)
		c_fail(c, "the walk must visit three nodes (visited %u)", n);
	if (a->list != &c->q || b->list != &c->q || d->list != &c->q)
		c_fail(c, "a queued skb must name its queue");

	kfree_skb(skb_dequeue(&c->q));
	c->deq++;
	kfree_skb(skb_dequeue(&c->q));
	c->deq++;
	kfree_skb(skb_dequeue(&c->q));
	c->deq++;
	if (!skb_queue_empty(&c->q))
		c_fail(c, "the drain must leave the queue empty");
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a3_2(struct cell_ctx *c)
{
	struct sk_buff *p;

	c_setup(c, "A3.2", SKB_PROFILE_HOST, NULL);

	if (skb_peek(&c->q) != NULL)
		c_fail(c, "peek on an empty queue must return NULL, not the sentinel");
	if (skb_peek_tail(&c->q) != NULL)
		c_fail(c, "peek_tail on an empty queue must return NULL");
	if (!skb_queue_empty(&c->q) || skb_queue_len(&c->q) != 0)
		c_fail(c, "a fresh queue must read empty");
	if (__skb_dequeue(&c->q) != NULL)
		c_fail(c, "dequeue on an empty queue must return NULL");

	p = alloc_skb(32, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "alloc_skb(32) returned NULL");
		return;
	}
	__skb_queue_tail(&c->q, p);
	c->enq++;
	if (skb_peek(&c->q) != p)
		c_fail(c, "peek must see the single queued skb");
	if (__skb_dequeue(&c->q) != p)
		c_fail(c, "dequeue must return the single queued skb");
	c->deq++;
	if (skb_peek(&c->q) != NULL)
		c_fail(c, "peek must be NULL again once the queue drained");
	if (p->list != NULL)
		c_fail(c, "a dequeued skb must no longer name a queue");

	kfree_skb(p);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a3_3(struct cell_ctx *c)
{
	struct sk_buff *p[4];
	unsigned int i;

	c_setup(c, "A3.3", SKB_PROFILE_HOST, NULL);
	for (i = 0; i < 4; i++) {
		p[i] = alloc_skb(24, GFP_KERNEL);
		if (p[i] == NULL) {
			c_fail(c, "allocation %u failed", i);
			return;
		}
		__skb_queue_tail(&c->q, p[i]);
		c->enq++;
	}
	for (i = 0; i < 4; i++) {
		struct sk_buff *got = __skb_dequeue(&c->q);

		if (got != p[i]) {
			c_fail(c, "dequeue %u must return the %u-th enqueued skb (FIFO)",
			    i, i);
			return;
		}
		c->deq++;
		if (skb_queue_len(&c->q) != 3 - i)
			c_fail(c, "qlen after dequeue %u: %u (want %u)", i,
			    skb_queue_len(&c->q), 3 - i);
		if (got->next != NULL || got->prev != NULL || got->list != NULL)
			c_fail(c, "a dequeued skb must be fully detached");
	}
	if (skb_peek(&c->q) != NULL)
		c_fail(c, "the drained queue must peek NULL");

	for (i = 0; i < 4; i++)
		kfree_skb(p[i]);
	c_end(c, 0, 0, 0, 0);
}

static void
cell_a3_4(struct cell_ctx *c)
{
	struct sk_buff *p[4];
	struct sk_buff *it;
	unsigned int n = 0;

	c_setup(c, "A3.4", SKB_PROFILE_HOST, NULL);
	for (unsigned int i = 0; i < 4; i++) {
		p[i] = alloc_skb(24, GFP_KERNEL);
		if (p[i] == NULL) {
			c_fail(c, "allocation %u failed", i);
			return;
		}
		__skb_queue_tail(&c->q, p[i]);
		c->enq++;
	}

	/* the documented way out of a queue (linuxkpi_core.h line 1851) */
	skb_unlink(p[1], &c->q);
	c->unl++;
	if (skb_queue_len(&c->q) != 3)
		c_fail(c, "unlink must drop qlen to 3 (got %u)", skb_queue_len(&c->q));
	if (p[1]->list != NULL)
		c_fail(c, "an unlinked skb must not name a queue");
	if (skb_peek(&c->q) != p[0] || skb_peek_tail(&c->q) != p[3])
		c_fail(c, "unlink must preserve the head and the tail of the queue");
	kfree_skb(p[1]);

	/* releasing a queued skb must leave the queue walkable and live-only;
	 * the object leaves the queue as part of being released, so the
	 * bookkeeping counts that as a removal too */
	kfree_skb(p[2]);
	c->unl++;
	if (skb_queue_len(&c->q) != 2)
		c_fail(c, "releasing a queued skb must drop qlen to 2 (got %u)",
		    skb_queue_len(&c->q));
	skb_queue_walk(&c->q, it) {
		n++;
		if (it->_gen == 0 || it->_gen == LKPI_SKB_FREED_MAGIC) {
			c_fail(c, "a queue must never hold a skb without an owner generation");
			return;
		}
	}
	if (n != 2)
		c_fail(c, "the queue must hold exactly the two survivors (walks %u)", n);
	if (skb_peek(&c->q) != p[0] || skb_peek_tail(&c->q) != p[3])
		c_fail(c, "the survivors must keep their order");

	while ((it = __skb_dequeue(&c->q)) != NULL) {
		c->deq++;
		kfree_skb(it);
	}
	if (skb_queue_len(&c->q) != 0 || skb_peek(&c->q) != NULL ||
	    !skb_queue_empty(&c->q))
		c_fail(c, "an emptied queue must read empty");
	c_end(c, 0, 0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* A4 named defects                                                    */
/* ------------------------------------------------------------------ */

static void
cell_a4_1(struct cell_ctx *c)
{
	struct sk_buff *p;
	struct lkpi_skb_pool_stats s;

	c_setup(c, "A4.1", SKB_PROFILE_HOST, NULL);
	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "alloc_skb(64) returned NULL");
		return;
	}
	kfree_skb(p);

	/* the second release of the same slot is named and skipped: it must
	 * not reach the allocator and must not change the accounting */
	kfree_skb(p);
	lkpi_skb_pool_stats(&s);
	if (s.double_free != 1)
		c_fail(c, "the second free must be named (double_free=%u)",
		    s.double_free);
	if (s.n_free != 1 || s.skb_used != 0)
		c_fail(c, "a skipped double free must release nothing (n_free=%u used=%u)",
		    s.n_free, s.skb_used);
	if (s.n_alloc - s.n_free != 0)
		c_fail(c, "conservation after the skipped free: n_alloc - n_free = %u",
		    s.n_alloc - s.n_free);

	/* the released slot is still canonical (the fence scan stays quiet),
	 * and it is handable out again */
	lkpi_skb_fence_scan();
	lkpi_skb_pool_stats(&s);
	if (s.fence_bad != 0)
		c_fail(c, "the skipped free must not disturb the slot state (fence_bad=%u)",
		    s.fence_bad);
	if (s.double_free != 1)
		c_fail(c, "the scan must not add to the double-free count");
	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "the slot must be reusable after a skipped double free");
		return;
	}
	kfree_skb(p);
	c_end(c, 1, 0, 0, 0);
}

static void
cell_a4_2(struct cell_ctx *c)
{
	static struct sk_buff foreign;
	struct sk_buff *interior = skb_double_slot_at(1);
	struct sk_buff *probe;
	unsigned char snapshot[sizeof(foreign)];
	struct lkpi_skb_pool_stats s0, s1;

	c_setup(c, "A4.2", SKB_PROFILE_HOST, NULL);
	memset(&foreign, 0, sizeof(foreign));
	lkpi_skb_pool_stats(&s0);

	/* a pointer that was never in the pool at all */
	kfree_skb(&foreign);
	/* a pointer inside the pool's storage but not at a slot boundary */
	kfree_skb((struct sk_buff *)(void *)((unsigned char *)interior + 4));
	/*
	 * ... and the same foreign object carrying a released-looking slot
	 * state: stray_free is defined by the pointer being outside the pool
	 * (skbuff_pool.h line 95), so the dispatch must not be fooled by the
	 * payload.
	 */
	foreign._gen = LKPI_SKB_FREED_MAGIC;
	memcpy(snapshot, &foreign, sizeof(foreign));
	kfree_skb(&foreign);

	lkpi_skb_pool_stats(&s1);
	if (s1.stray_free != s0.stray_free + 3)
		c_fail(c, "all three foreign releases must be named (stray_free %u -> %u)",
		    s0.stray_free, s1.stray_free);
	if (s1.double_free != s0.double_free)
		c_fail(c, "a foreign pointer must not be classified as a double free");
	if (s1.n_free != s0.n_free || s1.n_alloc != s0.n_alloc)
		c_fail(c, "a refused foreign release must not touch the pairing counters");
	if (memcmp(snapshot, &foreign, sizeof(foreign)) != 0)
		c_fail(c, "a refused foreign release must not write to the object");
	if (s1.skb_used != 0 || s1.data_used != 0)
		c_fail(c, "a refused foreign release must not consume pool slots");

	/*
	 * None of those pointers was handed to the allocator: the pool's own
	 * slots are untouched and handable out again.
	 */
	probe = alloc_skb(64, GFP_KERNEL);
	if (probe == NULL) {
		c_fail(c, "the pool must stay usable after foreign releases");
		return;
	}
	kfree_skb(probe);
	c_end(c, 0, 3, 0, 0);
}

static void
cell_a4_3(struct cell_ctx *c)
{
	struct sk_buff *p;
	unsigned char *buf;
	unsigned char *payload_snapshot;
	struct sk_buff header_snapshot;
	struct lkpi_skb_pool_stats s0, s1;

	c_setup(c, "A4.3", SKB_PROFILE_HOST, NULL);
	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "alloc_skb(64) returned NULL");
		return;
	}
	if (skb_put(p, 32) == NULL) {
		c_fail(c, "the setup put failed");
		return;
	}
	if (skb_double_slot_buffer(p) == NULL) {
		c_fail(c, "the host profile must back the skb with a pool buffer");
		return;
	}
	kfree_skb(p);
	buf = skb_double_released_buffer(p);
	if (buf == NULL) {
		c_fail(c, "the released slot must still own its buffer here");
		return;
	}
	lkpi_skb_pool_stats(&s0);

	memcpy(&header_snapshot, p, sizeof(header_snapshot));
	payload_snapshot = (unsigned char *)malloc(skb_double_cfg()->data_sz);
	if (payload_snapshot == NULL) {
		c_fail(c, "the snapshot allocation failed");
		return;
	}
	memcpy(payload_snapshot, buf, skb_double_cfg()->data_sz);

	if (skb_put(p, 8) != NULL)
		c_fail(c, "put on a released slot must return NULL");
	if (skb_push(p, 8) != NULL)
		c_fail(c, "push on a released slot must return NULL");

	lkpi_skb_pool_stats(&s1);
	if (s1.uaf_write != s0.uaf_write + 2)
		c_fail(c, "both write attempts must be named (uaf_write %u -> %u)",
		    s0.uaf_write, s1.uaf_write);
	if (s1.n_free != s0.n_free || s1.n_alloc != s0.n_alloc)
		c_fail(c, "a refused write must not touch the pairing counters");
	if (memcmp(&header_snapshot, p, sizeof(*p)) != 0)
		c_fail(c, "a refused write must not touch the released header");
	if (memcmp(payload_snapshot, buf, skb_double_cfg()->data_sz) != 0)
		c_fail(c, "a refused write must not touch the payload");
	if (p->_gen != LKPI_SKB_FREED_MAGIC)
		c_fail(c, "the slot must still read back as released");

	/* the fence scan over a released-but-clean pool must stay quiet */
	lkpi_skb_fence_scan();
	lkpi_skb_pool_stats(&s1);
	if (s1.fence_bad != s0.fence_bad)
		c_fail(c, "the refused writes must not corrupt the free slot (fence_bad=%u)",
		    s1.fence_bad);

	free(payload_snapshot);
	c_end(c, 0, 0, 2, 0);
}

static void
cell_a4_4(struct cell_ctx *c)
{
	struct sk_buff *p;
	unsigned char *buf;
	unsigned char original;
	struct lkpi_skb_pool_stats s0, s1;

	c_setup(c, "A4.4", SKB_PROFILE_HOST, NULL);
	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "alloc_skb(64) returned NULL");
		return;
	}
	if (skb_double_slot_buffer(p) == NULL) {
		c_fail(c, "the host profile must back the skb with a pool buffer");
		return;
	}
	kfree_skb(p);
	buf = skb_double_released_buffer(p);
	if (buf == NULL) {
		c_fail(c, "the released slot must still own its buffer here");
		return;
	}
	lkpi_skb_pool_stats(&s0);

	/* a write through a stale pointer into the released buffer: the fence
	 * scan must catch it, and repairing the byte must silence it again */
	original = buf[7];
	buf[7] = (unsigned char)(original ^ 0xffu);
	lkpi_skb_fence_scan();
	lkpi_skb_pool_stats(&s1);
	if (s1.fence_bad != s0.fence_bad + 1)
		c_fail(c, "the scan must name the corrupted free slot (fence_bad %u -> %u)",
		    s0.fence_bad, s1.fence_bad);
	if (s1.uaf_write != s0.uaf_write)
		c_fail(c, "the scan must not be confused with a write attempt");

	/* repairing the slot lets the scan pass quietly again */
	buf[7] = original;
	lkpi_skb_fence_scan();
	lkpi_skb_pool_stats(&s1);
	if (s1.fence_bad != s0.fence_bad + 1)
		c_fail(c, "a repaired free slot must not be reported again (fence_bad=%u)",
		    s1.fence_bad);

	p = alloc_skb(64, GFP_KERNEL);
	if (p == NULL) {
		c_fail(c, "the slot must be reusable after the fence work");
		return;
	}
	kfree_skb(p);
	c_end(c, 0, 0, 0, 1);
}

/* ------------------------------------------------------------------ */
/* the matrix                                                          */
/* ------------------------------------------------------------------ */

struct cell_slot {
	const char	*id;
	void		(*run)(struct cell_ctx *c);
};

static const struct cell_slot cells[] = {
	{ "A1.1", cell_a1_1 },
	{ "A1.2", cell_a1_2 },
	{ "A1.3", cell_a1_3 },
	{ "A1.4", cell_a1_4 },
	{ "A2.1", cell_a2_1 },
	{ "A2.2", cell_a2_2 },
	{ "A2.3", cell_a2_3 },
	{ "A2.4", cell_a2_4 },
	{ "A2.5", cell_a2_5 },
	{ "A2.6", cell_a2_6 },
	{ "A2.7", cell_a2_7 },
	{ "A3.1", cell_a3_1 },
	{ "A3.2", cell_a3_2 },
	{ "A3.3", cell_a3_3 },
	{ "A3.4", cell_a3_4 },
	{ "A4.1", cell_a4_1 },
	{ "A4.2", cell_a4_2 },
	{ "A4.3", cell_a4_3 },
	{ "A4.4", cell_a4_4 },
};

int
skb_matrix_run(void)
{
	struct cell_ctx c;
	unsigned int i, pass = 0, fail = 0;

	c_noop();
	for (i = 0; i < ARRAY_SIZE(cells); i++) {
		memset(&c, 0, sizeof(c));
		cells[i].run(&c);
		if (c.why[0] == '\0') {
			printf("SKBST: %s PASS\n", cells[i].id);
			pass++;
		} else {
			printf("SKBST: %s FAIL %s\n", cells[i].id, c.why);
			fail++;
		}
		fflush(stdout);
	}
	printf("SKBST: matrix cells=%u pass=%u fail=%u\n",
	    (unsigned int)ARRAY_SIZE(cells), pass, fail);
	fflush(stdout);
	return (int)fail;
}

const char *const *
skb_matrix_cell_ids(unsigned int *count)
{
	static const char *ids[ARRAY_SIZE(cells)];
	static int built;
	unsigned int i;

	if (!built) {
		for (i = 0; i < ARRAY_SIZE(cells); i++)
			ids[i] = cells[i].id;
		built = 1;
	}
	*count = (unsigned int)ARRAY_SIZE(cells);
	return ids;
}
