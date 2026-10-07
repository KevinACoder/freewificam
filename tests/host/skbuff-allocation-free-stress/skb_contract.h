/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * skb_contract.h - the host harness double for the skbuff pool contract,
 * plus the boundary that lets the production headers be compiled here.
 *
 * The object under test is the pool body behind
 * components/linuxkpi/skbuff/skbuff_pool.h.  That body is target-side code;
 * the directory around this header supplies a self-contained host double of
 * it so the pool contract (slot lifetime, generation bookkeeping, linear
 * geometry, queue shape, named defect counters) can be exercised off board.
 * struct sk_buff, the statistics layout, every declaration and the queue
 * struct come from the production headers; the double supplies bodies only.
 *
 * Reading of the contract encoded by this harness (all of it from the two
 * headers, quoted where it matters):
 *
 *   - struct sk_buff is offset-style: `tail` and `end` are offsets from
 *     `head`, `data` is a pointer into [head, head + end] (skbuff_pool.h
 *     "offset-style tail/end", line 14).
 *   - end equals the requested allocation size: alloc_skb(n) yields n bytes
 *     of tailroom, __dev_alloc_skb(n) reserves NET_SKB_PAD on top of n
 *     (linuxkpi_core.h lines 1825-1828: the dev_alloc_skb split, "+NET_SKB_PAD
 *     reserve, so a later push on the returned skb has the headroom Linux
 *     promises").  This makes the exact boundary observable.
 *   - the linear invariant is 0 <= data - head <= tail <= end, with
 *     len == tail - (data - head) and data_len == 0: linuxkpi_core.h lines
 *     2057-2058 state this shim never carries paged data ("tail always covers
 *     headroom + payload").
 *   - _gen: 0 = never handed out, positive = allocation generation of the
 *     current owner, LKPI_SKB_FREED_MAGIC = released back to the free list
 *     (skbuff_pool.h lines 63-67).  put/push/reserve on a released slot call
 *     lkpi_skb_uaf_hit() and leave the payload alone.
 *   - lkpi_skb_put, lkpi_skb_push and lkpi_skb_reserve are the real
 *     (non-inline) geometry entry points, so a refusal there is exactly what
 *     lkpi_skb_geo_bad_count() counts (linuxkpi_core.h lines 1803-1805:
 *     "geometry violations so far (put/push/reserve)").
 *   - skb_pull and skb_trim are static inlines in linuxkpi_core.h (lines
 *     1988-2002 and 1855-1863); the double supplies the fields they operate
 *     on, not a second body - using them here therefore exercises the
 *     production code, not the double.
 *   - skb_peek() returns NULL on an empty queue (linuxkpi_core.h lines
 *     1923-1935, the documented strengthened contract).
 *
 * Host shim boundary: this header includes the production headers directly,
 * so no mirrored struct or second declaration exists anywhere in the
 * harness.  Two host-toolchain details are handled here and only here:
 *
 *   1. glibc's <endian.h> (reached through <stdlib.h>) defines htole16,
 *      le32toh, htobe64 ... with libc meanings, and linuxkpi_core.h defines
 *      the same spellings for the kernel world; <errno.h> disagrees with the
 *      header on two of the error numbers as well.  The libc set is undefined
 *      before the include so the kernel spellings win in this build and the
 *      compile holds under -Werror.
 *   2. one pre-existing warning inside the imported header (a `struct mutex *`
 *      handed to the spinlock pair from a static inline scope-guard helper
 *      this harness never calls) is relaxed around the include.  The
 *      component's import record notes that warning.
 *
 * Both are boundary mechanics; neither touches the contract.
 */
#ifndef SKB_CONTRACT_H
#define SKB_CONTRACT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef htole16
#undef htole32
#undef htole64
#undef le16toh
#undef le32toh
#undef le64toh
#undef htobe16
#undef htobe32
#undef htobe64
#undef be16toh
#undef be32toh
#undef be64toh
#undef ENOTEMPTY
#undef ENOTCONN

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#include "linuxkpi_core.h"
#pragma GCC diagnostic pop

/* ------------------------------------------------------------------ */
/* harness geometry profiles                                           */
/* ------------------------------------------------------------------ */

/*
 * The production pool sizes come from CONFIG_ macros; the harness makes the
 * same three numbers plus the heap-fallback switch an init-time input, so the
 * wide host profile and a constrained MCU-shaped profile run against one
 * double.  With heap_fallback == 0 an allocation that cannot be served from
 * the fixed geometry fails and is counted in alloc_fail - it never falls back.
 */
struct skb_pool_cfg {
	unsigned int	n_header;	/* struct sk_buff slots */
	unsigned int	n_data;		/* linear data slots */
	unsigned int	data_sz;	/* bytes per data slot */
	int		heap_fallback;	/* 0: fixed geometry, fail out */
};

enum skb_profile {
	SKB_PROFILE_HOST = 0,
	SKB_PROFILE_MCU = 1,
};

#define SKB_DOUBLE_MAX_HDR	64
#define SKB_DOUBLE_MAX_DATA	64
#define SKB_DOUBLE_MAX_DATA_SZ	2048

/* ---- the double's face -------------------------------------------- */

/* select a profile and reset the pool; also reachable lazily through
 * lkpi_skb_pool_init() (which then picks the host profile) */
void skb_double_init(enum skb_profile profile);
/* the same, with an explicit geometry: the three sizes plus the heap-fallback
 * switch, so a cell can pin a combination neither preset names */
void skb_double_init_cfg(const struct skb_pool_cfg *c);
/* reset the pool to the current profile (fresh generations, empty lists) */
void skb_double_reset(void);
/*
 * The pool's own refusal/defect lines ("lkpi_skb: ...") are off by default:
 * a stress run produces them for every refused operation, and the harness's
 * machine-checkable output is the result lines alone.  --verbose turns them
 * back on; lkpi_skb_diag_dump()/lkpi_skb_gen_report() always print when
 * called explicitly.
 */
void skb_double_set_verbose(int on);
const struct skb_pool_cfg *skb_double_cfg(void);
const char *skb_double_profile_name(void);

/* live skb enumeration for the judge and the matrix: static slots in index
 * order, then heap-backed headers in allocation order */
unsigned int skb_double_live_count(void);
/*
 * visit callback: slot >= 0 for a static header slot, -1 for a heap-backed
 * header; data_pool is nonzero when the skb's linear buffer is a pool slot.
 * A nonzero return stops the walk and is passed back to the caller.
 */
typedef int (*skb_live_fn)(struct sk_buff *skb, int slot, int data_pool,
    void *arg);
int skb_double_visit_live(skb_live_fn fn, void *arg);

int skb_double_is_pool_slot(const struct sk_buff *skb);
/* static slot index of a pool address, -1 for anything else (address
 * arithmetic only, safe on a stale heap pointer) */
int skb_double_slot_index(const struct sk_buff *skb);
/* address-only "is this one of ours and released?": a static slot carrying
 * the release magic, or a heap header inside the free window.  Never
 * dereferences the pointer, so a stale heap pointer is safe to ask about. */
int skb_double_is_released(const struct sk_buff *skb);
/* address of static slot idx; idx may name a slot beyond the configured pool
 * (used by the stray-pointer cells) but must stay inside the static array */
struct sk_buff *skb_double_slot_at(unsigned int idx);
/* linear buffer of a slot, live or released (NULL when it is heap-backed) */
unsigned char *skb_double_slot_buffer(struct sk_buff *skb);
/*
 * The buffer of a released pool slot, but only while that data slot is still
 * on the free list - i.e. while its contents actually describe the released
 * object.  A stale pointer into a recycled slot would name some newer skb's
 * payload, so the harness never inspects or perturbs one; NULL means "the
 * released slot no longer owns a buffer of its own".
 */
unsigned char *skb_double_released_buffer(struct sk_buff *skb);
/* digest of the whole pool state (counters, slot bookkeeping, live payload
 * windows); pointer values are deliberately not part of it */
uint64_t skb_double_digest(void);

#endif /* SKB_CONTRACT_H */
