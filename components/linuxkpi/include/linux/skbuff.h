/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/skbuff.h
 * @brief   The socket-buffer allocation spelling Linux network drivers use that the
 *          contract header does not carry.
 *
 * @design declaration-only compatibility face; `struct sk_buff` and the queue/geometry
 *         operations already live in the contract header (components/linuxkpi/
 *         linuxkpi_core.h, whose sk_buff section is the single definition) and the pool
 *         API face is components/linuxkpi/skbuff/skbuff_pool.h. This header carries what
 *         those do not, in the interface's own grouping:
 *
 *           - the netdev-flavoured allocation entry point, whose callers here pass no
 *             net_device. It is declared as the interface declares it
 *             (include/linux/skbuff.h) so driver code reads unchanged; the body belongs
 *             to the same allocation seam as the rest of the sk_buff surface - no
 *             second pool.
 *           - the checksum-computation states a buffer's `ip_summed` carries, with the
 *             interface's values (they are contract with the receive path that sets
 *             them and the transmit path that tests them).
 *           - the buffer-geometry helpers (skb_end_pointer, skb_tail_pointer,
 *             skb_set_tail_pointer, skb_is_nonlinear, skb_cloned) and the
 *             shared-information record reached through skb_shinfo(), each with the
 *             port's real answer stated at its site: the pool hands out one linear
 *             payload per buffer with no trailing block, so the fragment-list and
 *             segmentation fields are the values a linear buffer has, and the one field
 *             the interface lets a caller write (frag_list) is written only from a
 *             branch that a false fragment-list test never enters.
 *           - the entry points whose bodies belong to the sk_buff seam rather than to a
 *             header (skb_copy_bits, kfree_skb_list), declared as the interface declares
 *             them.
 *
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SKBUFF_H_
#define _LINUXKPI_LINUX_SKBUFF_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/skbuff.h:23 includes <linux/atomic.h>; the face keeps the
 * interface's own include, which is how the atomic API reaches a unit that takes this
 * header. */
#include <linux/atomic.h>
/* [LNX] include/linux/skbuff.h:32 includes <linux/if_packet.h>; the receive
 * classification a driver stamps onto the buffer reaches a unit through it. */
#include <linux/if_packet.h>

struct net_device;

/* [LNX] include/linux/skbuff.h:248-251: the checksum-computation states a buffer's
 * `ip_summed` carries. The values are contract with every path that tests them (the
 * receive path writes one, the transmit path and the GRO path compare against them), so
 * they are stated with the interface's values. */
#define	CHECKSUM_NONE		0
#define	CHECKSUM_UNNECESSARY	1
#define	CHECKSUM_COMPLETE	2
#define	CHECKSUM_PARTIAL	3

/*
 * Allocate a socket buffer with `length` bytes of payload space and `NET_SKB_PAD`
 * headroom. `dev` may be NULL: nothing in this project charges the allocation to a
 * net device. Returns NULL when the pool is exhausted.
 */
struct sk_buff *__netdev_alloc_skb(struct net_device *dev, unsigned int length,
				   gfp_t gfp_mask);

/*
 * Clone a socket buffer: a second descriptor over the same payload, with its own head
 * and control block. Returns NULL when the descriptor pool is exhausted.
 */
struct sk_buff *skb_clone(const struct sk_buff *skb, gfp_t gfp_mask);

static inline struct sk_buff *netdev_alloc_skb(struct net_device *dev,
					       unsigned int length)
{
	return __netdev_alloc_skb(dev, length, GFP_ATOMIC);
}

/*
 * [LNX] include/linux/skbuff.h:1717-1745: the offset-layout triple. The interface has
 * two builds of these, selected on NET_SKBUFF_DATA_USES_OFFSET; this port's sk_buff
 * keeps tail/end as offsets (components/linuxkpi/skbuff/skbuff_pool.h), so the offset
 * build is the one transcribed. skb_end_pointer() is the address the interface's
 * shared-information block would begin at; the vendored receive path stores it as the
 * upper bound of a frame's payload, which is what the offset means here.
 */
static __always_inline __unused unsigned char *
skb_end_pointer(const struct sk_buff *skb)
{
	return skb->head + skb->end;
}

