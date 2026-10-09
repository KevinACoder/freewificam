/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/if_arp.h
 * @brief   The ARP hardware-type numbers a wireless interface reports, and the ARP
 *          wire header.
 *
 * @design declaration-only compatibility face. The interface (include/uapi/linux/
 *         if_arp.h) carries the whole ARP/RARP constant set and the wire header; what
 *         the vendored bytes name is the hardware-type number they advertise to the
 *         stack (Ethernet for the data interface, IEEE 802.11 for the monitor-style
 *         ones). Both shapes are transcribed - the numbers with the interface's exact
 *         values, the header field for field - and the subset is grown when a consumer
 *         needs more, never pre-populated with guesses. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_IF_ARP_H_
#define _LINUXKPI_LINUX_IF_ARP_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/uapi/linux/if_arp.h: the hardware types the compiled set names. */
#define	ARPHRD_ETHER			1	/* Ethernet 10Mbps */
#define	ARPHRD_LOOPBACK			772	/* Loopback device */
#define	ARPHRD_IEEE80211		801	/* IEEE 802.11 */
#define	ARPHRD_IEEE80211_PRISM		802	/* IEEE 802.11 + Prism2 header */
#define	ARPHRD_IEEE80211_RADIOTAP	803	/* IEEE 802.11 + radiotap header */
#define	ARPHRD_NONE			0xFFFE	/* zero header length */

/* [LNX] include/uapi/linux/if_arp.h:
 * the fixed part of the ARP/RARP packet header (the variable address block that
 * follows it is not part of the struct upstream either). */
struct arphdr {
	__be16		ar_hrd;		/* format of hardware address */
	__be16		ar_pro;		/* format of protocol address */
	unsigned char	ar_hln;		/* length of hardware address */
	unsigned char	ar_pln;		/* length of protocol address */
	__be16		ar_op;		/* ARP opcode (command) */
};

/* [LNX] include/uapi/linux/if_arp.h:108 */
#define	ARPOP_REPLY	2		/* ARP reply			*/

#endif /* _LINUXKPI_LINUX_IF_ARP_H_ */
