/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/if_packet.h
 * @brief   The packet-class vocabulary a receive path stamps onto an sk_buff.
 *
 * @design declaration-only compatibility face. The interface's header (include/linux/
 *         if_packet.h, over uapi/linux/if_packet.h) owns the packet-socket surface and
 *         the receive classification; what the compiled set reaches is the packet-type
 *         tag a driver writes into `skb->pkt_type` when it hands a frame upward. The
 *         values are the interface's wire/classification values and are carried whole
 *         (the interface itself notes two of them alias), because a partial copy of a
 *         classification space is an invitation to a wrong tag. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_LINUX_IF_PACKET_H_
#define _LINUXKPI_LINUX_IF_PACKET_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/uapi/linux/if_packet.h:26-36 */
#define	PACKET_HOST		0		/* To us		*/
#define	PACKET_BROADCAST	1		/* To all		*/
#define	PACKET_MULTICAST	2		/* To group		*/
#define	PACKET_OTHERHOST	3		/* To someone else 	*/
#define	PACKET_OUTGOING		4		/* Outgoing of any type */
#define	PACKET_LOOPBACK		5		/* MC/BRD frame looped back */
#define	PACKET_USER		6		/* To user space	*/
#define	PACKET_KERNEL		7		/* To kernel space	*/
#define	PACKET_FASTROUTE	6		/* Fastrouted frame	*/

#endif /* _LINUXKPI_LINUX_IF_PACKET_H_ */
