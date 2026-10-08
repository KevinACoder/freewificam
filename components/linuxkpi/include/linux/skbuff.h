/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/skbuff.h
 * @brief   The socket-buffer allocation spelling Linux network drivers use that the
 *          contract header does not carry.
 *
 * @design declaration-only compatibility face; `struct sk_buff` and the queue/geometry
 *         operations already live in the contract header (components/linuxkpi/
 *         linuxkpi_core.h, whose sk_buff section is the single definition) and the pool
 *         API face is components/linuxkpi/skbuff/skbuff_pool.h. This header carries only
 *         the allocation entry point those do not: the netdev-flavoured allocator, whose
 *         callers here pass no net_device. It is declared as the interface declares it
 *         (include/linux/skbuff.h) so driver code reads unchanged; the body belongs to
 *         the same allocation seam as the rest of the sk_buff surface - no second pool.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_SKBUFF_H_
#define _LINUXKPI_LINUX_SKBUFF_H_

#include "../../linuxkpi_core.h"

struct net_device;

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

#endif /* _LINUXKPI_LINUX_SKBUFF_H_ */