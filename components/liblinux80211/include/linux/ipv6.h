/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/ipv6.h
 * @brief   The IPv6 wire vocabulary a bridge/NAT layer reads out of a received frame.
 *
 * @design declaration-only compatibility face. The interface splits this surface across
 *         include/uapi/linux/ipv6.h (the wire header) and include/uapi/linux/in6.h (the
 *         address record), reached through include/linux/ipv6.h; the records the compiled
 *         set reaches are stated here, transcribed field for field, because the vendored
 *         bridge extension casts a received buffer to `struct ipv6hdr` and reads its
 *         addresses itself. The address record keeps the interface's union member names
 *         (s6_addr/s6_addr16) so driver code reads unchanged; the wire header keeps the
 *         interface's field order and its little-endian bitfield arm (this port is
 *         little-endian). Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_LINUX_IPV6_H_
#define _LINUXKPI_LINUX_IPV6_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/uapi/linux/in6.h: the 128-bit address record. */
struct in6_addr {
	union {
		__u8		u6_addr8[16];
		__be16		u6_addr16[8];
		__be32		u6_addr32[4];
	} in6_u;
#define	s6_addr			in6_u.u6_addr8
#define	s6_addr16		in6_u.u6_addr16
#define	s6_addr32		in6_u.u6_addr32
};

/* [LNX] include/uapi/linux/ipv6.h: the IPv6 wire header. */
struct ipv6hdr {
	__u8			priority:4,
				version:4;
	__u8			flow_lbl[3];

	__be16			payload_len;
	__u8			nexthdr;
	__u8			hop_limit;

	struct in6_addr		saddr;
	struct in6_addr		daddr;
};

/* [LNX] include/uapi/linux/in6.h:135 */
#define	IPPROTO_ICMPV6		58	/* ICMPv6 */

/* [LNX] include/uapi/linux/ipv6.h:63-68: the extension-header record a transmit
 * path walks with ipv6_optlen(). */
struct ipv6_opt_hdr {
	__u8	nexthdr;
	__u8	hdrlen;
	/* TLV encoded option data follows. */
} __packed;

/* [LNX] include/linux/ipv6.h:8 */
#define	ipv6_optlen(p)		((((p)->hdrlen) + 1) << 3)

#endif /* _LINUXKPI_LINUX_IPV6_H_ */
