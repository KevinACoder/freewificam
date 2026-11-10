/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/if_pppox.h
 * @brief   The PPPoE session-header records a bridge/NAT layer reads and rewrites in a
 *          received frame.
 *
 * @design declaration-only compatibility face. The interface's header (include/linux/
 *         if_pppox.h, over uapi/linux/if_pppox.h) owns the PPPoE/PPTP/L2TP socket
 *         surface; what the compiled set reaches is the on-wire session header and its
 *         tag record, because the vendored bridge extension rewrites the relay tag of a
 *         PPPoE frame in place. The two records are transcribed with the interface's
 *         layout (the flexible tag array is dropped, as the interface drops it for
 *         __KERNEL__) and the bitfield order is the target's, which is little-endian
 *         here. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_LINUX_IF_PPPOX_H_
#define _LINUXKPI_LINUX_IF_PPPOX_H_

#include "../../linuxkpi_core.h"

#include <asm/byteorder.h>

/* [LNX] include/uapi/linux/if_pppox.h:107-111: the PPPoE control/session codes the
 * vendored bridge extension switches on. */
#define	PADI_CODE	0x09
#define	PADO_CODE	0x07
#define	PADR_CODE	0x19
#define	PADS_CODE	0x65
#define	PADT_CODE	0xa7

/* [LNX] include/uapi/linux/if_pppox.h:112-117: the PPPoE tag record. */
struct pppoe_tag {
	__be16 tag_type;
	__be16 tag_len;
} __attribute__((packed));

/* [LNX] include/uapi/linux/if_pppox.h:127: the relay-session-id tag identifier, a
 * big-endian wire value. */
#define	PTT_RELAY_SID	__constant_htons(0x0110)

/* [LNX] include/uapi/linux/if_pppox.h:132-146: the PPPoE session header. The bitfield
 * order is selected from the target's endianness; this port is little-endian, so the
 * little-endian arm is the one stated. The tag array is excluded, as the interface
 * excludes it under __KERNEL__. */
struct pppoe_hdr {
	__u8 type:4;
	__u8 ver:4;
	__u8 code;
	__be16 sid;
	__be16 length;
} __packed;

/* [LNX] include/uapi/linux/if_pppox.h:151 */
#define	PPPOE_SES_HLEN	8

#endif /* _LINUXKPI_LINUX_IF_PPPOX_H_ */
