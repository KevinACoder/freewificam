/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * skb_contract.c - the host double of the skbuff pool body.
 *
 * The production pool bodies behind components/linuxkpi/skbuff/skbuff_pool.h
 * are target-side code; this file supplies host bodies for every symbol the
 * production headers declare (pool API, geometry entry points, queue surgery,
 * platform hooks), so the contract can be exercised off board while the
 * headers stay the single source of shape.  The reading of each semantic
 * choice is written down in skb_contract.h.
 *
 * Two pieces only make sense with the storage in view and live here:
 *
 *   - slot memory map: a per-slot record naming the linear buffer (pool slot
 *     index or heap pointer) that survives the release of the header slot, so
 *     a released slot can still be integrity-scanned after its head/data
 *     pointers have been zeroed;
 *   - determinism: pool slots are re-filled with a per-slot fence pattern on
 *     release and heap-backed buffers are zeroed on allocation, so the run
 *     digest is a pure function of the operation stream and never of the
 *     host allocator's leftovers.
 *
 * The double is single-threaded, like every other host suite here; the
 * platform hooks are still taken around every mutating operation so the shape
 * of the production critical sections is preserved.
 */

#include "skb_contract.h"

/* ------------------------------------------------------------------ */
/* storage                                                            */
/* ------------------------------------------------------------------ */

/* what a header slot's linear buffer is, and where it came from */
#define SKB_BUF_NONE	0
#define SKB_BUF_POOL	1	/* a slot of the fixed data pool */
#define SKB_BUF_HEAP	2	/* lkpi_spl_malloc backing */

struct skb_buf_ref {
	int		kind;
	int		idx;		/* pool data slot index */
	void		*heap;		/* heap buffer (live only) */
};

struct skb_heap_rec {
	struct sk_buff		*p;
	uint32_t		gen;
	struct skb_buf_ref	buf;
};

#define SKB_HEAP_MAX	4096	/* live heap-backed headers */
#define SKB_TOMB_MAX	1024	/* released ones, the double-free window */

static struct sk_buff		pool_hdr[SKB_DOUBLE_MAX_HDR];
static unsigned char		pool_data[SKB_DOUBLE_MAX_DATA]
					[SKB_DOUBLE_MAX_DATA_SZ];
static struct skb_buf_ref	slot_buf[SKB_DOUBLE_MAX_HDR];
static unsigned int		data_free[SKB_DOUBLE_MAX_DATA];
static unsigned int		data_free_n;

static struct skb_heap_rec	heap_live[SKB_HEAP_MAX];
static unsigned int		heap_n;
static uintptr_t		tomb[SKB_TOMB_MAX];
static unsigned int		tomb_next;
static unsigned int		tomb_filled;

static struct lkpi_skb_pool_stats stats;
static unsigned int		gen_counter;
static unsigned int		geo_bad;
static int			pool_ready;

static const struct skb_pool_cfg profiles[2] = {
	/* host: the wide profile, wide enough to hold a burst of skbs */
	{ .n_header = 32, .n_data = 32, .data_sz = 1024, .heap_fallback = 1 },
	/* mcu: the constrained profile, fixed geometry, no heap behind it */
	{ .n_header = 8, .n_data = 8, .data_sz = 256, .heap_fallback = 0 },
};

static struct skb_pool_cfg	cfg;
static enum skb_profile		profile_sel = SKB_PROFILE_HOST;
static int			cfg_custom;
static int			verbose;

/* ------------------------------------------------------------------ */
/* platform hooks (the only bindings the production pool has)          */
/* ------------------------------------------------------------------ */

void
skb_double_set_verbose(int on)
{

	verbose = on != 0;
}

void
lkpi_spl_log(const char *fmt, ...)
{
	va_list ap;

	if (!verbose)
		return;
	fputs("lkpi_skb: ", stdout);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	fflush(stdout);
}

unsigned long
lkpi_spl_irqsave(void)
{

	return 0;
}

void
lkpi_spl_irqrestore(unsigned long flags)
{

	(void)flags;
}

void *
lkpi_spl_malloc(unsigned int size)
{

	return malloc(size != 0 ? size : 1u);
}

void
lkpi_spl_free(void *p)
{

	free(p);
}

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static uint32_t
gen_next(void)
{

	gen_counter++;
	if (gen_counter == 0 || gen_counter >= LKPI_SKB_FREED_MAGIC)
		gen_counter = 1;
	return gen_counter;
}

/* the fence byte of a free data slot: per-slot so a write through a stale
 * pointer into the wrong slot cannot pass as intact */
static unsigned char
fence_byte(unsigned int idx)
{

	return (unsigned char)(0xa5u ^ (0x11u * (idx + 1u)));
}

