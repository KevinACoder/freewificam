/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/atalk.h
 * @brief   The Appletalk AARP record a bridge/NAT layer reads out of a received frame.
 *
 * @design declaration-only compatibility face, carried as a legacy record: the interface
 *         generation this face follows (7.3) has dropped include/linux/atalk.h, but the
 *         vendored bridge extension still includes it by name and casts the AARP payload
 *         to `struct elapaarp`, so the record is carried here with the layout of the last
 *         interface that had it (include/linux/atalk.h, the record and the two constants
 *         the compiled set names: AARP_PA_ALEN and the AARP_HW_TYPE_* pair). Only the
 *         record and those constants are carried; the Appletalk stack entry points the
 *         old header also declared are not, because no compiled unit names them. The
 *         record's layout is the wire layout and is exact - the vendored bytes read its
 *         fields from a received frame. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_LINUX_ATALK_H_
#define _LINUXKPI_LINUX_ATALK_H_

#include "../../linuxkpi_core.h"

#include <linux/if_ether.h>

/* [LNX] include/linux/atalk.h (legacy): the AARP (Appletalk Address Resolution
 * Protocol) record. */
struct elapaarp {
	__be16	hw_type;
#define	AARP_HW_TYPE_ETHERNET		1
#define	AARP_HW_TYPE_TOKENRING		2
	__be16	pa_type;
	__u8	hw_len;
	__u8	pa_len;
#define	AARP_PA_ALEN			4
	__be16	function;
#define	AARP_REQUEST			1
#define	AARP_REPLY			2
#define	AARP_PROBE			3
	__u8	hw_src[ETH_ALEN];
	__u8	pa_src_zero;
	__be16	pa_src_net;
	__u8	pa_src_node;
	__u8	hw_dst[ETH_ALEN];
	__u8	pa_dst_zero;
	__be16	pa_dst_net;
	__u8	pa_dst_node;
} __attribute__((packed));

#endif /* _LINUXKPI_LINUX_ATALK_H_ */
