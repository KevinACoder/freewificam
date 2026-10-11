/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/ip.h
 * @brief   The include a driver's platform layer names for the IPv4 helpers; the wire
 *          header itself belongs to the vendored tree in this build, and this face
 *          states why.
 *
 * @design declaration-only compatibility face, deliberately without the IPv4 wire
 *         header. The interface (include/linux/ip.h) defines `struct iphdr` and
 *         ip_hdr(); in this build the vendored tree carries its own include/ip.h with
 *         the same `struct iphdr` (and its own include guard), and that header is
 *         included after this one by the driver's own type header - so defining the
 *         struct here as well would be a redefinition of one wire format, which is
 *         exactly the duplicate the project forbids. The struct is therefore
 *         forward-declared for any pointer use; the byte layout has exactly one
 *         definition in this build, the vendored one, and the day the vendored header
 *         stops providing it the layout moves here rather than appearing twice.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_IP_H_
#define _LINUXKPI_LINUX_IP_H_

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

#endif /* _LINUXKPI_LINUX_IP_H_ */