/* [LNX] include/linux/skbuff.h:2725-2728 (offset build) */
static __always_inline __unused unsigned char *
skb_tail_pointer(const struct sk_buff *skb)
{
	return skb->head + skb->tail;
}

/* [LNX] include/linux/skbuff.h:2726-2730 (offset build). The offset is relative to
 * the data pointer: the interface moves the tail to the payload's start first
 * (skb_reset_tail_pointer, the contract header's), so skb_tail_pointer() answers
 * data + offset whatever headroom the buffer carries. Getting this wrong is not a
 * cosmetic slip - a buffer with headroom then reports a tail short by exactly that
 * headroom, and a driver that bounds a frame against the tail (the vendored SDIO
 * receive path does, hal/rtl8188f/sdio/sdio_ops.c:1367-1381) rejects frames that
 * reach the end of their buffer.
 *
 * The payload length follows here, where the interface leaves it alone: this port's
 * pool enforces "the tail covers headroom plus payload" on every geometry operation
 * (skbuff_pool.c:234-249), and a helper that legally desyncs the two in Linux would
 * make that check refuse the driver's next skb_put. */
static __always_inline __unused void
skb_set_tail_pointer(struct sk_buff *skb, const int offset)
{
	skb_reset_tail_pointer(skb);
	skb->tail += (unsigned int)offset;
	skb->len = skb->tail - (unsigned int)(skb->data - skb->head);
}

/*
 * [LNX] include/linux/skbuff.h (struct skb_shared_info): the per-buffer record carrying
 * the fragment list, the segmentation parameters and the destructor argument. The
 * members carried here are the ones the compiled set names, in the interface's order
 * (flags, meta_len, nr_frags, tx_flags, gso_size, gso_segs, frag_list) so their relative
 * offsets are the interface's; everything from the timestamp union on is not carried -
 * no compiled unit names it, and the types it drags (skb_frag_t, ubuf_info, the
 * hardware-timestamp records) have no counterpart on this port.
 */
struct skb_shared_info {
	__u8		flags;
	__u8		meta_len;
	__u8		nr_frags;
	__u8		tx_flags;
	unsigned short	gso_size;
	unsigned short	gso_segs;
	struct sk_buff	*frag_list;
};

/*
 * [LNX] include/linux/skbuff.h:1784: `#define skb_shinfo(SKB) ((struct skb_shared_info
 * *)(skb_end_pointer(SKB)))`.
 *
 * The interface's macro points into the trailing block of the buffer's linear store.
 * This port's pool does not reserve one: it hands out one linear payload per buffer and
 * sets `end` to that payload's size, so the interface's address is the byte after the
 * payload - for a full-size payload, the next pool slot, which a write there would
 * corrupt. What is returned instead is a record stating what is true of every buffer
 * this port produces: no fragment list, no segmentation, no fragments (every field
 * above at its zero value). Reads therefore return the port's real answer rather than
 * whatever bytes a reused pool buffer happens to hold, and the vendored transmit path's
 * fragment-list cleanup - reached only through skb_has_frag_list() below, which reads
 * frag_list - is never entered. The one write the compiled set performs stores NULL into
 * frag_list, which is that same answer.
 *
 * What a real implementation needs later: a per-buffer trailing block (the pool
 * reserving sizeof(struct skb_shared_info) past the payload, `end` describing the
 * payload alone), at which point this becomes the interface's own macro and a writable
 * field means what the interface means by it. Until then a caller must not treat any
 * field as per-buffer storage.
 */
static __always_inline __unused struct skb_shared_info *
lkpi_skb_shinfo_linear(const struct sk_buff *skb)
{
	static struct skb_shared_info linear;

	(void)skb;
	return &linear;
}

#define	skb_shinfo(SKB)	lkpi_skb_shinfo_linear(SKB)

/* [LNX] include/linux/skbuff.h:2527-2530 */
static __always_inline __unused bool
skb_is_nonlinear(const struct sk_buff *skb)
{
	return skb->data_len;
}

/* [LNX] include/linux/skbuff.h:4234-4237: the fragment-list test, in the interface's own
 * shape. Every buffer this port produces has no fragment list (see skb_shinfo above), so
 * the answer is false for all of them - which is the branch the vendored transmit path's
 * cleanup expects for a linear buffer. */