/*
 * Index of a static header slot, by arithmetic only - never dereferences the
 * pointer, so a freed heap pointer or an interior pointer is refused without
 * touching memory.  Returns -1 unless the pointer names a configured slot.
 */
static int
slot_index_of(const struct sk_buff *skb)
{
	uintptr_t base = (uintptr_t)&pool_hdr[0];
	uintptr_t addr = (uintptr_t)skb;
	uintptr_t span = (uintptr_t)(sizeof(pool_hdr[0]) * cfg.n_header);
	uintptr_t all = (uintptr_t)sizeof(pool_hdr);

	if (addr < base || addr >= base + all)
		return -1;
	if ((addr - base) % sizeof(pool_hdr[0]) != 0)
		return -1;
	if (addr - base >= span)
		return -1;		/* inside the array, outside the pool */
	return (int)((addr - base) / sizeof(pool_hdr[0]));
}

static int
heap_live_find(const struct sk_buff *skb)
{
	unsigned int i;

	for (i = 0; i < heap_n; i++)
		if (heap_live[i].p == skb)
			return (int)i;
	return -1;
}

static void
heap_live_del(unsigned int i)
{

	heap_live[i] = heap_live[heap_n - 1];
	heap_n--;
}

/* is this data-pool slot back on the free list?  A released header slot
 * keeps its buffer record, but the slot itself may have been handed to some
 * newer skb since - only a slot that is still free describes the released
 * object and is safe to inspect or perturb. */
static int
data_slot_is_free(unsigned int idx)
{
	unsigned int i;

	for (i = 0; i < data_free_n; i++)
		if (data_free[i] == idx)
			return 1;
	return 0;
}

static int
tomb_find(uintptr_t p)
{
	unsigned int i;

	for (i = 0; i < tomb_filled; i++)
		if (tomb[i] == p)
			return 1;
	return 0;
}

static void
tomb_add(uintptr_t p)
{

	tomb[tomb_next] = p;
	tomb_next = (tomb_next + 1u) % SKB_TOMB_MAX;
	if (tomb_filled < SKB_TOMB_MAX)
		tomb_filled++;
}

/* the linear invariant, read off the documented shape (header, lines
 * 2057-2058: tail always covers headroom + payload, no paged data) */
static int
geom_ok(const struct sk_buff *skb)
{
	uintptr_t off;

	if (skb->head == NULL || skb->data == NULL)
		return 0;
	if ((uintptr_t)skb->data < (uintptr_t)skb->head)
		return 0;
	off = (uintptr_t)skb->data - (uintptr_t)skb->head;
	if (off > skb->tail || skb->tail > skb->end)
		return 0;
	if (skb->len != (unsigned int)(skb->tail - off))
		return 0;
	if (skb->data_len != 0)
		return 0;
	return 1;
}

enum skb_write_ok {
	SKB_W_OK = 0,
	SKB_W_UAF,		/* released slot: counted, refused */
	SKB_W_STRAY,		/* not a pool object at all: counted, refused */
	SKB_W_GEO,		/* live object with a broken pre-state */
};

/*
 * Validate a geometry-op target.  Returns SKB_W_OK for a live, geometrically
 * sane object; the failure outcomes are already counted, and no field of a
 * refused object is touched.
 */
static enum skb_write_ok
write_target(struct sk_buff *skb)
{
	int slot;

	if (skb == NULL) {
		geo_bad++;
		return SKB_W_STRAY;
	}
	slot = slot_index_of(skb);
	if (slot >= 0) {
		if (pool_hdr[slot]._gen == LKPI_SKB_FREED_MAGIC) {
			lkpi_skb_uaf_hit(skb);
			return SKB_W_UAF;
		}
		if (pool_hdr[slot]._gen == 0) {
			geo_bad++;
			lkpi_spl_log("geometry op on slot %d, which was never handed out, refused", slot);
			return SKB_W_STRAY;
		}
	} else if (heap_live_find(skb) < 0) {
		geo_bad++;
		lkpi_spl_log("geometry op on a pointer outside the pool refused");
		return SKB_W_STRAY;
	}
	if (!geom_ok(skb)) {
		geo_bad++;
		lkpi_spl_log("geometry op on an object whose pre-state breaks the linear invariant refused");
		return SKB_W_GEO;
	}
	return SKB_W_OK;
}

/* ------------------------------------------------------------------ */
/* init / reset                                                        */
/* ------------------------------------------------------------------ */

