/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    skbuff_pool.c
 * @brief   The skbuff pool body: the dual static pool (header slots plus linear data
 *          buffers) behind the skbuff_pool.h face, its geometry, its named defect
 *          counters and the platform hooks that face declares.
 *
 * @design static dual pool, hand-out by a state scan - the pool geometry, the
 *         refcounted linear buffers and the oversize fallback follow the fact source
 *         (components/skbuff/freertos/freertos_skbuff.c:10-137 builds the fixed arrays
 *         and the per-slot reference count, :143-263 is allocation and release, and
 *         :654-690 is the clone that shares a payload). The object lifetime
 *         bookkeeping (the release marker on the header, the tombstone ring for
 *         heap-backed headers, the named counters) is the contract this project froze
 *         in the harness double it ships
 *         (tests/host/skbuff-allocation-free-stress/skb_contract.c:271-302 and
 *         616-770). The two references meet at one deliberate convergence, recorded
 *         with its reason in skbuff/DIVERGENCES.md: the source's free lists are
 *         dropped, and hand-out scans the slot array for the lowest-index free slot,
 *         because the release marker (`_gen`) and the reference count (0 = free)
 *         already name every free slot - a second linkage could only disagree with
 *         them - and the lowest-index policy is the one the contract cells froze.
 *         Critical sections are the platform hooks the face declares
 *         (skbuff_pool.h:124-133); on the target they are the port's interrupt-mask
 *         sections (os/cmsis2/cmsis2_ext.h:38-41), the same sections the linuxkpi
 *         spinlocks take (linuxkpi_core.c:386-409). The public queue operations take
 *         the queue's own lock, as the face requires (linuxkpi_core.h:1811-1817); the
 *         raw `__` forms are the caller-locks contract and take nothing.
 * @date    08.10.2026
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "linuxkpi_core.h"
#include <linux/skbuff.h>

/* the target's critical-section and heap entry points (the facade); a host build
 * defines LKPI_SKB_POOL_NO_TARGET_HOOKS and supplies the hooks itself */
#ifndef LKPI_SKB_POOL_NO_TARGET_HOOKS
#include "os/cmsis2/cmsis2.h"
#endif

/* ------------------------------------------------------------------ */
/* geometry: the compiled macros are upper bounds                      */
/* ------------------------------------------------------------------ */

#define LKPI_SKB_SLOT_MAX	CONFIG_LKPI_SKB_POOL_NUM
#define LKPI_SKB_DATA_MAX	CONFIG_LKPI_SKB_DATA_POOL_NUM
#define LKPI_SKB_DATA_MAXSZ	CONFIG_LKPI_SKB_DATA_BUF_SZ

_Static_assert(LKPI_SKB_SLOT_MAX > 0, "the header pool needs at least one slot");
_Static_assert(LKPI_SKB_DATA_MAX > 0, "the data pool needs at least one slot");
_Static_assert(LKPI_SKB_DATA_MAXSZ > 0, "a data slot needs a payload size");

/* Heap-backed headers in flight at once. A diagnostic registry, not a budget: an
 * allocation beyond it is refused with alloc_fail rather than left untracked. */
#ifndef CONFIG_LKPI_SKB_HEAP_HDR_MAX
#define CONFIG_LKPI_SKB_HEAP_HDR_MAX	32
#endif
/* Released heap-backed headers remembered. A ring, newest first: an entry that
 * falls out degrades a later double free to a named stray refusal - the pointer
 * is refused either way, never handed to the allocator. */
#ifndef CONFIG_LKPI_SKB_TOMB_MAX
#define CONFIG_LKPI_SKB_TOMB_MAX	64
#endif

/* ------------------------------------------------------------------ */
/* storage                                                             */
/* ------------------------------------------------------------------ */

/*
 * The trailing shared record every buffer carries (linux/skbuff.h, skb_shinfo):
 * it sits at an 8-byte boundary at or just past head + `end`, inside the storage,
 * so a buffer must be able to hold the payload the request can reach plus the
 * alignment gap (up to seven bytes) plus the record.  The record moves with the
 * request (its accessor derives the address), not with the slot, which is why
 * the reserve is appended to the payload array rather than addressed at a fixed
 * offset.
 */
#define LKPI_SKB_SHINFO_MAXSZ \
	(((unsigned int)sizeof(struct skb_shared_info) + 7u + 63u) & ~63u)

/*
 * A data slot: the reference count (0 = free, 1 = owned, one more per clone
 * sharing the payload) and the payload itself, cache-line aligned. The previous
 * bring-up lost bulk transfers by handing the DMA path a buffer whose alignment
 * was unknowable; a pool slot's alignment is a fact a consumer may rely on, and
 * the member attribute makes every slot's payload 64-byte aligned because the
 * whole struct is. The payload is a fixed array rather than a flexible one: an
 * element of the array must carry its own payload for the stride to be right,
 * and the compiled size is the bound the runtime geometry then stays under.
 */
struct lkpi_skb_data_slot {
	uint32_t	ref;
	uint32_t	_pad;
	unsigned char	buf[CONFIG_LKPI_SKB_DATA_BUF_SZ + LKPI_SKB_SHINFO_MAXSZ]
	    __attribute__((aligned(64)));
};

/* The stride must carry header, payload and shared record: if the payload were
 * declared as a flexible member, the array element would be header-sized and every
 * slot's payload would land on its neighbour's header. */
