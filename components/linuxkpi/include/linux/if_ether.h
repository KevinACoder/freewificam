/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/if_ether.h
 * @brief   The Ethernet constants and the wire header, as the Linux-family drivers
 *          name them.
 * @design declaration-only compatibility face, the same shape as this component's
 *         linux/usb.h (components/linuxkpi/include/linux/usb.h:1-20): the Linux names
 *         are declared first-party and the values are transcribed from the interface
 *         definition (include/uapi/linux/if_ether.h), never copied as text. Subset:
 *         grown by the consumer, so only what the compiled drivers and this project's
 *         own code reach for is here.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_IF_ETHER_H_
#define _LINUXKPI_LINUX_IF_ETHER_H_

#include "../../linuxkpi_core.h"

/* ------------------------------------------------------------------ */
/* sizes                                                               */
/* ------------------------------------------------------------------ */

#define	ETH_ALEN		6		/* octets in an Ethernet address */
#define	ETH_HLEN		14		/* octets in the Ethernet header */
#define	ETH_ZLEN		60		/* minimum frame, without the FCS */
#define	ETH_DATA_LEN		1500		/* maximum payload */
#define	ETH_FRAME_LEN		1514		/* maximum frame, without the FCS */
#define	ETH_FCS_LEN		4		/* octets in the FCS */

/* ------------------------------------------------------------------ */
/* EtherTypes                                                          */
/* ------------------------------------------------------------------ */

#define	ETH_P_LOOP		0x0060
#define	ETH_P_PUP		0x0200
#define	ETH_P_IP		0x0800
#define	ETH_P_ARP		0x0806
#define	ETH_P_IPV6		0x86DD
#define	ETH_P_PAE		0x888E		/* EAPOL over Ethernet */

/* ------------------------------------------------------------------ */
/* the wire header                                                     */
/* ------------------------------------------------------------------ */

struct ethhdr {
	unsigned char	h_dest[ETH_ALEN];
	unsigned char	h_source[ETH_ALEN];
	__be16		h_proto;
} __packed;

#endif /* _LINUXKPI_LINUX_IF_ETHER_H_ */