static void
pool_reset(void)
{
	unsigned int i;

	memset(pool_hdr, 0, sizeof(pool_hdr));
	memset(pool_data, 0, sizeof(pool_data));
	memset(slot_buf, 0, sizeof(slot_buf));
	for (i = 0; i < SKB_DOUBLE_MAX_DATA; i++)
		data_free[i] = i;
	data_free_n = cfg.n_data;
	memset(heap_live, 0, sizeof(heap_live));
	heap_n = 0;
	memset(tomb, 0, sizeof(tomb));
	tomb_next = 0;
	tomb_filled = 0;
	memset(&stats, 0, sizeof(stats));
	gen_counter = 0;
	geo_bad = 0;
}

void
skb_double_init_cfg(const struct skb_pool_cfg *c)
{
	unsigned int i;

	if (c->n_header == 0 || c->n_data == 0 || c->data_sz == 0 ||
	    c->n_header > SKB_DOUBLE_MAX_HDR ||
	    c->n_data > SKB_DOUBLE_MAX_DATA ||
	    c->data_sz > SKB_DOUBLE_MAX_DATA_SZ) {
		lkpi_spl_log("requested geometry is empty or exceeds the double's storage");
		abort();
	}
	cfg = *c;
	/* remember whether the request still matches a named preset */
	cfg_custom = 1;
	for (i = 0; i < 2; i++)
		if (cfg.n_header == profiles[i].n_header &&
		    cfg.n_data == profiles[i].n_data &&
		    cfg.data_sz == profiles[i].data_sz &&
		    cfg.heap_fallback == profiles[i].heap_fallback) {
			profile_sel = (enum skb_profile)i;
			cfg_custom = 0;
		}
	pool_reset();
	pool_ready = 1;
	/* the fence byte must never be zero: the corruption op and the fence
	 * check both rely on a zero byte being foreign to every slot */
	for (i = 0; i < cfg.n_data; i++) {
		if (fence_byte(i) == 0) {
			lkpi_spl_log("fence byte collides with zero at data slot %u", i);
			abort();
		}
	}
	lkpi_skb_pool_init();
}

void
skb_double_init(enum skb_profile profile)
{

	if (profile != SKB_PROFILE_HOST && profile != SKB_PROFILE_MCU) {
		lkpi_spl_log("unknown profile %d, refusing to initialise", (int)profile);
		abort();
	}
	skb_double_init_cfg(&profiles[profile]);
	profile_sel = profile;
	cfg_custom = 0;
}

void
skb_double_reset(void)
{

	pool_reset();
	pool_ready = 1;
}

void
lkpi_skb_pool_init(void)
{

	if (pool_ready)
		return;
	/* lazily reachable without an explicit profile: the host shape wins */
	skb_double_init(SKB_PROFILE_HOST);
}

const struct skb_pool_cfg *
skb_double_cfg(void)
{

	return &cfg;
}

const char *
skb_double_profile_name(void)
{

	if (cfg_custom)
		return "custom";
	return profile_sel == SKB_PROFILE_MCU ? "mcu" : "host";
}

/* ------------------------------------------------------------------ */
/* allocation                                                          */
/* ------------------------------------------------------------------ */