_Static_assert(offsetof(struct lkpi_skb_data_slot, buf) % 64u == 0u,
    "a data slot's payload must be cache-line aligned");
_Static_assert(sizeof(struct lkpi_skb_data_slot) >=
    offsetof(struct lkpi_skb_data_slot, buf) + LKPI_SKB_DATA_MAXSZ +
    LKPI_SKB_SHINFO_MAXSZ,
    "a data slot's stride must contain its payload and the shared record");

/* where a live object's linear buffer came from; the record survives the
 * header's release so the fence scan can still reach the buffer */
#define LKPI_SKB_BUF_NONE	0u
#define LKPI_SKB_BUF_POOL	1u
#define LKPI_SKB_BUF_HEAP	2u

struct lkpi_skb_buf_rec {
	uint8_t		kind;
	uint8_t		pad;
	uint16_t	idx;		/* pool data slot index */
	void		*heap;		/* payload pointer, live only */
};

struct lkpi_skb_heap_rec {
	struct sk_buff		*p;
	struct lkpi_skb_buf_rec	buf;
};

static struct sk_buff		skb_slots[LKPI_SKB_SLOT_MAX];
static struct lkpi_skb_data_slot skb_data[LKPI_SKB_DATA_MAX];
static struct lkpi_skb_buf_rec	slot_buf[LKPI_SKB_SLOT_MAX];
static struct lkpi_skb_heap_rec	heap_live[CONFIG_LKPI_SKB_HEAP_HDR_MAX];
static uint32_t			heap_live_n;
static uintptr_t		tomb[CONFIG_LKPI_SKB_TOMB_MAX];
static uint32_t			tomb_head;
static uint32_t			tomb_filled;

static struct lkpi_skb_pool_stats stats;
static unsigned int		geo_bad;
static uint32_t			gen_counter;

/* the geometry in force: the compiled macros until lkpi_skb_pool_configure()
 * selects another one within them (the host contract harness picks a scenario
 * geometry per run; a board build leaves the macros) */
static uint32_t			cfg_n_header;
static uint32_t			cfg_n_data;
static uint32_t			cfg_data_sz;
static int			cfg_heap_fallback;
static int			pool_ready;

/*
 * A build may pin the pool's whole storage; the arithmetic is the check, so the
 * budget and the arrays cannot drift apart.
 */
#ifdef CONFIG_LKPI_SKB_POOL_BUDGET_BYTES
_Static_assert(sizeof(skb_slots) + sizeof(skb_data) + sizeof(slot_buf) +
    sizeof(heap_live) + sizeof(tomb) + sizeof(stats) <=
    (size_t)CONFIG_LKPI_SKB_POOL_BUDGET_BYTES,
    "the pool storage exceeds CONFIG_LKPI_SKB_POOL_BUDGET_BYTES");
#endif

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

/* the fence byte of a free data slot: per-slot, so a write through a stale
 * pointer into the wrong slot cannot pass as intact, and never zero (the
 * corruption probes write a zero byte and rely on it being foreign) */
static unsigned char fence_byte(unsigned int idx)
{
	return (unsigned char)(0xa5u ^ (0x11u * (idx + 1u)));
}

static uint32_t gen_next(void)
{
	gen_counter++;
	if (gen_counter == 0 || gen_counter >= LKPI_SKB_FREED_MAGIC)
		gen_counter = 1;
	return gen_counter;
}

/*
 * Index of a static header slot, by address arithmetic only - never dereferences
 * the pointer, so a stale heap pointer or an interior pointer is refused without
 * touching memory. Returns -1 unless the pointer names a slot of the compiled
 * array; a slot at or beyond the configured pool is the caller's business.
 */
static int pool_slot_index(const struct sk_buff *skb)
{
	uintptr_t base = (uintptr_t)&skb_slots[0];
	uintptr_t addr = (uintptr_t)skb;
	uintptr_t stride = (uintptr_t)sizeof(skb_slots[0]);

	if (addr < base || addr >= base + stride * (uintptr_t)LKPI_SKB_SLOT_MAX)
		return -1;
	if ((addr - base) % stride != 0)
		return -1;
	return (int)((addr - base) / stride);
}

static int heap_live_find(const struct sk_buff *skb)
{
	uint32_t i;

	for (i = 0; i < heap_live_n; i++)
		if (heap_live[i].p == skb)
			return (int)i;
	return -1;
}

/* keep the registry in allocation order (the order the harness's live
 * enumeration documents), so removal shifts the tail down */
static void heap_live_del(unsigned int idx)
{
	uint32_t i;

	for (i = idx; i + 1u < heap_live_n; i++)
		heap_live[i] = heap_live[i + 1u];
	heap_live_n--;
}

static int tomb_find(uintptr_t p)
{
	uint32_t i;

	for (i = 0; i < tomb_filled; i++)
		if (tomb[i] == p)
			return 1;
	return 0;
}

static void tomb_add(uintptr_t p)
{
	tomb[tomb_head] = p;
	tomb_head = (tomb_head + 1u) % (uint32_t)CONFIG_LKPI_SKB_TOMB_MAX;
	if (tomb_filled < (uint32_t)CONFIG_LKPI_SKB_TOMB_MAX)
		tomb_filled++;
}

