/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/rtnetlink.h
 * @brief   The include a driver's configuration layer names for the routing-netlink
 *          surface; the part it reaches is the global configuration lock, already stated
 *          by the face's linux/rtnetlink.h.
 *
 * @design declaration-only compatibility face. The interface's header (include/net/
 *         rtnetlink.h) builds the routing-netlink messages and walks the link registry;
 *         it is part of the vendored driver's fixed include set, so the header must exist
 *         under this name, but no translation unit of the compiled set names any symbol
 *         of it - the vendored vendor-event layer takes the name for the configuration
 *         side it does not reach on this port. The face therefore carries the interface's
 *         include of the global lock header (linux/rtnetlink.h) and nothing else: the
 *         first caller that needs a message builder arrives with its declaration and the
 *         interface's exact shape. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_NET_RTNETLINK_H_
#define _LINUXKPI_NET_RTNETLINK_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/net/rtnetlink.h: includes <linux/rtnetlink.h> for the global lock and
 * <net/netlink.h> for the attribute builders a link message is laid out with. */
#include <linux/rtnetlink.h>
#include <net/netlink.h>

#endif /* _LINUXKPI_NET_RTNETLINK_H_ */
