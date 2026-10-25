/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/tcp.h
 * @brief   The TCP wire header and the transport-header accessors a driver's
 *          transmit path names.
 *
 * @design declaration-only compatibility face. The wire header is the uapi
 *         record (include/uapi/linux/tcp.h), transcribed field for field with the
 *         little-endian bitfield arm this port's target selects; the accessors are
 *         the interface's inline shapes (include/linux/tcp.h:24-37), stated over
 *         this port's transport-header derivation (skb_transport_header, the
 *         network header plus the IPv4 header length - the same geometry the
 *         interface's udp_hdr reaches through its stack offsets). Reference for
 *         the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_TCP_H_
#define _LINUXKPI_LINUX_TCP_H_

#include "../../linuxkpi_core.h"
#include <linux/skbuff.h>
#include <linux/ip.h>

/* [LNX] include/uapi/linux/tcp.h: the TCP wire header, in the
 * __LITTLE_ENDIAN_BITFIELD arm this port's target selects. */
struct tcphdr {
	__be16	source;
	__be16	dest;
	__be32	seq;
	__be32	ack_seq;
	__u16	ae:1,
		res1:3,
		doff:4,
		fin:1,
		syn:1,
		rst:1,
		psh:1,
		ack:1,
		urg:1,
		ece:1,
		cwr:1;
	__be16	window;
	__sum16	check;
	__be16	urg_ptr;
};

/* [LNX] include/linux/tcp.h:24-27 */
static __always_inline __unused struct tcphdr *
tcp_hdr(const struct sk_buff *skb)
{
	return (struct tcphdr *)skb_transport_header(skb);
}

/* [LNX] include/linux/tcp.h:29-32 */
static __always_inline __unused unsigned int
__tcp_hdrlen(const struct tcphdr *th)
{
	return th->doff * 4;
}

/* [LNX] include/linux/tcp.h:34-37 */
static __always_inline __unused unsigned int
tcp_hdrlen(const struct sk_buff *skb)
{
	return __tcp_hdrlen(tcp_hdr(skb));
}

#endif /* _LINUXKPI_LINUX_TCP_H_ */
