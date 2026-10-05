/*
 * @file
 * @brief IPv4 addressing constants used by the net80211 bridge.
 */

#ifndef _COMPAT_NETINET_IN_H_
#define _COMPAT_NETINET_IN_H_

#include <sys/endian.h>
#include <sys/types.h>

#define IPVERSION 4

struct in_addr {
	uint32_t s_addr;
};

struct in_ifaddr {
	struct in_addr ia_addr;
	struct in_addr ia_netmask;
};

typedef uint32_t in_addr_t;

#define INADDR_ANY 0x00000000
#define INADDR_BROADCAST 0xffffffff

#endif /* _COMPAT_NETINET_IN_H_ */
