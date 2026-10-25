/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/ipv6.h
 * @brief   The IPv6 next-header vocabulary and address comparison a driver's
 *          offload and transmit paths name.
 *
 * @design declaration-only compatibility face. The address comparison is the
 *         interface's own inline shape (include/net/ipv6.h:505); the next-header
 *         constant and the extension-header test are stated as the interface
 *         states them (the test's body belongs to the kernel's networking stack
 *         and has no counterpart on this port yet). The address and wire records
 *         themselves come from linux/ipv6.h, this port's home for them.
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_NET_IPV6_H_
#define _LINUXKPI_NET_IPV6_H_

#include "../../linuxkpi_core.h"
#include <linux/ipv6.h>

/* [LNX] include/net/ipv6.h:43 */
#define	NEXTHDR_NONE		59	/* No next header */

/* [LNX] include/net/ipv6.h:505-508 */
static __always_inline __unused int
ipv6_addr_cmp(const struct in6_addr *a1, const struct in6_addr *a2)
{
	return memcmp(a1, a2, sizeof(struct in6_addr));
}

/* [LNX] include/net/ipv6.h:1107 */
bool ipv6_ext_hdr(u8 nexthdr);

#endif /* _LINUXKPI_NET_IPV6_H_ */