static __always_inline __unused bool
skb_has_frag_list(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->frag_list != NULL;
}

/*
 * [LNX] include/linux/skbuff.h:2029-2033: `static inline int skb_cloned(const struct
 * sk_buff *skb)`, whose interface body tests the header's `cloned` flag together with
 * the shared record's dataref. This port's sk_buff has no `cloned` flag and the pool
 * tracks payload sharing in its own data record (not visible from a descriptor), so the
 * descriptor cannot prove a payload private. The answer is therefore the shared one:
 * the pool's clone is a second descriptor over a reference-counted payload, and a
 * buffer that reached the receive path through that clone must not be handed to a path
 * that writes it. A buffer that took a private copy is also told "shared", which at
 * worst costs it a fast path - never its data.
 */
static __always_inline __unused int
skb_cloned(const struct sk_buff *skb)
{
	(void)skb;
	return 1;
}

/*
 * [LNX] include/linux/skbuff.h:4286: `int skb_copy_bits(const struct sk_buff *skb, int
 * offset, void *to, int len)`. Declared, not defined: the interface's body copies out of
 * the linear area and then walks the fragment list, returning -EFAULT when the range
 * does not fit; on this port the fragment walk has nothing to visit, but the entry point
 * belongs to the sk_buff seam the pool body owns, next to the other copy operations
 * there, so a caller that needs it gets the seam's definition rather than a second one
 * here.
 */
int skb_copy_bits(const struct sk_buff *skb, int offset, void *to, int len);

/*
 * [LNX] include/linux/skbuff.h:1341-1344: `static inline void kfree_skb_list(struct
 * sk_buff *segs)`, which frees every buffer of a `->next` chain. The interface's own
 * linear body (the "copy from skbuff.c" shape the vendored tree carries for kernels
 * older than 3.10) is exactly this walk, and on this port the chain is the only
 * structure a list has - so the walk is the body, and it reuses kfree_skb() (the
 * contract header's single release call) rather than adding a second release path.
 */
static __always_inline __unused void
kfree_skb_list(struct sk_buff *segs)
{
	while (segs != NULL) {
		struct sk_buff *next = segs->next;

		kfree_skb(segs);
		segs = next;
	}
}

/*
 * [LNX] include/linux/skbuff.h:3865-3869: `static inline struct sk_buff *pskb_copy(
 * struct sk_buff *skb, gfp_t gfp_mask)` copies the buffer so that its data is private.
 * On the interface the distinction from skb_copy() is the fragment list; this port's
 * sk_buff is one linear buffer with no fragment list (the contract header's sk_buff
 * carries data_len 0 by construction), so the private copy is the copy the contract
 * header already states, and the body delegates to it rather than adding a second copy
 * mechanism.
 */
static __always_inline __unused struct sk_buff *
pskb_copy(struct sk_buff *skb, gfp_t gfp_mask)
{
	return skb_copy(skb, gfp_mask);
}

/*
 * [LNX] include/linux/skbuff.h:4040-4043: `static inline int skb_linearize(struct
 * sk_buff *skb) { return skb_is_nonlinear(skb) ? __skb_linearize(skb) : 0; }`. The
 * predicate is false for every buffer this port produces (single linear storage), which
 * is why the honest body is the interface's own already-linear result.
 */
static __always_inline __unused int
skb_linearize(struct sk_buff *skb)
{
	(void)skb;
	return 0;
}

/*
 * [LNX] include/linux/skbuff.h:4244-4245: `#define skb_walk_frags(skb, iter) for (iter =
 * skb_shinfo(skb)->frag_list; iter; iter = iter->next)`. The interface walks the
 * fragment list; this port's sk_buff has no fragment list at all (single linear
 * storage), so the walk visits nothing - stated as the interface's loop shape so the
 * call site's body stays a loop body, with the empty range being the port's real
 * answer rather than a silent skip.
 */
#define	skb_walk_frags(skb, iter)					\
	for ((iter) = NULL; (iter) != NULL; (iter) = (iter)->next)

#endif /* _LINUXKPI_LINUX_SKBUFF_H_ */