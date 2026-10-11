/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * judge.c - the four invariant families and the first-witness report.
 *
 * The rule families and the reading behind each of them are written down in
 * judge.h.  Everything here compares the double's observable state against
 * the production headers' documented semantics and against the operation
 * stream's own bookkeeping; nothing is derived from the double's internals.
 */

#include <stdarg.h>

#include "judge.h"

static struct lkpi_skb_pool_stats	pre;
static int				quiet;
static struct sk_buff_head		*jq;
static const unsigned long		*j_enq;
static const unsigned long		*j_deq;
static const unsigned long		*j_unl;

void
judge_reset(void)
{

	memset(&pre, 0, sizeof(pre));
	jq = NULL;
	j_enq = NULL;
	j_deq = NULL;
	j_unl = NULL;
}

void
judge_attach(struct sk_buff_head *q, const unsigned long *enqueued,
    const unsigned long *dequeued, const unsigned long *unlinked)
{

	jq = q;
	j_enq = enqueued;
	j_deq = dequeued;
	j_unl = unlinked;
}

void
judge_pre(void)
{

	lkpi_skb_pool_stats(&pre);
}

void
judge_set_quiet(int on)
{

	quiet = on != 0;
}

void
judge_witness(unsigned long op_index, const char *kind, const char *fmt, ...)
{
	char detail[240];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(detail, sizeof(detail), fmt, ap);
	va_end(ap);
	if (!quiet) {
		printf("SKBST: first-witness op=%lu kind=%s detail=%s\n", op_index,
		    kind, detail);
		fflush(stdout);
	}
}

/* ------------------------------------------------------------------ */
/* 1. geometry                                                         */
/* ------------------------------------------------------------------ */

struct geom_ctx {
	struct skb_bad	*bad;
	int		stop;
};

static int
geom_visit(struct sk_buff *skb, int slot, int data_pool, void *arg)
{
	struct geom_ctx *c = (struct geom_ctx *)arg;
	struct skb_bad *bad = c->bad;
	uintptr_t off;

	(void)data_pool;
	if (skb->_gen == 0 || skb->_gen == LKPI_SKB_FREED_MAGIC) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "live skb slot=%d carries generation 0x%08x, which names no owner",
		    slot, skb->_gen);
		return 1;
	}
	if (skb->head == NULL || skb->data == NULL) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "live skb slot=%d has a NULL pointer (head=%p data=%p)",
		    slot, (const void *)skb->head, (const void *)skb->data);
		return 1;
	}
	if ((uintptr_t)skb->data < (uintptr_t)skb->head) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "live skb slot=%d data precedes head", slot);
		return 1;
	}
	off = (uintptr_t)skb->data - (uintptr_t)skb->head;
	if (off > skb->end) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "live skb slot=%d data offset %lu past end=%u",
		    slot, (unsigned long)off, skb->end);
		return 1;
	}
	if (off > skb->tail || skb->tail > skb->end) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "live skb slot=%d breaks data <= tail <= end (dataoff=%lu tail=%u end=%u)",
		    slot, (unsigned long)off, skb->tail, skb->end);
		return 1;
	}
	if (skb->len != (unsigned int)(skb->tail - off)) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "live skb slot=%d len=%u but tail - dataoff = %u",
		    slot, skb->len, (unsigned int)(skb->tail - off));
		return 1;
	}
	if (skb->data_len != 0) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "live skb slot=%d carries data_len=%u in a linear-only shim",
		    slot, skb->data_len);
		return 1;
	}
	if ((unsigned int)off + skb->len + (unsigned int)(skb->end - skb->tail)
	    != skb->end) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "live skb slot=%d headroom + len + tailroom != end",
		    slot);
		return 1;
	}
	return 0;
}

int
judge_check_geometry(struct skb_bad *bad)
{
	struct geom_ctx c;

	memset(bad, 0, sizeof(*bad));
	snprintf(bad->kind, sizeof(bad->kind), "geometry");
	c.bad = bad;
	c.stop = 0;
	skb_double_visit_live(geom_visit, &c);
	return bad->detail[0] != '\0' ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* 2. pairing                                                          */
/* ------------------------------------------------------------------ */

struct pair_ctx {
	unsigned int	static_live;
	unsigned int	pool_data;
	unsigned int	total;
};

static int
pair_visit(struct sk_buff *skb, int slot, int data_pool, void *arg)
{
	struct pair_ctx *c = (struct pair_ctx *)arg;

	(void)skb;
	c->total++;
	if (slot >= 0)
		c->static_live++;
	if (data_pool)
		c->pool_data++;
	return 0;
}

int
judge_check_pairing(struct skb_bad *bad)
{
	struct lkpi_skb_pool_stats st;
	struct pair_ctx c = { 0, 0, 0 };

	memset(bad, 0, sizeof(*bad));
	snprintf(bad->kind, sizeof(bad->kind), "pairing");
	lkpi_skb_pool_stats(&st);
	skb_double_visit_live(pair_visit, &c);

	if ((uint32_t)(st.n_alloc - st.n_free) != c.total) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "n_alloc - n_free = %u but %u skbs are live",
		    st.n_alloc - st.n_free, c.total);
		return -1;
	}
	if (st.skb_used != c.static_live) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "skb_used = %u but %u static header slots are live",
		    st.skb_used, c.static_live);
		return -1;
	}
	if (st.data_used != c.pool_data) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "data_used = %u but %u live skbs hold a pool data slot",
		    st.data_used, c.pool_data);
		return -1;
	}
	if (st.dyalloc_live != c.total - c.pool_data) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "dyalloc_live = %u but %u live skbs hold a heap buffer",
		    st.dyalloc_live, c.total - c.pool_data);
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* 3. named counters                                                   */
/* ------------------------------------------------------------------ */

