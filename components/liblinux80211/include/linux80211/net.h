/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net.h
 * @brief  The network-egress seam: how a frame leaves the component for the
 *         network stack, and how the stack hands one back.
 *
 * Downward (stack -> device): the egress module resolves the interface's
 * net_device and calls lkpi_netdev_xmit(), which allocates the skb, copies the
 * frame and enters the device's own ndo_start_xmit (mac80211's, once the stack
 * lands).  This is the same hand-off the in-tree lwIP port performs today
 * (third-party/lwip/ports/lkpinetif.c: lkpinetif_tx_frame).
 *
 * Upward (device -> stack): the egress module defines lkpi_netdev_rx().  The
 * component's netif_rx() calls it (the definition is a weak symbol, so an
 * image without an egress consumes the frame and says nothing).  The callee
 * owns the skb and must release it.
 *
 * Replaceability is link-time, not runtime: one image has exactly one
 * definition of lkpi_netdev_rx, the same discipline the bus seam's backends
 * follow.  lwIP is the intended implementation; the null egress is the
 * reference second implementation the seam's test links.
 *
 * STATUS: draft (FW_NET_API_READY 0).  The seam is frozen when the real
 * mac80211 net-device path compiles against it; until then this header states
 * the contract the existing code already satisfies and nothing more.
 */

#ifndef LINUX80211_NET_H
#define LINUX80211_NET_H

#include <stddef.h>

/* 0: the shape may still change when mac80211's net_device path lands. */
#define FW_NET_API_READY 0

struct sk_buff;
struct net_device;

/*
 * Provided by the egress module.  Called from netif_rx() with ownership of the
 * skb; must release it exactly once.  A weak declaration in the component lets
 * an image without an egress link.
 */
void lkpi_netdev_rx(struct sk_buff *skb);

/*
 * Provided by the component.  Allocates an skb, copies `len` bytes of `frame`
 * and enters the device's ndo_start_xmit; on success the device owns the skb.
 * Returns 0, -ENODEV when the device has no transmit entry, or -ENOMEM.
 */
int lkpi_netdev_xmit(struct net_device *dev, const void *frame, size_t len);

#endif /* LINUX80211_NET_H */