/*
 * The structural invariant of a geometry-op target (skbuff_pool.h:14: offset-style
 * tail/end; the geometry entry points apply to the linear area, and a buffer whose
 * frame continues into fragments is refused below by its nonzero data_len): the
 * head, data and tail pointers are ordered and inside the slot.
 *
 * The payload length is deliberately NOT part of it. The interface maintains no
 * such relation - its own operations only need tailroom and pointer order - and the
 * vendored receive path depends on that freedom: rtw_os_alloc_recvframe() assigns
 * pkt_copy->len directly and then aligns the buffer with two skb_reserve() calls
 * (os_dep/linux/recv_linux.c:92-98), which a "tail covers headroom plus payload"
 * check refuses, leaving the frame buffer unaligned and every frame it carries
 * mis-framed.
 */
static int geom_ok(const struct sk_buff *skb)
{
	uintptr_t off;

	if (skb->head == NULL || skb->data == NULL)
		return 0;
	if ((uintptr_t)skb->data < (uintptr_t)skb->head)
		return 0;
	off = (uintptr_t)skb->data - (uintptr_t)skb->head;
	if (off > skb->tail || skb->tail > skb->end)
		return 0;
	if (skb->data_len != 0)
		return 0;
	return 1;
}

/*
 * Validate a geometry-op target. Returns non-zero for a live, geometrically sane
 * object; the failure outcomes are already counted, and no field of a refused
 * object is touched.
 */
static int write_target(struct sk_buff *skb)
{
	int slot;

	if (skb == NULL) {
		geo_bad++;
		return 0;
	}
	slot = pool_slot_index(skb);
	if (slot >= 0 && slot < (int)cfg_n_header) {
		if (skb_slots[slot]._gen == LKPI_SKB_FREED_MAGIC) {
			lkpi_skb_uaf_hit(skb);
			return 0;
		}
		if (skb_slots[slot]._gen == 0) {
			geo_bad++;
			lkpi_spl_log("geometry op on slot %d, which was never handed out, refused", slot);
			return 0;
		}
	} else if (heap_live_find(skb) < 0) {
		geo_bad++;
		lkpi_spl_log("geometry op on a pointer outside the pool refused");
		return 0;
	}
	if (!geom_ok(skb)) {
		geo_bad++;
		lkpi_spl_log("geometry op on an object whose pre-state breaks the linear invariant refused");
		return 0;
	}
	return 1;
}

/* the buffer record of a live object (static slot or heap-backed header) */
static int object_buf_rec(const struct sk_buff *skb, struct lkpi_skb_buf_rec *out)
{
	int slot = pool_slot_index(skb);
	int hi;

	if (slot >= 0 && slot < (int)cfg_n_header &&
	    skb_slots[slot]._gen != 0 && skb_slots[slot]._gen != LKPI_SKB_FREED_MAGIC) {
		*out = slot_buf[slot];
		return 1;
	}
	hi = heap_live_find(skb);
	if (hi >= 0) {
		*out = heap_live[hi].buf;
		return 1;
	}
	return 0;
}

static void buf_ref_inc(const struct lkpi_skb_buf_rec *rec)
{
	if (rec->kind == LKPI_SKB_BUF_POOL) {
		skb_data[rec->idx].ref++;
	} else if (rec->kind == LKPI_SKB_BUF_HEAP) {
		uint32_t *ref = (uint32_t *)(void *)((unsigned char *)rec->heap - 8);

		(*ref)++;
	}
}

/* the page fragments a dying payload owns: each fragment's page reference goes
 * back exactly once, here, where the payload itself is released - the interface's
 * own rule (a clone that shares the payload must not release them again) */
static void frag_pages_release(struct sk_buff *skb)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	unsigned int i;

	if (sh->nr_frags == 0)
		return;
	for (i = 0; i < (unsigned int)sh->nr_frags && i < (unsigned int)MAX_SKB_FRAGS;
	    i++)
		lkpi_spl_page_put(sh->frags[i].bv_page);
	sh->nr_frags = 0;
}

/* one reference to a linear buffer goes away: the last one returns the slot to
 * the pool (fence-filled) or frees the heap buffer */
static void buf_ref_put(const struct lkpi_skb_buf_rec *rec, struct sk_buff *skb)
{
	if (rec->kind == LKPI_SKB_BUF_POOL) {
		struct lkpi_skb_data_slot *ds = &skb_data[rec->idx];

		if (ds->ref > 0 && --ds->ref > 0)
			return;		/* a clone still owns the payload */
		frag_pages_release(skb);
		memset(ds->buf, fence_byte(rec->idx), cfg_data_sz);
		stats.data_used--;
	} else if (rec->kind == LKPI_SKB_BUF_HEAP) {
		uint32_t *ref = (uint32_t *)(void *)((unsigned char *)rec->heap - 8);

		if (*ref > 0 && --(*ref) > 0)
			return;
		frag_pages_release(skb);
		stats.dyalloc_live--;
		lkpi_spl_free((unsigned char *)rec->heap - 8);
	}
}

/* canonical shape of a released header slot: every field zeroed, only the
 * generation carries the release magic. Anything else is slot corruption. */
static void slot_mark_released(struct sk_buff *skb)
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