static struct sk_buff *
pool_alloc(unsigned int size, unsigned int gfp, int dev)
{
	unsigned int need = size + (dev ? (unsigned int)NET_SKB_PAD : 0u);
	unsigned int i;
	unsigned long flags;
	struct sk_buff *skb = NULL;
	unsigned char *buf = NULL;
	int hdr_slot = -1;
	int hdr_heap = 0;
	struct skb_buf_ref buf_ref = { .kind = SKB_BUF_NONE, .idx = -1 };

	(void)gfp;	/* wait-or-fail only steers the heap; the double never waits */
	if (!pool_ready)
		lkpi_skb_pool_init();

	flags = lkpi_spl_irqsave();

	/*
	 * dyalloc_oversize is attributed to the request ("requests >
	 * DATA_BUF_SZ", skbuff_pool.h line 89) and is therefore taken before
	 * the supply is probed; slot_overflow/data_overflow belong to the heap
	 * fallback path ("<pool> exhausted -> heap", lines 90-91) and are
	 * taken only when the heap actually serves the request.
	 */
	if (need > cfg.data_sz)
		stats.dyalloc_oversize++;

	/* header slot: a never-handed-out or released slot is reusable */
	if (stats.skb_used < cfg.n_header) {
		for (i = 0; i < cfg.n_header; i++) {
			uint32_t g = pool_hdr[i]._gen;

			if (g == 0 || g == LKPI_SKB_FREED_MAGIC) {
				hdr_slot = (int)i;
				break;
			}
		}
	}
	if (hdr_slot >= 0) {
		skb = &pool_hdr[hdr_slot];
	} else if (cfg.heap_fallback) {
		skb = (struct sk_buff *)lkpi_spl_malloc((unsigned int)sizeof(*skb));
		if (skb == NULL) {
			stats.alloc_fail++;
			goto out;
		}
		if (heap_n >= SKB_HEAP_MAX) {
			/* cannot track a live heap header: fail cleanly */
			lkpi_spl_free(skb);
			skb = NULL;
			stats.alloc_fail++;
			goto out;
		}
		hdr_heap = 1;
		stats.slot_overflow++;
	} else {
		stats.alloc_fail++;
		goto out;
	}

	/* linear buffer */
	if (need <= cfg.data_sz && data_free_n > 0) {
		buf_ref.idx = (int)data_free[--data_free_n];
		buf_ref.kind = SKB_BUF_POOL;
		buf = pool_data[buf_ref.idx];
		stats.data_used++;
		if (stats.data_used > stats.data_high)
			stats.data_high = stats.data_used;
	} else if (need > cfg.data_sz) {
		if (!cfg.heap_fallback) {
			stats.alloc_fail++;
			goto fail_header;
		}
		buf = (unsigned char *)lkpi_spl_malloc(need);
		if (buf == NULL) {
			stats.alloc_fail++;
			goto fail_header;
		}
		buf_ref.kind = SKB_BUF_HEAP;
		buf_ref.heap = buf;
		stats.dyalloc_live++;
	} else {
		/* the data pool is exhausted: the documented heap outlet */
		stats.data_overflow++;
		if (!cfg.heap_fallback) {
			stats.alloc_fail++;
			goto fail_header;
		}
		buf = (unsigned char *)lkpi_spl_malloc(need != 0 ? need : 1u);
		if (buf == NULL) {
			stats.alloc_fail++;
			goto fail_header;
		}
		buf_ref.kind = SKB_BUF_HEAP;
		buf_ref.heap = buf;
		stats.dyalloc_live++;
	}

	memset(skb, 0, sizeof(*skb));
	skb->head = buf;
	skb->data = buf;
	skb->tail = 0;
	skb->end = need;
	skb->len = 0;
	skb->data_len = 0;
	skb->truesize = (unsigned int)sizeof(*skb) + need;
	skb->_gen = gen_next();
	if (buf_ref.kind == SKB_BUF_HEAP)
		memset(buf, 0, need);	/* host determinism, see the file comment */

	if (hdr_heap) {
		heap_live[heap_n].p = skb;
		heap_live[heap_n].gen = skb->_gen;
		heap_live[heap_n].buf = buf_ref;
		heap_n++;
	} else {
		slot_buf[hdr_slot] = buf_ref;
		stats.skb_used++;
		if (stats.skb_used > stats.skb_high)
			stats.skb_high = stats.skb_used;
	}
	stats.n_alloc++;
	if (dev) {
		skb->data += NET_SKB_PAD;
		skb->tail = NET_SKB_PAD;
	}
	goto out;

fail_header:
	if (hdr_heap && skb != NULL)
		lkpi_spl_free(skb);
	skb = NULL;
out:
	lkpi_spl_irqrestore(flags);
	return skb;
}

struct sk_buff *
lkpi_skb_alloc(unsigned int size, unsigned int gfp)
{

	return pool_alloc(size, gfp, 0);
}

struct sk_buff *
lkpi_dev_alloc_skb(unsigned int size, unsigned int gfp)
{

	return pool_alloc(size, gfp, 1);
}

/* ------------------------------------------------------------------ */
/* release                                                             */
/* ------------------------------------------------------------------ */

static void
buf_release(const struct skb_buf_ref *ref)
{

	if (ref->kind == SKB_BUF_POOL) {
		memset(pool_data[ref->idx], fence_byte((unsigned int)ref->idx),
		    cfg.data_sz);
		data_free[data_free_n++] = (unsigned int)ref->idx;
		stats.data_used--;
	} else if (ref->kind == SKB_BUF_HEAP) {
		stats.dyalloc_live--;
		lkpi_spl_free(ref->heap);
	}
}

/* canonical shape of a released header slot: every field zeroed, only the
 * generation carries the release magic.  Anything else is slot corruption. */
static void
slot_mark_released(struct sk_buff *skb)
{

	skb->next = NULL;
	skb->prev = NULL;
	skb->list = NULL;
	skb->head = NULL;
	skb->data = NULL;
	skb->tail = 0;
	skb->end = 0;
	skb->len = 0;
	skb->data_len = 0;
	skb->truesize = 0;
	skb->protocol = 0;
	skb->queue_mapping = 0;
	skb->priority = 0;
	skb->pkt_type = 0;
	skb->ip_summed = 0;
	skb->dev = NULL;
	skb->sk = NULL;
	memset(skb->cb, 0, sizeof(skb->cb));
	skb->_gen = LKPI_SKB_FREED_MAGIC;
}

