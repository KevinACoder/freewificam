/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/net.h
 * @brief   The include cfg80211 names for the socket vocabulary; this port
 *          carries no socket transport.
 *
 * @design declaration-only compatibility face.  The interface's linux/net.h is
 *         the kernel networking layer's master header: socket construction,
 *         sendmsg/recvmsg, sockptr_t, the poll/tap records.  The compiled
 *         wireless stack reaches exactly one operation through it - the
 *         release of a socket a cfg80211 connect trigger created (struct
 *         socket sits in cfg80211's TCP trigger record and the WOWLAN config)
 *         - plus the address-family/type constants the same sources test.  The
 *         socket itself is created by the egress/control plane, not by this
 *         component: the port has no kernel socket layer, so sock_release() is
 *         a body for S1 to supply (the object is only ever released, never
 *         dereferenced by the stack).  A caller that needs socket creation or
 *         I/O through this header is the falsifier for this face.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_NET_H_
#define _LINUXKPI_LINUX_NET_H_

#include "../../linuxkpi_core.h"

struct socket;

/* [LNX] include/uapi/linux/net.h:33-40: the socket types. */
#define	SOCK_STREAM	1
#define	SOCK_DGRAM	2
#define	SOCK_RAW	3

/* [LNX] include/uapi/linux/socket.h: the address families the sources test.
 * PF_* and AF_* share the values on Linux; both spellings are stated because
 * cfg80211 and the netlink uapi use one each. */
#define	PF_UNSPEC	0
#define	PF_INET		2
#define	AF_UNSPEC	PF_UNSPEC
#define	AF_INET		PF_INET
#define	AF_PACKET	17
#define	PF_PACKET	AF_PACKET
#define	AF_NETLINK	16
#define	PF_NETLINK	AF_NETLINK

/* [LNX] include/linux/net.h:290-291: `int sock_release(struct socket *sock)`.
 * The body is S1's; the stack only releases an already-created socket. */
int	sock_release(struct socket *sock);

#endif /* _LINUXKPI_LINUX_NET_H_ */