/* first deviation of a released slot's canonical shape, or NULL when clean */
static const char *free_slot_shape_bad(const struct sk_buff *skb, char *detail,
    size_t n)
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
				snprintf(detail, n,
				    "released slot control block byte %u nonzero",
				    (unsigned int)i);
				return detail;
			}
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* geometry and reset                                                  */
/* ------------------------------------------------------------------ */

int lkpi_skb_pool_configure(unsigned int n_header, unsigned int n_data,
    unsigned int data_sz, int heap_fallback)
{
	unsigned int i;

	if (n_header == 0 || n_header > LKPI_SKB_SLOT_MAX ||
	    n_data == 0 || n_data > LKPI_SKB_DATA_MAX ||
	    data_sz == 0 || data_sz > LKPI_SKB_DATA_MAXSZ)
		return -1;
	for (i = 0; i < n_data; i++)
		if (fence_byte(i) == 0)
			return -1;

	cfg_n_header = n_header;
	cfg_n_data = n_data;
	cfg_data_sz = data_sz;
	cfg_heap_fallback = heap_fallback != 0;

	memset(skb_slots, 0, sizeof(skb_slots));
	memset(skb_data, 0, sizeof(skb_data));
	memset(slot_buf, 0, sizeof(slot_buf));
	memset(heap_live, 0, sizeof(heap_live));
	memset(tomb, 0, sizeof(tomb));
	memset(&stats, 0, sizeof(stats));
	heap_live_n = 0;
	tomb_head = 0;
	tomb_filled = 0;
	gen_counter = 0;
	geo_bad = 0;
	pool_ready = 1;
	return 0;
}

void lkpi_skb_pool_init(void)
{
	if (pool_ready)
		return;
	/* the compiled geometry, with the allocator's documented fallback */
	(void)lkpi_skb_pool_configure(LKPI_SKB_SLOT_MAX, LKPI_SKB_DATA_MAX,
	    LKPI_SKB_DATA_MAXSZ, 1);
}

/* ------------------------------------------------------------------ */
/* allocation                                                          */
/* ------------------------------------------------------------------ */

static struct sk_buff *pool_alloc(unsigned int size, int dev)
{
	const unsigned int need = size + (dev ? (unsigned int)NET_SKB_PAD : 0u);
	struct lkpi_skb_buf_rec rec = { LKPI_SKB_BUF_NONE, 0, 0, NULL };
	struct sk_buff *skb = NULL;
	unsigned char *buf = NULL;
	int hdr_heap = 0;
	int slot = -1;
	int i;
	unsigned long flags;

	lkpi_skb_pool_init();
	flags = lkpi_spl_irqsave();

	/*
	 * dyalloc_oversize is attributed to the request ("requests > DATA_BUF_SZ",
	 * skbuff_pool.h:89) and is therefore taken before the supply is probed;
	 * slot_overflow/data_overflow belong to the served heap path ("<pool>
	 * exhausted -> heap", lines 90-91) and are taken only when the heap
	 * actually serves the request.
	 */
	if (need > cfg_data_sz)
		stats.dyalloc_oversize++;

	/* header: the lowest-index free slot, else the heap */
	for (i = 0; i < (int)cfg_n_header; i++)
		if (skb_slots[i]._gen == 0 ||
		    skb_slots[i]._gen == LKPI_SKB_FREED_MAGIC) {
			slot = i;
			break;
		}
	if (slot >= 0) {
		skb = &skb_slots[slot];
	} else if (cfg_heap_fallback) {
		if (heap_live_n >= (uint32_t)CONFIG_LKPI_SKB_HEAP_HDR_MAX) {
			stats.alloc_fail++;
			lkpi_spl_log("alloc refused: header slots full (%u) and "
			    "heap headers full (%u), size=%u", cfg_n_header,
			    (unsigned)CONFIG_LKPI_SKB_HEAP_HDR_MAX, size);
			goto out;
		}
		skb = (struct sk_buff *)lkpi_spl_malloc((unsigned int)sizeof(*skb));
		if (skb == NULL) {
			stats.alloc_fail++;
			lkpi_spl_log("alloc refused: no heap for a header, size=%u",
			    size);
			goto out;
		}
		hdr_heap = 1;
	} else {
		stats.alloc_fail++;
		lkpi_spl_log("alloc refused: no free header slot and no heap "
		    "fallback, size=%u", size);
		goto out;
	}

	/* linear buffer: the lowest-index free slot, else the heap */
	if (need <= cfg_data_sz) {
		for (i = 0; i < (int)cfg_n_data; i++)
			if (skb_data[i].ref == 0) {
				skb_data[i].ref = 1;
				rec.kind = LKPI_SKB_BUF_POOL;
				rec.idx = (uint16_t)i;
				buf = skb_data[i].buf;
				stats.data_used++;
				if (stats.data_used > stats.data_high)
					stats.data_high = stats.data_used;
				break;
			}
	}
	if (buf == NULL) {
		uint32_t *ref;

		if (!cfg_heap_fallback) {
			stats.alloc_fail++;
			goto fail_header;
		}
		/* one reference word in front of the payload, so a clone can share
		 * a heap-backed buffer exactly like a pool slot; the trailing
		 * shared record rides the same allocation */
		ref = (uint32_t *)lkpi_spl_malloc(8u + need + LKPI_SKB_SHINFO_MAXSZ);
		if (ref == NULL) {
			stats.alloc_fail++;
			lkpi_spl_log("alloc refused: no heap for %u bytes "
			    "(data slot is %u)", 8u + need + LKPI_SKB_SHINFO_MAXSZ,
			    cfg_data_sz);
			goto fail_header;
		}
		*ref = 1;
		buf = (unsigned char *)ref + 8;
		rec.kind = LKPI_SKB_BUF_HEAP;
		rec.heap = buf;
		if (need <= cfg_data_sz)
			stats.data_overflow++;
		stats.dyalloc_live++;
	}

