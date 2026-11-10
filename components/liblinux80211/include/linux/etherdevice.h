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

/* [LNX] include/linux/etherdevice.h: the MAC <-> u64 helpers the real
 * net/cfg80211.h names (random address derivation and the scan denominator).
 * The interface packs the address big-endian into the low 48 bits; both arms
 * are the interface's own bodies. */
static __always_inline u64 ether_addr_to_u64(const u8 *addr)
{
	u64 u = 0;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		u = u << 8 | addr[i];
	return u;
}

static __always_inline void u64_to_ether_addr(u64 u, u8 *addr)
{
	int i;

	for (i = ETH_ALEN - 1; i >= 0; i--) {
		addr[i] = u & 0xff;
		u >>= 8;
	}
}
#include <linux/if_ether.h>
#include <linux/netdevice.h>

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

/* [LNX] include/linux/etherdevice.h:406-413: the comparison for addresses that may
 * not be 16-bit aligned. The interface's !CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
 * arm is a plain memcmp(), which is what this port's byte-wise ether_addr_equal()
 * above already is; stated separately because a caller names this spelling when it
 * cannot promise alignment, and the compiler may then emit an unaligned-safe
 * compare. */
static inline bool ether_addr_equal_unaligned(const u8 *addr1, const u8 *addr2)
{
	return memcmp(addr1, addr2, ETH_ALEN) == 0;
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

/* [LNX] include/linux/etherdevice.h:324-327: the address setter a driver's MAC-change
 * path calls. The interface's body is __dev_addr_set(dev, addr, ETH_ALEN), which is
 * dev_addr_mod() at offset zero; that is the call stated here, over the face's
 * dev_addr_mod() (include/linux/netdevice.h). */
static inline void eth_hw_addr_set(struct net_device *dev, const u8 *addr)
{
	dev_addr_mod(dev, 0, addr, ETH_ALEN);
}


/* [LNX] include/linux/etherdevice.h:213-230: an Ethertype/length field at or
 * below ETH_P_802_3_MAX is a length (802.3), not an Ethertype.  htons() is the
 * contract header's byte-order helper. */
#define	ETH_P_802_3_MAX	1536
static inline bool eth_proto_is_802_3(__be16 proto)
{
#ifndef __BIG_ENDIAN
	proto &= htons(0xFF00);
#endif
	return proto <= htons(ETH_P_802_3_MAX);
}

#endif /* _LINUXKPI_LINUX_ETHERDEVICE_H_ */