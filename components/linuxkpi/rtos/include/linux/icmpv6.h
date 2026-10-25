/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/icmpv6.h
 * @brief   The ICMPv6 record a bridge/NAT layer rewrites the checksum of.
 *
 * @design declaration-only compatibility face. The interface's header (include/linux/
 *         icmpv6.h, over uapi/linux/icmpv6.h) owns the ICMPv6 message surface; what the
 *         compiled set reaches is the fixed header, because the vendored bridge
 *         extension recomputes the checksum of a neighbour-discovery message whose
 *         link-layer address it replaced. The record is transcribed field for field; the
 *         bitfield order is the target's, which is little-endian here. Reference for the
 *         face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_LINUX_ICMPV6_H_
#define _LINUXKPI_LINUX_ICMPV6_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/uapi/linux/icmpv6.h: the ICMPv6 message header. */
struct icmp6hdr {
	__u8		icmp6_type;
	__u8		icmp6_code;
	__sum16		icmp6_cksum;

	union {
		__be32			un_data32[1];
		__be16			un_data16[2];
		__u8			un_data8[4];

		struct icmpv6_echo {
			__be16		identifier;
			__be16		sequence;
		} u_echo;

		struct icmpv6_nd_advt {
			__u32		reserved:5,
					override:1,
					solicited:1,
					router:1,
					reserved2:24;
		} u_nd_advt;

		struct icmpv6_nd_ra {
			__u8		hop_limit;
			__u8		reserved:3,
					router_pref:2,
					home_agent:1,
					other:1,
					managed:1;
			__be16		rt_lifetime;
		} u_nd_ra;
	} icmp6_dataun;

#define	icmp6_identifier	icmp6_dataun.u_echo.identifier
#define	icmp6_sequence		icmp6_dataun.u_echo.sequence
#define	icmp6_pointer		icmp6_dataun.un_data32[0]
#define	icmp6_mtu		icmp6_dataun.un_data32[0]
#define	icmp6_unused		icmp6_dataun.un_data32[0]
#define	icmp6_maxdelay		icmp6_dataun.un_data16[0]
#define	icmp6_datagram_len	icmp6_dataun.un_data8[0]
#define	icmp6_router		icmp6_dataun.u_nd_advt.router
#define	icmp6_solicited		icmp6_dataun.u_nd_advt.solicited
#define	icmp6_override		icmp6_dataun.u_nd_advt.override
#define	icmp6_ndiscreserved	icmp6_dataun.u_nd_advt.reserved
#define	icmp6_hop_limit		icmp6_dataun.u_nd_ra.hop_limit
#define	icmp6_addrconf_managed	icmp6_dataun.u_nd_ra.managed
#define	icmp6_addrconf_other	icmp6_dataun.u_nd_ra.other
#define	icmp6_rt_lifetime	icmp6_dataun.u_nd_ra.rt_lifetime
#define	icmp6_router_pref	icmp6_dataun.u_nd_ra.router_pref
};

#endif /* _LINUXKPI_LINUX_ICMPV6_H_ */
