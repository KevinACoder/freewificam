/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/tcp.h
 * @brief   The TCP surface a driver's transmit path reaches through this name.
 *
 * @design declaration-only compatibility face. The interface's header
 *         (include/net/tcp.h) carries the socket-layer TCP API; the wire header and
 *         the transport-header accessors a driver's transmit path names live in the
 *         interface's linux/tcp.h, which this face states under that name. This
 *         header is the include-path spelling of that surface rather than a second
 *         copy: a consumer that includes only net/tcp.h reaches the same
 *         declarations. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_NET_TCP_H_
#define _LINUXKPI_NET_TCP_H_

#include "../../linuxkpi_core.h"
#include <linux/tcp.h>

#endif /* _LINUXKPI_NET_TCP_H_ */