static int
named_one(struct skb_bad *bad, const char *name, uint32_t before, uint32_t after,
    int expect)
{
	uint32_t delta = after - before;

	if (delta == (uint32_t)expect)
		return 0;
	if (expect == 0)
		snprintf(bad->detail, sizeof(bad->detail),
		    "%s moved by %u with no operation to account for it",
		    name, delta);
	else
		snprintf(bad->detail, sizeof(bad->detail),
		    "%s moved by %u, expected %d", name, delta, expect);
	return -1;
}

int
judge_check_named(struct skb_bad *bad, const struct lkpi_skb_pool_stats *before,
    const struct lkpi_skb_pool_stats *after, int expect_double_free,
    int expect_stray_free, int expect_uaf_write, int expect_fence_bad)
{

	memset(bad, 0, sizeof(*bad));
	snprintf(bad->kind, sizeof(bad->kind), "counter");
	if (named_one(bad, "double_free", before->double_free, after->double_free,
	    expect_double_free) != 0)
		return -1;
	if (named_one(bad, "stray_free", before->stray_free, after->stray_free,
	    expect_stray_free) != 0)
		return -1;
	if (named_one(bad, "uaf_write", before->uaf_write, after->uaf_write,
	    expect_uaf_write) != 0)
		return -1;
	if (named_one(bad, "fence_bad", before->fence_bad, after->fence_bad,
	    expect_fence_bad) != 0)
		return -1;
	return 0;
}

/* ------------------------------------------------------------------ */
/* 4. queue integrity                                                  */
/* ------------------------------------------------------------------ */

int
judge_check_queue(struct skb_bad *bad)
{
	struct sk_buff *s;
	unsigned long n = 0;
	long long expect;

	memset(bad, 0, sizeof(*bad));
	snprintf(bad->kind, sizeof(bad->kind), "queue");
	if (jq == NULL)
		return 0;

	expect = (long long)*j_enq - (long long)*j_deq - (long long)*j_unl;
	if ((long long)jq->qlen != expect) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "qlen = %u but the stream enqueued %lu, dequeued %lu and unlinked %lu",
		    jq->qlen, *j_enq, *j_deq, *j_unl);
		return -1;
	}
	for (s = jq->next; s != (struct sk_buff *)jq; s = s->next) {
		if (s == NULL) {
			snprintf(bad->detail, sizeof(bad->detail),
			    "queue chain ends in NULL instead of the sentinel");
			return -1;
		}
		if (n > jq->qlen) {
			snprintf(bad->detail, sizeof(bad->detail),
			    "queue chain longer than qlen = %u (cycle or stale link)",
			    jq->qlen);
			return -1;
		}
		if (s->_gen == 0 || s->_gen == LKPI_SKB_FREED_MAGIC) {
			snprintf(bad->detail, sizeof(bad->detail),
			    "queued skb carries generation 0x%08x, which names no owner",
			    s->_gen);
			return -1;
		}
		n++;
	}
	if (n != jq->qlen) {
		snprintf(bad->detail, sizeof(bad->detail),
		    "queue chain has %lu nodes but qlen = %u", n, jq->qlen);
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* the per-operation judge                                             */
/* ------------------------------------------------------------------ */

int
judge_post(unsigned long op_index, const char *op_kind, int expect_double_free,
    int expect_stray_free, int expect_uaf_write, int expect_fence_bad)
{
	struct lkpi_skb_pool_stats post;
	struct skb_bad bad;

	lkpi_skb_pool_stats(&post);
	if (judge_check_geometry(&bad) != 0 ||
	    judge_check_pairing(&bad) != 0 ||
	    judge_check_named(&bad, &pre, &post, expect_double_free,
	    expect_stray_free, expect_uaf_write, expect_fence_bad) != 0 ||
	    judge_check_queue(&bad) != 0) {
		judge_witness(op_index, bad.kind, "op=%s %s", op_kind, bad.detail);
		return -1;
	}
	return 0;
}