	memset(skb, 0, sizeof(*skb));
	skb->head = buf;
	skb->data = buf;
	skb->tail = 0;
	/*
	 * `end` is the request's - the interface's own geometry (alloc_skb(64)
	 * followed by skb_reserve(NET_SKB_PAD) leaves 64 bytes of tailroom), and
	 * the two host suites record exactly that. A frame larger than the request
	 * therefore takes the vendored receive path's fragment branch again
	 * (third-party/iwlwifi/mvm/rx.c:80: `len <= skb_tailroom(skb) ? len : ...`)
	 * - and now has somewhere to go: the buffer's own shared record carries the
	 * page fragments (skb_add_rx_frag below), and the substitute's receive
	 * entry linearizes the frame before the stack sees it. `end` no longer has
	 * to stand in for fragment storage it does not have.
	 */
	skb->end = need;
	skb->len = 0;
	skb->data_len = 0;
	skb->truesize = (unsigned int)sizeof(*skb) + need;
	skb->_gen = gen_next();
	/*
	 * The buffer's trailing shared record, zeroed at every allocation: the
	 * bytes it occupies were payload for whoever held this buffer last (a
	 * slot) or are uninitialised (a fresh heap block), and a stale nr_frags
	 * or frag_list would be read by the first driver that asks.  Its address
	 * is the accessor's (an 8-byte boundary at or past the payload), never
	 * recomputed here.
	 */
	memset(skb_shinfo(skb), 0, sizeof(struct skb_shared_info));

	if (hdr_heap) {
		heap_live[heap_live_n].p = skb;
		heap_live[heap_live_n].buf = rec;
		heap_live_n++;
		stats.slot_overflow++;
	} else {
		slot_buf[slot] = rec;
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
	if (hdr_heap)
		lkpi_spl_free(skb);
	skb = NULL;
out:
	lkpi_spl_irqrestore(flags);
	return skb;
}

struct sk_buff *lkpi_skb_alloc(unsigned int size, unsigned int gfp)
{
	(void)gfp;		/* the pool never waits for a slot */
	return pool_alloc(size, 0);
}

struct sk_buff *lkpi_dev_alloc_skb(unsigned int size, unsigned int gfp)
{
	(void)gfp;
	return pool_alloc(size, 1);
}

struct sk_buff *__netdev_alloc_skb(struct net_device *dev, unsigned int length,
    gfp_t gfp_mask)
{
	(void)dev;		/* nothing in this project charges a net device */
	(void)gfp_mask;
	return pool_alloc(length, 1);
}

/*
 * Clone: a second descriptor over the same payload. The payload reference is
 * shared (the fact source's clone contract), so the buffer returns to the pool
 * when the last descriptor holding it is released; the control block is the
 * clone's own (the caller fills it for its own frame).
 */
struct sk_buff *skb_clone(const struct sk_buff *skb, gfp_t gfp_mask)
{
	struct lkpi_skb_buf_rec src = { LKPI_SKB_BUF_NONE, 0, 0, NULL };
	struct sk_buff *clone = NULL;
	int hdr_heap = 0;
	int slot = -1;
	int i;
	unsigned long flags;

	(void)gfp_mask;
	if (skb == NULL || !object_buf_rec(skb, &src)) {
		lkpi_spl_log("clone of a pointer the pool does not own refused");
		return NULL;
	}

	lkpi_skb_pool_init();
	flags = lkpi_spl_irqsave();
	for (i = 0; i < (int)cfg_n_header; i++)
		if (skb_slots[i]._gen == 0 ||
		    skb_slots[i]._gen == LKPI_SKB_FREED_MAGIC) {
			slot = i;
			break;
		}
	if (slot >= 0) {
		clone = &skb_slots[slot];
	} else if (cfg_heap_fallback &&
	    heap_live_n < (uint32_t)CONFIG_LKPI_SKB_HEAP_HDR_MAX) {
		clone = (struct sk_buff *)lkpi_spl_malloc((unsigned int)sizeof(*clone));
		if (clone == NULL) {
			stats.alloc_fail++;
			goto out;
		}
		hdr_heap = 1;
	} else {
		stats.alloc_fail++;
		goto out;
	}

	buf_ref_inc(&src);
	memset(clone, 0, sizeof(*clone));
	clone->head = skb->head;
	clone->data = skb->data;
	clone->tail = skb->tail;
	clone->end = skb->end;
	clone->len = skb->len;
	clone->data_len = skb->data_len;
	clone->truesize = skb->truesize;
	clone->dev = skb->dev;
	clone->protocol = skb->protocol;
	clone->queue_mapping = skb->queue_mapping;
	clone->_gen = gen_next();