static void
slot_release(int idx)
{

	buf_release(&slot_buf[idx]);
	/* the memory map stays: a released slot without its head pointer can
	 * still be integrity-scanned through this record */
	slot_buf[idx].heap = NULL;
	slot_mark_released(&pool_hdr[idx]);
	stats.skb_used--;
	stats.n_free++;
}

void
lkpi_skb_free(struct sk_buff *skb)
{
	unsigned long flags;
	int slot;

	if (skb == NULL)	/* kfree_skb(NULL) is a documented no-op */
		return;

	flags = lkpi_spl_irqsave();
	slot = slot_index_of(skb);
	if (slot >= 0) {
		if (pool_hdr[slot]._gen == LKPI_SKB_FREED_MAGIC) {
			stats.double_free++;
			lkpi_spl_log("double free of released slot %d skipped", slot);
			goto out;
		}
		if (pool_hdr[slot]._gen == 0) {
			stats.stray_free++;
			lkpi_spl_log("free of slot %d, which was never handed out, refused", slot);
			goto out;
		}
		/*
		 * The shim tracks ->list so a queued object is never freed out
		 * from under its queue: releasing it detaches it first (the
		 * header's own note on why ->list is maintained, and the only
		 * shape that leaves the queue walkable).
		 */
		if (skb->list != NULL)
			lkpi_skb_do_unlink(skb, skb->list);
		slot_release(slot);
	} else {
			int hi = heap_live_find(skb);
			uintptr_t addr = (uintptr_t)skb;	/* kept past the release */

			if (hi >= 0) {
				struct skb_heap_rec *rec = &heap_live[hi];

				buf_release(&rec->buf);
				lkpi_spl_free(skb);
				tomb_add(addr);
				heap_live_del((unsigned int)hi);
				stats.n_free++;
			} else if (tomb_find(addr)) {
			stats.double_free++;
			lkpi_spl_log("double free of a released heap header skipped");
		} else {
			stats.stray_free++;
			lkpi_spl_log("free of a pointer outside the pool refused (not passed to the allocator)");
		}
	}
out:
	lkpi_spl_irqrestore(flags);
}

/* ------------------------------------------------------------------ */
/* geometry entry points                                               */
/* ------------------------------------------------------------------ */

unsigned char *
lkpi_skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned long flags;
	unsigned char *p = NULL;

	flags = lkpi_spl_irqsave();
	if (write_target(skb) != SKB_W_OK)
		goto out;
	if (len > skb->end - skb->tail) {
		geo_bad++;
		lkpi_spl_log("put past the tail refused (len=%u tailroom=%u)",
		    len, skb->end - skb->tail);
		goto out;
	}
	p = skb->head + skb->tail;	/* the old tail, Linux semantics */
	skb->tail += len;
	skb->len += len;
out:
	lkpi_spl_irqrestore(flags);
	return p;
}

void *
lkpi_skb_push(struct sk_buff *skb, unsigned int len)
{
	unsigned long flags;
	unsigned char *p = NULL;

	flags = lkpi_spl_irqsave();
	if (write_target(skb) != SKB_W_OK)
		goto out;
	if (len > (unsigned int)(skb->data - skb->head)) {
		geo_bad++;
		lkpi_spl_log("push past the headroom refused (len=%u headroom=%u)",
		    len, (unsigned int)(skb->data - skb->head));
		goto out;
	}
	skb->data -= len;
	skb->len += len;
	p = skb->data;
out:
	lkpi_spl_irqrestore(flags);
	return p;
}

void
lkpi_skb_reserve(struct sk_buff *skb, unsigned int len)
{
	unsigned long flags;

	flags = lkpi_spl_irqsave();
	if (write_target(skb) != SKB_W_OK)
		goto out;
	if (len > skb->end - skb->tail) {
		geo_bad++;
		lkpi_spl_log("reserve past the tail refused (len=%u tailroom=%u)",
		    len, skb->end - skb->tail);
		goto out;
	}
	skb->data += len;
	skb->tail += len;
out:
	lkpi_spl_irqrestore(flags);
}

unsigned int
lkpi_skb_geo_bad_count(void)
{

	return geo_bad;
}

void
lkpi_skb_uaf_hit(struct sk_buff *skb)
{
	int slot = skb != NULL ? slot_index_of(skb) : -1;

	stats.uaf_write++;
	if (slot >= 0)
		lkpi_spl_log("put/push/reserve on released slot %d (gen 0x%08x) refused, payload untouched",
		    slot, pool_hdr[slot]._gen);
	else
		lkpi_spl_log("put/push/reserve on a released object outside the pool refused");
}

/* ------------------------------------------------------------------ */
/* queue surgery                                                       */
/* ------------------------------------------------------------------ */

