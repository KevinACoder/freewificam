/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/ip.h
 * @brief   The include a driver's platform layer names for the IPv4 helpers; the wire
 *          header itself belongs to the vendored tree in this build, and this face
 *          states why.
 *
 * @design declaration-only compatibility face carrying the IPv4 wire header. The face is
 *         the single definition of `struct iphdr` in this build: the mainline drivers here
 *         reach `ip_hdr(skb)->protocol` through it, and the face contract asserts the
 *         layout. A vendored tree that ships its own replacement `include/ip.h` (the SDIO
 *         vendor driver does) must not define the struct a second time, and on the
 *         interface the mechanism that keeps it from doing so is the include guard: the
 *         interface's `linux/ip.h` and the vendored replacement use the same guard name,
 *         so the replacement is skipped once the interface's header has been read. This
 *         face therefore carries the interface's guard name (`_LINUX_IP_H`) rather than the
 *         project's own, which is what makes a vendored shadow header behave here exactly
 *         as it behaves on the interface. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUX_IP_H
#define _LINUX_IP_H

#include "../../linuxkpi_core.h"
#include <linux/in.h>
#include <linux/ieee80211.h>

struct sk_buff;
struct iphdr;

/*
 * [LNX] include/linux/ip.h: `static inline struct iphdr *ip_hdr(const struct sk_buff
 * *skb) { return (struct iphdr *)skb_network_header(skb); }`.
 *
 * The interface reaches the network header through the offset its stack keeps on
 * the sk_buff: mac80211 prepends the 802.11 header into the headroom and leaves
 * that offset pointing at the IP header, so a driver's transmit path can read the
 * header it was handed.  This port has no such offset - the bridge builds the
 * whole 802.11 frame in the buffer - so the network header is derived from the
 * frame's own geometry instead, which is the port's real offset and the one the
 * drivers here are compiled against: an 802.11 data frame's payload opens with
 * the LLC/SNAP header, and the network header follows it.  A frame that is not a
 * data frame, or one too short to hold what the derivation names, has no network
 * header and answers NULL.
 */
static __always_inline __unused struct iphdr *
ip_hdr(const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr;
	unsigned int off;

	if (skb == NULL || skb->len < 24)
		return NULL;
	hdr = (const struct ieee80211_hdr *)skb->data;
	if (!ieee80211_is_data(hdr->frame_control))
		return NULL;
	off = ieee80211_hdrlen(hdr->frame_control) + 8;	/* LLC/SNAP */
	if (off > skb->len)
		return NULL;
	return (struct iphdr *)(skb->data + off);
}


/* [LNX] include/uapi/linux/ip.h: the IPv4 wire header. The interface selects its
 * bitfield order from the target's endianness; this port is little-endian, so the
 * __LITTLE_ENDIAN_BITFIELD arm is the one stated. */
struct iphdr {
	__u8	ihl:4,
		version:4;
	__u8	tos;
	__be16	tot_len;
	__be16	id;
	__be16	frag_off;
	__u8	ttl;
	__u8	protocol;
	__sum16	check;
	__be32	saddr;
	__be32	daddr;
	/*The options start here. */
};

/*
 * The interface reaches these three through offsets its stack keeps on the
 * sk_buff (include/linux/skbuff.h: skb->network_header / skb->transport_header).
 * This port's sk_buff carries neither offset: the network header is the
 * derivation ip_hdr() states above, and the transport header is the network
 * header plus the IPv4 header's own length. The three accessors are therefore
 * stated here, over that one derivation, so a driver that names any of them reads
 * the port's real addresses rather than an unset offset.
 */
static __always_inline __unused unsigned char *
skb_network_header(const struct sk_buff *skb)
{
	return (unsigned char *)ip_hdr(skb);
}

/* [LNX] include/linux/skbuff.h:3270: `return skb->transport_header -
 * skb->network_header;` - the length of the network header alone. */
static __always_inline __unused u32
skb_network_header_len(const struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);

	if (iph == NULL)
		return 0;
	return (u32)iph->ihl * 4;
}

/* [LNX] include/linux/skbuff.h:3090: the transport header follows the network
 * header by its length, which is the geometry udp_hdr() states for its own
 * family. */
static __always_inline __unused unsigned char *
skb_transport_header(const struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);

	if (iph == NULL)
		return NULL;
	return (unsigned char *)iph + (unsigned int)iph->ihl * 4;
}

#endif /* _LINUX_IP_H */