	if (hdr_heap) {
		heap_live[heap_live_n].p = clone;
		heap_live[heap_live_n].buf = src;
		heap_live_n++;
		stats.slot_overflow++;
	} else {
		slot_buf[slot] = src;
		stats.skb_used++;
		if (stats.skb_used > stats.skb_high)
			stats.skb_high = stats.skb_used;
	}
	stats.n_alloc++;
out:
	lkpi_spl_irqrestore(flags);
	return clone;
}

/*
 * Copy a run of bytes out of a buffer. [LNX] include/linux/skbuff.h:4286: the
 * interface's body copies from the linear area and then walks the fragment list,
 * answering -EFAULT when the range does not fit. This port's fragments are the
 * buffer's own record entries (linux/skbuff.h, skb_shinfo), and the walk below is
 * the interface's: the linear part first, then the fragments in order. The
 * vendored transmit path reads a management frame's bytes through this
 * (third-party/rtl8189fs/os_dep/linux/xmit_linux.c:46).
 */
int skb_copy_bits(const struct sk_buff *skb, int offset, void *to, int len)
{
	const struct skb_shared_info *sh;
	unsigned int headlen, off;
	unsigned int left;
	unsigned char *dst = to;
	unsigned int i;

	if (skb == NULL || to == NULL || len < 0)
		return -EFAULT;
	if (offset < 0 ||
	    (unsigned int)offset + (unsigned int)len > skb->len)
		return -EFAULT;

	off = (unsigned int)offset;
	left = (unsigned int)len;
	headlen = skb->len - skb->data_len;
	if (off < headlen) {
		unsigned int n = headlen - off;

		if (n > left)
			n = left;
		memcpy(dst, skb->data + off, n);
		dst += n;
		off += n;
		left -= n;
	}
	sh = skb_shinfo(skb);
	for (i = 0; left > 0 && i < (unsigned int)sh->nr_frags; i++) {
		const skb_frag_t *frag = &sh->frags[i];
		unsigned int flen = frag->bv_len;
		unsigned int from, n;

		if (off >= headlen + flen) {
			headlen += flen;
			continue;
		}
		from = off > headlen ? off - headlen : 0;
		n = flen - from;
		if (n > left)
			n = left;
		memcpy(dst, (const unsigned char *)page_address(frag->bv_page) +
		    frag->bv_offset + from, n);
		dst += n;
		off += n;
		left -= n;
		headlen += flen;
	}
	return left == 0 ? 0 : -EFAULT;
}

/*
 * [LNX] include/linux/skbuff.h:2702-2708: the interface attaches a page as
 * fragment `i` of the buffer's shared record, a reference the record now owns
 * (the page arrives with the reference its previous holder carried - the
 * vendored receive path's steal, iwl-trans.h:252), and adds the fragment's
 * length to the buffer's accounting. This port's record is the buffer's own
 * storage (linux/skbuff.h, skb_shinfo), and the fragment stays a fragment: the
 * substitute's common receive entry linearizes the whole frame before the
 * net80211 side - which speaks linear frames - sees it (linuxkpi_80211.c,
 * ieee80211_rx_irqsafe); the fragment's page is released when the payload dies
 * (frag_pages_release above). The interface's truesize addition is not applied:
 * this port's truesize is the pool's own, fixed at allocation.
 */
void skb_add_rx_frag(struct sk_buff *skb, int i, struct page *page, int off,
		     int size, unsigned int truesize)
{
	struct skb_shared_info *sh;

	(void)truesize;
	if (skb == NULL || page == NULL || size <= 0)
		return;
	if (i < 0 || i >= (int)MAX_SKB_FRAGS) {
		lkpi_spl_log("rx fragment index %d out of range (cap %u); fragment dropped",
		    i, (unsigned)MAX_SKB_FRAGS);
		return;
	}
	sh = skb_shinfo(skb);
	sh->frags[i].bv_page = page;
	sh->frags[i].bv_offset = (unsigned int)off;
	sh->frags[i].bv_len = (unsigned int)size;
	if ((unsigned int)sh->nr_frags < (unsigned int)i + 1u)
		sh->nr_frags = (unsigned char)(i + 1);
	skb->data_len += (unsigned int)size;
	skb->len += (unsigned int)size;
}

/* ------------------------------------------------------------------ */
/* release                                                             */
/* ------------------------------------------------------------------ */

void lkpi_skb_free(struct sk_buff *skb)
{
	unsigned long flags;
	int slot;
	int hi;

	if (skb == NULL)	/* kfree_skb(NULL) is a documented no-op */
		return;
	lkpi_skb_pool_init();
	flags = lkpi_spl_irqsave();

	slot = pool_slot_index(skb);
	if (slot >= 0 && slot < (int)cfg_n_header) {
		if (skb->_gen == LKPI_SKB_FREED_MAGIC) {
			stats.double_free++;
			lkpi_spl_log("double free of released slot %d skipped", slot);
			goto out;
		}
		if (skb->_gen == 0) {
			stats.stray_free++;
			lkpi_spl_log("free of slot %d, which was never handed out, refused", slot);
			goto out;
		}
		/*
		 * A queued object is never freed out from under its queue: releasing
		 * it detaches it first (the face tracks ->list for exactly this and
		 * for the double-enqueue diagnostic).
		 */
		if (skb->list != NULL)
			lkpi_skb_do_unlink(skb, skb->list);
		buf_ref_put(&slot_buf[slot], skb);
		/* The buffer record stays: a released slot without its head pointer
		 * can still be integrity-scanned through this record (the fence scan
		 * checks the data slot is still free before reading it). Only a heap
		 * pointer is cleared - it no longer names anything. */
		if (slot_buf[slot].kind == LKPI_SKB_BUF_HEAP)
			slot_buf[slot].heap = NULL;
		slot_mark_released(skb);
		stats.skb_used--;
		stats.n_free++;
		goto out;
	}

	hi = heap_live_find(skb);
	if (hi >= 0) {
		uintptr_t addr = (uintptr_t)skb;	/* kept past the release */

		if (skb->list != NULL)
			lkpi_skb_do_unlink(skb, skb->list);
		buf_ref_put(&heap_live[hi].buf, skb);
		lkpi_spl_free(skb);
		tomb_add(addr);
		heap_live_del((unsigned int)hi);
		stats.n_free++;
	} else if (tomb_find((uintptr_t)skb)) {
		stats.double_free++;
		lkpi_spl_log("double free of a released heap-backed header skipped");
	} else {
		stats.stray_free++;
		lkpi_spl_log("free of a pointer outside the pool refused (not passed to the allocator)");
	}
out:
	lkpi_spl_irqrestore(flags);
}

/* ------------------------------------------------------------------ */
/* geometry entry points                                               */
/* ------------------------------------------------------------------ */

unsigned char *lkpi_skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned long flags = lkpi_spl_irqsave();
	unsigned char *p = NULL;

