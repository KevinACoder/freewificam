/*
 * @file
 * @brief IPv4 header layout (ARP/IP bridging paths only).
 */

#ifndef _COMPAT_NETINET_IP_H_
#define _COMPAT_NETINET_IP_H_

#include <sys/types.h>

struct ip {
	uint8_t ip_hl_v;
	uint8_t ip_tos;
	uint16_t ip_len;
	uint16_t ip_id;
	uint16_t ip_off;
	uint8_t ip_ttl;
	uint8_t ip_p;
	uint16_t ip_sum;
	struct in_addr ip_src, ip_dst;
} __packed;

#endif /* _COMPAT_NETINET_IP_H_ */
