/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/etherdevice.h
 * @brief   The Ethernet address helpers Linux drivers expect (copy, compare, classify).
 * @design declaration-only compatibility face, the same shape as this component's
 *         linux/usb.h (components/linuxkpi/include/linux/usb.h:1-20). The helpers are
 *         written as plain byte operations with the same observable semantics as the
 *         interface (include/linux/etherdevice.h); the upstream word-at-a-time forms are
 *         an optimisation this project does not need, and reuse this project's
 *         allocation/string primitives rather than introducing a second set.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_ETHERDEVICE_H_
#define _LINUXKPI_LINUX_ETHERDEVICE_H_

#include "../../linuxkpi_core.h"
#include <linux/if_ether.h>

struct net_device;
struct sk_buff;

/*
 * [LNX] include/linux/etherdevice.h:40: the receive-side protocol classifier. The
 * interface declares it as an entry point (its body lives in the network stack's
 * ethernet layer, where it inspects the frame's length/type field, decides between
 * 802.3 and Ethernet II, pulls the link header and records the device and the mac
 * header position on the buffer). Declared, not defined, for the same reason the
 * interface's other entry points are: the frame-header conventions it decides between
 * are the port's receive seam's answer, and an unresolved symbol reports a missing
 * answer where a body invented here would decide the convention in the wrong place.
 */
__be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev);

static inline bool is_zero_ether_addr(const u8 *addr)
{
	return !(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]);
}

static inline bool is_multicast_ether_addr(const u8 *addr)
{
	return 0x01 & addr[0];
}

static inline bool is_unicast_ether_addr(const u8 *addr)
{
	return !is_multicast_ether_addr(addr);
}

static inline bool is_broadcast_ether_addr(const u8 *addr)
{
	return (addr[0] & addr[1] & addr[2] & addr[3] & addr[4] & addr[5]) == 0xff;
}

static inline bool is_valid_ether_addr(const u8 *addr)
{
	return !is_multicast_ether_addr(addr) && !is_zero_ether_addr(addr);
}

static inline bool ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++) {
		if (addr1[i] != addr2[i])
			return false;
	}
	return true;
}

static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		dst[i] = src[i];
}

static inline void eth_zero_addr(u8 *addr)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		addr[i] = 0x00;
}

/* [LNX] include/linux/etherdevice.h:237: a random unicast address with the
 * locally-administered bit set, the form the 8821CU driver seeds a MAC with. */
static inline void eth_random_addr(u8 *addr)
{
	get_random_bytes(addr, ETH_ALEN);
	addr[0] &= 0xfe;	/* clear multicast bit */
	addr[0] |= 0x02;	/* set local assignment bit (IEEE802) */
}

static inline void eth_broadcast_addr(u8 *addr)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		addr[i] = 0xff;
}

#endif /* _LINUXKPI_LINUX_ETHERDEVICE_H_ */