	if (!write_target(skb))
		goto out;
	if (len > skb->end - skb->tail) {
		geo_bad++;
		/* the geometry and the caller: a refusal is a contract mismatch
		 * someone has to name, and the put site is not in this file */
		lkpi_spl_log("put past the tail refused (len=%u tailroom=%u "
		    "head=%p end=%u tail=%u ra=%p)", len, skb->end - skb->tail,
		    (void *)skb->head, skb->end, skb->tail,
		    __builtin_return_address(0));
		goto out;
	}
	p = skb->head + skb->tail;	/* the old tail, Linux semantics */
	skb->tail += len;
	skb->len += len;
out:
	lkpi_spl_irqrestore(flags);
	return p;
}

void *lkpi_skb_push(struct sk_buff *skb, unsigned int len)
{
	unsigned long flags = lkpi_spl_irqsave();
	void *p = NULL;

	if (!write_target(skb))
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

void lkpi_skb_reserve(struct sk_buff *skb, unsigned int len)
{
	unsigned long flags = lkpi_spl_irqsave();

	if (!write_target(skb))
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

unsigned int lkpi_skb_geo_bad_count(void)
{
	return geo_bad;
}

void lkpi_skb_uaf_hit(struct sk_buff *skb)
{
	int slot = skb != NULL ? pool_slot_index(skb) : -1;

	stats.uaf_write++;
	if (slot >= 0)
		lkpi_spl_log("put/push/reserve on released slot %d (gen 0x%08x) refused, payload untouched",
		    slot, skb_slots[slot]._gen);
	else
		lkpi_spl_log("put/push/reserve on a released object outside the pool refused");
}

/* ------------------------------------------------------------------ */
/* queue surgery                                                       */
/* ------------------------------------------------------------------ */

void lkpi_skb_queue_reset(struct sk_buff_head *q)
{
	q->next = (struct sk_buff *)q;
	q->prev = (struct sk_buff *)q;
	q->qlen = 0;
	/* the lock is NOT touched: __skb_queue_head_init runs on live queues and
	 * on stack-local temps that have no lock storage to own */
}

void lkpi_skb_queue_init(struct sk_buff_head *q)
{
	spin_lock_init(&q->lock);
	lkpi_skb_queue_reset(q);
}

void lkpi_skb_queue_head_init_lockable(struct sk_buff_head *q)
{
	spin_lock_init(&q->lock);
	lkpi_skb_queue_reset(q);
}

/* the double-enqueue diagnostic the face keeps ->list for: an skb that still
 * names a queue is refused instead of splicing a cycle into the list */
static int queue_linkable(const struct sk_buff *skb)
{
	if (skb == NULL) {
		lkpi_spl_log("queue op with a NULL skb refused");
		return 0;
	}
	if (skb->list != NULL) {
		lkpi_spl_log("double enqueue refused (skb still owned by a queue)");
		return 0;
	}
	return 1;
}

void lkpi_skb_do_queue_tail(struct sk_buff_head *q, struct sk_buff *skb)
{
	struct sk_buff *at;

	if (!queue_linkable(skb))
		return;
	at = q->prev;
	skb->next = (struct sk_buff *)q;
	skb->prev = at;
	at->next = skb;
	q->prev = skb;
	skb->list = q;
	q->qlen++;
}

void lkpi_skb_do_queue_head(struct sk_buff_head *q, struct sk_buff *skb)
{
	struct sk_buff *at;

	if (!queue_linkable(skb))
		return;
	at = q->next;
	skb->next = at;
	skb->prev = (struct sk_buff *)q;
	at->prev = skb;
	q->next = skb;
	skb->list = q;
	q->qlen++;
}

static void do_detach(struct sk_buff_head *q, struct sk_buff *skb)
{
	skb->prev->next = skb->next;
	skb->next->prev = skb->prev;
	q->qlen--;
	skb->next = NULL;
	skb->prev = NULL;
	skb->list = NULL;
}

struct sk_buff *lkpi_skb_do_dequeue(struct sk_buff_head *q)
{
	struct sk_buff *skb;

	if (q->qlen == 0)
		return NULL;
	skb = q->next;
	do_detach(q, skb);
	return skb;
}

void lkpi_skb_do_unlink(struct sk_buff *skb, struct sk_buff_head *q)
{
	if (skb == NULL || skb->list != q) {
		lkpi_spl_log("unlink of an skb this queue does not own refused");
		return;
	}
	do_detach(q, skb);
}

void lkpi_skb_queue_tail(struct sk_buff_head *q, struct sk_buff *skb)
{
	unsigned long long flags = lkpi_spin_lock_irqsave(&q->lock);

	lkpi_skb_do_queue_tail(q, skb);
	lkpi_spin_unlock_irqrestore(&q->lock, flags);
}

void lkpi_skb_queue_head(struct sk_buff_head *q, struct sk_buff *skb)
{
	unsigned long long flags = lkpi_spin_lock_irqsave(&q->lock);

	lkpi_skb_do_queue_head(q, skb);
	lkpi_spin_unlock_irqrestore(&q->lock, flags);
}

struct sk_buff *lkpi_skb_dequeue(struct sk_buff_head *q)
{
	unsigned long long flags = lkpi_spin_lock_irqsave(&q->lock);
	struct sk_buff *skb;

	skb = lkpi_skb_do_dequeue(q);
	lkpi_spin_unlock_irqrestore(&q->lock, flags);
	return skb;
}

void lkpi_skb_unlink(struct sk_buff *skb, struct sk_buff_head *q)
{
	unsigned long long flags = lkpi_spin_lock_irqsave(&q->lock);

	lkpi_skb_do_unlink(skb, q);
	lkpi_spin_unlock_irqrestore(&q->lock, flags);
}

void lkpi_skb_queue_purge(struct sk_buff_head *q)
{
	struct sk_buff *skb;

	while ((skb = lkpi_skb_dequeue(q)) != NULL)
		lkpi_skb_free(skb);
}

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */
/* ------------------------------------------------------------------ */

void lkpi_skb_pool_stats(struct lkpi_skb_pool_stats *out)
{
	*out = stats;
}

void lkpi_skb_diag_dump(void)
{
	lkpi_spl_log("pool used=%u/%u high=%u/%u heap_live=%u oversize=%u storage_overflow header=%u data=%u alloc_fail=%u",
	    stats.skb_used, stats.data_used, stats.skb_high, stats.data_high,
	    stats.dyalloc_live, stats.dyalloc_oversize, stats.slot_overflow,
	    stats.data_overflow, stats.alloc_fail);
	lkpi_spl_log("pool n_alloc=%u n_free=%u geo_bad=%u double_free=%u stray_free=%u uaf_write=%u fence_bad=%u",
	    stats.n_alloc, stats.n_free, geo_bad, stats.double_free,
	    stats.stray_free, stats.uaf_write, stats.fence_bad);
}

void lkpi_skb_gen_report(void)
{
	/* emergency outlet: one line, no lock assumptions */
	lkpi_spl_log("report alloc=%u free=%u fail=%u df=%u sf=%u uaf=%u fence=%u",
	    stats.n_alloc, stats.n_free, stats.alloc_fail, stats.double_free,
	    stats.stray_free, stats.uaf_write, stats.fence_bad);
}

void lkpi_skb_fence_scan(void)
{
	unsigned long flags = lkpi_spl_irqsave();
	uint32_t i;

	for (i = 0; i < cfg_n_header; i++) {
		char detail[128];

		if (skb_slots[i]._gen != LKPI_SKB_FREED_MAGIC)
			continue;
		if (free_slot_shape_bad(&skb_slots[i], detail, sizeof(detail)) != NULL) {
			stats.fence_bad++;
			lkpi_spl_log("fence scan: released header slot %u is corrupted (%s)",
			    i, detail);
			goto out;
		}
		if (slot_buf[i].kind == LKPI_SKB_BUF_POOL &&
		    skb_data[slot_buf[i].idx].ref == 0) {
			unsigned int di = slot_buf[i].idx;
			unsigned char fb = fence_byte(di);
			unsigned int j;

			for (j = 0; j < cfg_data_sz; j++) {
				if (skb_data[di].buf[j] != fb) {
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
/* platform hooks (target build; a host build supplies its own)        */
/* ------------------------------------------------------------------ */

#ifndef LKPI_SKB_POOL_NO_TARGET_HOOKS
unsigned long lkpi_spl_irqsave(void)
{
	return (unsigned long)osCriticalEnter();
}

void lkpi_spl_irqrestore(unsigned long flags)
{
	osCriticalExit((os_critical_state_t)flags);
}

void lkpi_spl_log(const char *fmt, ...)
{
	char line[192];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	lkpi_printf(ULOG_INFO_LEVEL, "lkpi_skb: %s\n", line);
}

void *lkpi_spl_malloc(unsigned int size)
{
	return osMemoryAlloc(size != 0 ? size : 1u);
}

void lkpi_spl_free(void *p)
{
	osMemoryFree(p);
}

void lkpi_spl_page_put(struct page *page)
{
	/* the interface's own release (mm.h): one reference back, the block
	 * returned when it was the last */
	put_page(page);
}
#endif /* LKPI_SKB_POOL_NO_TARGET_HOOKS */
