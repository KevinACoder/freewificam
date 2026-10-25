/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/in6.h
 * @brief   The IPv6 address record and protocol numbers a driver's offload records
 *          reach through this name.
 *
 * @design declaration-only compatibility face. The interface's header carries the
 *         128-bit address record and the IPv6 protocol numbers; this face states
 *         the address record in linux/ipv6.h (its home for the IPv6 wire
 *         vocabulary) and the protocol numbers in linux/in.h, so this header is
 *         the include-path spelling of that set rather than a second copy.
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_IN6_H_
#define _LINUXKPI_LINUX_IN6_H_

#include "../../linuxkpi_core.h"
#include <linux/ipv6.h>
#include <linux/in.h>

#endif /* _LINUXKPI_LINUX_IN6_H_ */
