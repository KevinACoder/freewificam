#ifndef _LINUXKPI_LINUX_ETHERDEVICE_H_
#define _LINUXKPI_LINUX_ETHERDEVICE_H_
#include "../../linuxkpi_core.h"
#include <linux/if_ether.h>

static __always_inline __unused void
ether_addr_copy(u8 *dst, const u8 *src)
{
	memcpy(dst, src, ETH_ALEN);
}

static __always_inline __unused bool
ether_addr_equal(const u8 *a, const u8 *b)
{
	return memcmp(a, b, ETH_ALEN) == 0;
}

static __always_inline __unused bool
is_multicast_ether_addr(const u8 *addr)
{
	return (addr[0] & 0x01) != 0;
}

static __always_inline __unused bool
is_broadcast_ether_addr(const u8 *addr)
{
	return (addr[0] & addr[1] & addr[2] & addr[3] & addr[4] &
	    addr[5]) == 0xff;
}

static __always_inline __unused bool
is_zero_ether_addr(const u8 *addr)
{
	return (addr[0] | addr[1] | addr[2] | addr[3] | addr[4] |
	    addr[5]) == 0;
}

static __always_inline __unused bool
is_valid_ether_addr(const u8 *addr)
{
	return !is_multicast_ether_addr(addr) && !is_zero_ether_addr(addr);
}

static __always_inline __unused bool
is_unicast_ether_addr(const u8 *addr)
{
	return !is_multicast_ether_addr(addr);
}

static __always_inline __unused void
eth_zero_addr(u8 *addr)
{
	memset(addr, 0, ETH_ALEN);
}

static __always_inline __unused void
eth_broadcast_addr(u8 *addr)
{
	memset(addr, 0xff, ETH_ALEN);
}

static __always_inline __unused void
eth_random_addr(u8 *addr)
{
	u32 r = (u32)get_random_int();

	addr[0] = (u8)(r >> 24) & 0xfe;	/* unicast, locally administered */
	addr[1] = (u8)(r >> 16);
	addr[2] = (u8)(r >> 8);
	addr[3] = (u8)r;
	addr[4] = (u8)(r >> 24);
	addr[5] = (u8)(r >> 16);
}

#endif
