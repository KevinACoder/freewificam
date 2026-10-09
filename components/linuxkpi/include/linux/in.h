/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/in.h
 * @brief   The IP protocol numbers the wire parsers compare against.
 *
 * @design declaration-only compatibility face: the values are the assigned protocol
 *         numbers (uapi/linux/in.h, an anonymous enumeration there) and are contract with
 *         every packet a driver inspects or builds; the socket-address records the
 *         interface also carries arrive with the consumer that names them.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_IN_H_
#define _LINUXKPI_LINUX_IN_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/uapi/linux/in.h:32,38,44 (the enumerators the compiled set names) */
enum {
	IPPROTO_ICMP = 1,
	IPPROTO_TCP = 6,
	IPPROTO_UDP = 17,
};

#endif /* _LINUXKPI_LINUX_IN_H_ */
