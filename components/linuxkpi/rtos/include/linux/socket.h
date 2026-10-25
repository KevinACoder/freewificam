/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/socket.h
 * @brief   The generic socket address record, which the wireless request records embed.
 *
 * @design declaration-only compatibility face, grown per consumer. The interface
 *         (include/linux/socket.h) is the kernel's socket-address vocabulary: the family
 *         size type, the generic record, and the conversion helpers. The
 *         wireless-extension records this face states embed the generic record, so it
 *         is transcribed with the interface's field names and types - the family as the
 *         two-byte type the interface's uapi header defines it as. The socket layer
 *         itself is not on either side of this interface here (this project's networking
 *         is the port's), so nothing beyond the record is carried. Reference for the face
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SOCKET_H_
#define _LINUXKPI_LINUX_SOCKET_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/uapi/linux/socket.h:10: typedef unsigned short __kernel_sa_family_t; */
typedef unsigned short	sa_family_t;

/* [LNX] include/linux/socket.h:36-39 */
struct sockaddr {
	sa_family_t	sa_family;	/* address family, AF_xxx */
	char		sa_data[14];	/* 14 bytes of protocol address */
};

#endif /* _LINUXKPI_LINUX_SOCKET_H_ */
