/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/gso.h
 * @brief   The generic-segmentation-offload entry point and the segmentation-type
 *          bits a driver's A-MSDU transmit path names.
 *
 * @design declaration-only compatibility face. The interface's header
 *         (include/net/gso.h) declares the segmentation entry point and a family
 *         of per-tunnel callback helpers; what the compiled set reaches is the
 *         entry point (whose inline form forwards to the extern
 *         __skb_gso_segment) and the two segmentation-type bits the driver ORs
 *         into the shared record. The segmentation-type bits keep the interface's
 *         values (include/linux/skbuff.h:669,679) so the value the transmit path
 *         stamps is the one the receive path tests. The extern body belongs to the
 *         kernel's networking stack and has no counterpart on this port yet.
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_NET_GSO_H_
#define _LINUXKPI_NET_GSO_H_

#include "../../linuxkpi_core.h"
#include <linux/skbuff.h>
#include <linux/netdevice.h>

/* [LNX] include/linux/skbuff.h:669,679: the two segmentation-type bits the
 * compiled set names, at the interface's positions. */
#define	SKB_GSO_TCPV4		(1 << 0)
#define	SKB_GSO_TCPV6		(1 << 4)

/* [LNX] include/linux/skbuff.h:5296-5299 */
static __always_inline __unused bool
skb_is_gso(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_size;
}

/* [LNX] include/net/gso.h:73-74 */
struct sk_buff *__skb_gso_segment(struct sk_buff *skb,
				  netdev_features_t features, bool tx_path);

/* [LNX] include/net/gso.h:76-80 */
static __always_inline __unused struct sk_buff *
skb_gso_segment(struct sk_buff *skb, netdev_features_t features)
{
	return __skb_gso_segment(skb, features, true);
}

#endif /* _LINUXKPI_NET_GSO_H_ */
