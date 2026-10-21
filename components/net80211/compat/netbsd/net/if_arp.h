/*
 * @file
 * @brief ARP header layout used by the bridge code.
 */

#ifndef _COMPAT_NET_IF_ARP_H_
#define _COMPAT_NET_IF_ARP_H_

#include <sys/types.h>

struct arphdr {
	uint16_t ar_hrd;
	uint16_t ar_pro;
	uint8_t ar_hln;
	uint8_t ar_pln;
	uint16_t ar_op;
};

#define ARPHRD_ETHER 1
#define ARPOP_REQUEST 1
#define ARPOP_REPLY 2

#endif /* _COMPAT_NET_IF_ARP_H_ */
