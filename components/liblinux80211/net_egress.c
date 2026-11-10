/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net_egress.c
 * @brief   The egress seam's component half: one call from a network module
 *          into a net_device's transmit entry.
 *
 * The receive half of the seam is the egress module's own definition of
 * lkpi_netdev_rx(); the component calls it from netif_rx() and owns nothing
 * else about it.  The transmit half is this function: the module has a frame
 * and a net_device, and the only thing between them is an skb.
 *
 * @design the same hand-off the in-tree lwIP port performs today, lifted out
 *         of that port so a second egress does not repeat it
 *         (third-party/lwip/ports/lkpinetif.c:lkpinetif_tx_frame).  The device
 *         owns the skb once ndo_start_xmit returns, exactly as on Linux.
 */

#include <stddef.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include "linux80211/net.h"

#ifndef GFP_KERNEL
#define GFP_KERNEL 0
#endif

int
lkpi_netdev_xmit(struct net_device *dev, const void *frame, size_t len)
{
	struct sk_buff *skb;

	if (dev == NULL || frame == NULL || len == 0)
		return -EINVAL;
	if (dev->netdev_ops == NULL || dev->netdev_ops->ndo_start_xmit == NULL)
		return -ENODEV;

	skb = lkpi_dev_alloc_skb((unsigned int)len, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;
	if (skb_put(skb, (unsigned int)len) == NULL) {
		kfree_skb(skb);
		return -ENOMEM;
	}
	memcpy(skb->data, frame, len);
	(void)dev->netdev_ops->ndo_start_xmit(skb, dev);
	return 0;
}
