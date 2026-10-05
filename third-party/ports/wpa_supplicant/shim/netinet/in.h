/*
 * @file
 * @brief Minimal netinet/in.h for the wpa core group: newlib has
 * none and the compat shadow pulls the whole BSD macro world with
 * it. The PSK-only build set only touches struct in_addr.
 *
 * @date 11.09.2026
 * @author zhugengyu
 */

#ifndef _WPA_SHIM_NETINET_IN_H_
#define _WPA_SHIM_NETINET_IN_H_

#include <stdint.h>
#include <sys/types.h>

struct in_addr {
	uint32_t s_addr;
};

struct in6_addr {
	uint8_t s6_addr[16];
};

/* the address-format helpers wpa references; values per POSIX */
#define AF_INET 2
#define AF_INET6 10
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

#endif /* _WPA_SHIM_NETINET_IN_H_ */