void
lkpi_skb_queue_head_init_lockable(struct sk_buff_head *q)
{

	spin_lock_init(&q->lock);
	lkpi_skb_queue_init(q);
}

void
lkpi_skb_queue_init(struct sk_buff_head *q)
{

	spin_lock_init(&q->lock);
	q->next = (struct sk_buff *)q;
	q->prev = (struct sk_buff *)q;
	q->qlen = 0;
}

/* live/stack queues: reset the list shape, never touch the lock
 * (linuxkpi_core.h line 1808-1810) */
void
lkpi_skb_queue_reset(struct sk_buff_head *q)
{

	q->next = (struct sk_buff *)q;
	q->prev = (struct sk_buff *)q;
	q->qlen = 0;
}

static void
queue_add_tail(struct sk_buff_head *q, struct sk_buff *skb)
{
	struct sk_buff *at = q->prev;

	skb->next = (struct sk_buff *)q;
	skb->prev = at;
	at->next = skb;
	q->prev = skb;
	skb->list = q;
	q->qlen++;
}

static void
queue_add_head(struct sk_buff_head *q, struct sk_buff *skb)
{
	struct sk_buff *at = q->next;

	skb->next = at;
	skb->prev = (struct sk_buff *)q;
	at->prev = skb;
	q->next = skb;
	skb->list = q;
	q->qlen++;
}

/* the double-enqueue diagnostic the shim keeps ->list for: an skb that still
 * names a queue is refused instead of splicing a cycle into the list */
static int
queue_linkable(struct sk_buff_head *q, struct sk_buff *skb)
{

	if (skb == NULL) {
		lkpi_spl_log("queue op with a NULL skb refused");
		return 0;
	}
	if (skb->list != NULL) {
		lkpi_spl_log("double enqueue refused (skb still owned by a queue)");
		return 0;
	}
	(void)q;
	return 1;
}

void
lkpi_skb_do_queue_tail(struct sk_buff_head *q, struct sk_buff *skb)
{

	if (!queue_linkable(q, skb))
		return;
	queue_add_tail(q, skb);
}

void
lkpi_skb_do_queue_head(struct sk_buff_head *q, struct sk_buff *skb)
{

	if (!queue_linkable(q, skb))
		return;
	queue_add_head(q, skb);
}

struct sk_buff *
lkpi_skb_do_dequeue(struct sk_buff_head *q)
{
	struct sk_buff *skb;

	if (q->qlen == 0)
		return NULL;
	skb = q->next;
	q->next = skb->next;
	skb->next->prev = (struct sk_buff *)q;
	q->qlen--;
	skb->next = NULL;
	skb->prev = NULL;
	skb->list = NULL;
	return skb;
}

void
lkpi_skb_do_unlink(struct sk_buff *skb, struct sk_buff_head *q)
{

	if (skb == NULL || skb->list != q) {
		lkpi_spl_log("unlink of an skb this queue does not own refused");
		return;
	}
	skb->prev->next = skb->next;
	skb->next->prev = skb->prev;
	q->qlen--;
	skb->next = NULL;
	skb->prev = NULL;
	skb->list = NULL;
}

void
lkpi_skb_queue_tail(struct sk_buff_head *q, struct sk_buff *skb)
{
	unsigned long flags = lkpi_spl_irqsave();

	lkpi_skb_do_queue_tail(q, skb);
	lkpi_spl_irqrestore(flags);
}

void
lkpi_skb_queue_head(struct sk_buff_head *q, struct sk_buff *skb)
{
	unsigned long flags = lkpi_spl_irqsave();

	lkpi_skb_do_queue_head(q, skb);
	lkpi_spl_irqrestore(flags);
}

struct sk_buff *
lkpi_skb_dequeue(struct sk_buff_head *q)
{
	unsigned long flags = lkpi_spl_irqsave();
	struct sk_buff *skb;

	skb = lkpi_skb_do_dequeue(q);
	lkpi_spl_irqrestore(flags);
	return skb;
}

void
lkpi_skb_unlink(struct sk_buff *skb, struct sk_buff_head *q)
{
	unsigned long flags = lkpi_spl_irqsave();

	lkpi_skb_do_unlink(skb, q);
	lkpi_spl_irqrestore(flags);
}

void
lkpi_skb_queue_purge(struct sk_buff_head *q)
{
	struct sk_buff *skb;

	while ((skb = lkpi_skb_dequeue(q)) != NULL)
		lkpi_skb_free(skb);
}

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */
/* ------------------------------------------------------------------ */

void
lkpi_skb_pool_stats(struct lkpi_skb_pool_stats *out)
{

	*out = stats;
}

