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
 *
 *         Three fields are part of the contract and were missing while the
 *         only caller was a full-MAC driver that never looked at them:
 *
 *         - skb->dev: mac80211's transmit path and its status path read it;
 *         - skb->protocol: the control-port test (EAPOL) compares it against
 *           sdata->control_port_protocol, and the 802.1X path is selected by
 *           that comparison, not by the frame bytes alone;
 *         - headroom and tailroom: the header push takes from the headroom
 *           (dev_alloc_skb reserves NET_SKB_PAD = 32 here, but an 802.11
 *           header plus a CCMP header can need more), and the stack may append
 *           a CCMP MIC or an IV when the key is not fully offloaded.  The
 *           egress therefore tops the headroom up to 64 and reserves 64 bytes
 *           past the frame; the allocation size is derived from the two, not
 *           hard-coded against the pool's pad.
 *
 *         The return value is the transmit entry's, mapped to the seam's
 *         convention.  A driver drop is not reported as success: the caller
 *         (the supplicant's EAPOL send or lwIP's linkoutput) has to see it.
 */

#include <stddef.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/errno.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include "linux80211/net.h"

#ifndef GFP_KERNEL
#define GFP_KERNEL 0
#endif

/* mac80211's transmit path: an 802.11 header (up to 30 bytes) plus an optional
 * CCMP header (8) must fit before the frame, and an encrypting stack may append
 * a MIC (8) or an IV after it.  64/64 is the same margin the Linux net core
 * gives a device that asks for LL_MAX_HEADER. */
#define LKPI_XMIT_HEADROOM	64u
#define LKPI_XMIT_TAILROOM	64u

int
lkpi_netdev_xmit(struct net_device *dev, const void *frame, size_t len)
{
	const unsigned char *f = frame;
	const unsigned int extra_head = LKPI_XMIT_HEADROOM > NET_SKB_PAD ?
		LKPI_XMIT_HEADROOM - NET_SKB_PAD : 0u;
	struct sk_buff *skb;
	netdev_tx_t ret;

	if (dev == NULL || frame == NULL || len < ETH_HLEN)
		return -EINVAL;
	if (dev->netdev_ops == NULL || dev->netdev_ops->ndo_start_xmit == NULL)
		return -ENODEV;

	/* The pad the allocator already reserves counts toward the headroom; the
	 * rest of the headroom and the tailroom are this call's. */
	skb = lkpi_dev_alloc_skb((unsigned int)len + extra_head +
				 LKPI_XMIT_TAILROOM, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;
	if (extra_head != 0u)
		skb_reserve(skb, extra_head);
	if (skb_put(skb, (unsigned int)len) == NULL) {
		kfree_skb(skb);
		return -ENOMEM;
	}
	memcpy(skb->data, frame, len);
	skb->dev = dev;
	skb->protocol = htons((u16)(((u16)f[12] << 8) | f[13]));

	ret = dev->netdev_ops->ndo_start_xmit(skb, dev);
	return ret == NETDEV_TX_OK ? 0 : -EIO;
}
