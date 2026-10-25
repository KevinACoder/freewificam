/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/ip6_checksum.h
 * @brief   The IPv6 pseudo-header checksum helper a bridge/NAT layer names.
 *
 * @design declaration-only compatibility face. The interface's header (include/net/
 *         ip6_checksum.h) owns the IPv6 checksum entry points; the compiled set names
 *         csum_ipv6_magic(), which folds the IPv6 pseudo-header into the running sum of
 *         a rewritten ICMPv6 message. It is declared as the interface declares it, and
 *         the checksum primitive it is built over (csum_partial) is carried by this
 *         face's net/checksum.h; the body is the port's. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_NET_IP6_CHECKSUM_H_
#define _LINUXKPI_NET_IP6_CHECKSUM_H_

#include "../../linuxkpi_core.h"

#include <linux/ipv6.h>
#include <net/checksum.h>

/* [LNX] include/net/ip6_checksum.h:34-36: __sum16 csum_ipv6_magic(const struct in6_addr
 * *saddr, const struct in6_addr *daddr, __u32 len, __u8 proto, __wsum csum); */
extern __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
			       const struct in6_addr *daddr,
			       __u32 len, __u8 proto, __wsum csum);

#endif /* _LINUXKPI_NET_IP6_CHECKSUM_H_ */
