/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    uapi/linux/if_arp.h
 * @brief   The ARP hardware-type numbers and the ARP wire header, at the name
 *          the network drivers include.
 *
 * @design declaration-only compatibility face. The interface splits the ARP
 *         vocabulary in two files: the uapi half (include/uapi/linux/if_arp.h)
 *         carries the hardware-type numbers and the fixed part of the wire
 *         header, and the kernel half (include/linux/if_arp.h) includes it and
 *         adds the in-kernel pieces. This port carries the numbers and the
 *         header once, here, because both spellings are named by compiled
 *         units: mac80211/cfg80211 include <linux/if_arp.h> and brcmfmac's
 *         cfg80211.c:15 includes <uapi/linux/if_arp.h>. The kernel half is the
 *         include of this file, which is the interface's own shape, so a name
 *         has one definition whichever spelling reaches it. Values are the
 *         interface's; the subset grows when a consumer names another number,
 *         never pre-populated with guesses.
 * @date    19.11.2026
 */

#ifndef _UAPI_LINUX_IF_ARP_H
#define _UAPI_LINUX_IF_ARP_H

#include <linux/types.h>

/* [LNX] include/uapi/linux/if_arp.h:31-104, the ARPHRD_* hardware types the
 * compiled set names. */
#define	ARPHRD_ETHER			1	/* Ethernet 10Mbps */
#define	ARPHRD_LOOPBACK			772	/* Loopback device */
#define	ARPHRD_IEEE80211		801	/* IEEE 802.11 */
#define	ARPHRD_IEEE80211_PRISM		802	/* IEEE 802.11 + Prism2 header */
#define	ARPHRD_IEEE80211_RADIOTAP	803	/* IEEE 802.11 + radiotap header */
#define	ARPHRD_NONE			0xFFFE	/* zero header length */

/* [LNX] include/uapi/linux/if_arp.h: the fixed part of the ARP/RARP packet
 * header (the variable address block that follows it is not part of the struct
 * upstream either). */
struct arphdr {
	__be16		ar_hrd;		/* format of hardware address */
	__be16		ar_pro;		/* format of protocol address */
	unsigned char	ar_hln;		/* length of hardware address */
	unsigned char	ar_pln;		/* length of protocol address */
	__be16		ar_op;		/* ARP opcode (command) */
};

/* [LNX] include/uapi/linux/if_arp.h:108-113, the ARP opcodes the compiled set
 * names (the ARP reply the 4-address path rewrites). */
#define	ARPOP_REQUEST	1		/* ARP request			*/
#define	ARPOP_REPLY	2		/* ARP reply			*/

#endif /* _UAPI_LINUX_IF_ARP_H */
