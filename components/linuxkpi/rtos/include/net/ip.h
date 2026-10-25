/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/ip.h
 * @brief   The include a driver's bridge/NAT layer names for the IPv4 helpers; the wire
 *          header it needs is the face's linux/ip.h.
 *
 * @design declaration-only compatibility face. The interface's header (include/net/ip.h)
 *         declares the IPv4 stack entry points (input/output, routing, options) over the
 *         wire header in include/linux/ip.h. The compiled set reaches only the wire
 *         header through this name - the vendored bridge extension casts a received
 *         buffer to `struct iphdr` and reads its addresses itself, calling nothing from
 *         the IPv4 stack - so this face carries the interface's include of the wire
 *         header and no entry points: a caller that needs one arrives with its
 *         declaration and the interface's exact shape, where a stub here would invent a
 *         stack this image does not have. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_NET_IP_H_
#define _LINUXKPI_NET_IP_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/net/ip.h: includes <linux/ip.h> for the wire header. */
#include <linux/ip.h>

#endif /* _LINUXKPI_NET_IP_H_ */