void
lkpi_skb_diag_dump(void)
{

	lkpi_spl_log("pool used=%u/%u high=%u/%u heap_live=%u oversize=%u storage_overflow header=%u data=%u alloc_fail=%u",
	    stats.skb_used, stats.data_used, stats.skb_high, stats.data_high,
	    stats.dyalloc_live, stats.dyalloc_oversize, stats.slot_overflow,
	    stats.data_overflow, stats.alloc_fail);
	lkpi_spl_log("pool n_alloc=%u n_free=%u geo_bad=%u double_free=%u stray_free=%u uaf_write=%u fence_bad=%u",
	    stats.n_alloc, stats.n_free, geo_bad, stats.double_free,
	    stats.stray_free, stats.uaf_write, stats.fence_bad);
}

void
lkpi_skb_gen_report(void)
{
	/* emergency outlet: one line, no lock assumptions */
	lkpi_spl_log("report alloc=%u free=%u fail=%u df=%u sf=%u uaf=%u fence=%u",
	    stats.n_alloc, stats.n_free, stats.alloc_fail, stats.double_free,
	    stats.stray_free, stats.uaf_write, stats.fence_bad);
}

/* first deviation of a released slot's canonical shape, or NULL when clean */
static const char *
free_slot_shape_bad(const struct sk_buff *skb, char *detail, size_t n)
{
	if (skb->next != NULL || skb->prev != NULL || skb->list != NULL) {
		snprintf(detail, n, "released slot still linked into a list");
		return detail;
	}
	if (skb->head != NULL || skb->data != NULL) {
		snprintf(detail, n, "released slot still carries a buffer pointer");
		return detail;
	}
	if (skb->end != 0 || skb->len != 0 || skb->data_len != 0 ||
	    skb->truesize != 0) {
		snprintf(detail, n, "released slot still carries geometry (end=%u len=%u)",
		    skb->end, skb->len);
		return detail;
	}
	if (skb->tail != 0) {
		snprintf(detail, n, "released slot tail=%u (want 0)", skb->tail);
		return detail;
	}
	if (skb->dev != NULL || skb->sk != NULL) {
		snprintf(detail, n, "released slot still carries owner pointers");
		return detail;
	}
	if (skb->protocol != 0 || skb->queue_mapping != 0 ||
	    skb->priority != 0 || skb->pkt_type != 0 || skb->ip_summed != 0) {
		snprintf(detail, n, "released slot still carries header fields");
		return detail;
	}
	{
		size_t i;

		for (i = 0; i < sizeof(skb->cb); i++)
			if (((const unsigned char *)skb->cb)[i] != 0) {
				snprintf(detail, n, "released slot control block byte %u nonzero",
				    (unsigned int)i);
				return detail;
			}
	}
	return NULL;
}

void
lkpi_skb_fence_scan(void)
{
	unsigned long flags = lkpi_spl_irqsave();
	unsigned int i;

	for (i = 0; i < cfg.n_header; i++) {
		struct sk_buff *skb = &pool_hdr[i];
		char detail[128];

		if (skb->_gen != LKPI_SKB_FREED_MAGIC)
			continue;
		if (free_slot_shape_bad(skb, detail, sizeof(detail)) != NULL) {
			stats.fence_bad++;
			lkpi_spl_log("fence scan: released header slot %u is corrupted (%s)",
			    i, detail);
			goto out;
		}
		if (slot_buf[i].kind == SKB_BUF_POOL && slot_buf[i].idx >= 0 &&
		    data_slot_is_free((unsigned int)slot_buf[i].idx)) {
			unsigned int di = (unsigned int)slot_buf[i].idx;
			unsigned char fb = fence_byte(di);
			unsigned int j;

			for (j = 0; j < cfg.data_sz; j++) {
				if (pool_data[di][j] != fb) {
					stats.fence_bad++;
					lkpi_spl_log("fence scan: released data slot %u is corrupted at byte %u (header slot %u)",
					    di, j, i);
					goto out;
				}
			}
		}
	}
out:
	lkpi_spl_irqrestore(flags);
}

/* ------------------------------------------------------------------ */
/* live enumeration and the state digest                               */
/* ------------------------------------------------------------------ */

unsigned int
skb_double_live_count(void)
{

	return stats.skb_used + heap_n;
}

int
skb_double_visit_live(skb_live_fn fn, void *arg)
{
	unsigned int i;
	int rc;

	for (i = 0; i < cfg.n_header; i++) {
		struct sk_buff *skb = &pool_hdr[i];
		uint32_t g = skb->_gen;

		if (g == 0 || g == LKPI_SKB_FREED_MAGIC)
			continue;
		rc = fn(skb, (int)i, slot_buf[i].kind == SKB_BUF_POOL, arg);
		if (rc != 0)
			return rc;
	}
	for (i = 0; i < heap_n; i++) {
		rc = fn(heap_live[i].p, -1,
		    heap_live[i].buf.kind == SKB_BUF_POOL, arg);
		if (rc != 0)
			return rc;
	}
	return 0;
}

