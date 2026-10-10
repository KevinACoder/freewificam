/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/udp.h
 * @brief   The UDP header layout and accessor a receive path names.
 *
 * @design declaration-only compatibility face. `struct udphdr` is the wire layout and is
 *         transcribed with the interface's exact field order and types (the protocol
 *         header is read off a received frame, so the bytes are contract); the accessor
 *         `udp_hdr()` is declared as the interface declares it and not defined, for the
 *         same reason as its IPv4 sibling in this face (components/linuxkpi/include/
 *         linux/ip.h): the interface's body reaches a transport-header offset this
 *         project's sk_buff contract does not model yet, and a caller must be given the
 *         port's real offset rather than a silently wrong one.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_UDP_H_
#define _LINUXKPI_LINUX_UDP_H_

#include "../../linuxkpi_core.h"
#include <linux/in.h>
#include <linux/ip.h>

struct sk_buff;

/* [LNX] include/uapi/linux/udp.h:24-29 */
struct udphdr {
	__be16	source;
	__be16	dest;
	__be16	len;
	__sum16	check;
};

/*
 * [LNX] include/linux/udp.h:21-24: `static inline struct udphdr *udp_hdr(const struct
 * sk_buff *skb) { return (struct udphdr *)skb_transport_header(skb); }`.
 *
 * Same port geometry as its IPv4 sibling (components/linuxkpi/include/linux/ip.h):
 * the transport header sits behind the network header by the network header's own
 * length.  A frame whose network header cannot be derived, or one whose IP header
 * is not fully present, has no transport header and answers NULL.
 */
static __always_inline __unused struct udphdr *
udp_hdr(const struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);

	if (iph == NULL)
		return NULL;
	return (struct udphdr *)((const unsigned char *)iph +
	    ((unsigned int)iph->ihl * 4));
}

#endif /* _LINUXKPI_LINUX_UDP_H_ */