int
skb_double_is_pool_slot(const struct sk_buff *skb)
{

	return slot_index_of(skb) >= 0;
}

int
skb_double_slot_index(const struct sk_buff *skb)
{

	return slot_index_of(skb);
}

int
skb_double_is_released(const struct sk_buff *skb)
{
	int slot = skb != NULL ? slot_index_of(skb) : -1;

	if (slot >= 0)
		return pool_hdr[slot]._gen == LKPI_SKB_FREED_MAGIC;
	if (skb == NULL)
		return 0;
	/*
	 * A heap address lives again as soon as the allocator hands it out, so
	 * "still released" has to mean "in the free window and not live now".
	 */
	if (heap_live_find(skb) >= 0)
		return 0;
	return tomb_find((uintptr_t)skb);
}

struct sk_buff *
skb_double_slot_at(unsigned int idx)
{

	if (idx >= SKB_DOUBLE_MAX_HDR) {
		lkpi_spl_log("slot index %u is outside the static array", idx);
		abort();
	}
	return &pool_hdr[idx];
}

unsigned char *
skb_double_slot_buffer(struct sk_buff *skb)
{
	int slot = skb != NULL ? slot_index_of(skb) : -1;

	if (slot < 0)
		return NULL;
	if (slot_buf[slot].kind == SKB_BUF_POOL && slot_buf[slot].idx >= 0)
		return pool_data[slot_buf[slot].idx];
	return NULL;
}

unsigned char *
skb_double_released_buffer(struct sk_buff *skb)
{
	int slot = skb != NULL ? slot_index_of(skb) : -1;

	if (slot < 0)
		return NULL;
	if (slot_buf[slot].kind != SKB_BUF_POOL || slot_buf[slot].idx < 0)
		return NULL;
	if (!data_slot_is_free((unsigned int)slot_buf[slot].idx))
		return NULL;
	return pool_data[slot_buf[slot].idx];
}

#define DG_OFFSET	0xcbf29ce484222325ULL
#define DG_PRIME	0x100000001b3ULL

static uint64_t
dg_bytes(uint64_t h, const void *p, size_t n)
{
	const unsigned char *b = (const unsigned char *)p;
	size_t i;

	for (i = 0; i < n; i++) {
		h ^= b[i];
		h *= DG_PRIME;
	}
	return h;
}

static uint64_t
dg_u64(uint64_t h, uint64_t v)
{
	unsigned int i;

	for (i = 0; i < 8; i++) {
		h ^= (v >> (8u * i)) & 0xffu;
		h *= DG_PRIME;
	}
	return h;
}

static uint64_t
dg_live_payload(uint64_t h, const struct sk_buff *skb)
{
	uintptr_t off;

	if (skb->head == NULL || skb->data == NULL || skb->len == 0)
		return h;
	if ((uintptr_t)skb->data < (uintptr_t)skb->head)
		return h;
	off = (uintptr_t)skb->data - (uintptr_t)skb->head;
	if (off > skb->end || skb->len > skb->end - off)
		return h;
	return dg_bytes(h, skb->data, skb->len);
}

uint64_t
skb_double_digest(void)
{
	uint64_t h = DG_OFFSET;
	unsigned int i;

	h = dg_bytes(h, &stats, sizeof(stats));
	h = dg_u64(h, gen_counter);
	h = dg_u64(h, geo_bad);
	h = dg_u64(h, data_free_n);
	for (i = 0; i < data_free_n; i++)
		h = dg_u64(h, data_free[i]);
	for (i = 0; i < cfg.n_header; i++) {
		const struct sk_buff *skb = &pool_hdr[i];

		h = dg_u64(h, skb->_gen);
		h = dg_u64(h, skb->end);
		h = dg_u64(h, skb->tail);
		h = dg_u64(h, skb->len);
		h = dg_u64(h, (uint64_t)(slot_buf[i].kind == SKB_BUF_HEAP));
		h = dg_u64(h, (uint64_t)(unsigned int)slot_buf[i].idx);
		h = dg_live_payload(h, skb);
	}
	for (i = 0; i < heap_n; i++) {
		h = dg_u64(h, heap_live[i].gen);
		h = dg_u64(h, (uint64_t)(heap_live[i].buf.kind == SKB_BUF_POOL));
		h = dg_u64(h, (uint64_t)(unsigned int)heap_live[i].buf.idx);
		h = dg_live_payload(h, heap_live[i].p);
	}
	/* released heap pointers are addresses; only the count is state */
	h = dg_u64(h, tomb_filled);
	h = dg_u64(h, tomb_next);
	return h;
}
