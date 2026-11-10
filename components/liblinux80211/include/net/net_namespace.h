/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/net_namespace.h
 * @brief   The network-namespace tag and the one global instance the compiled set names.
 *
 * @design declaration-only compatibility face. The interface's header (include/net/
 *         net_namespace.h) owns the per-namespace registry and its bookkeeping; the only
 *         part the compiled set reaches is the initial namespace object a device lookup
 *         is anchored at and the possible_net_t tag the wiphy/genl/sock records carry.
 *         The namespace is an opaque tag here - this port presents its interfaces through
 *         lwIP rather than through the Linux netns machinery - and the object is a
 *         declaration only: its definition and its meaning are the port's, so a caller
 *         that needs it gets an unresolved symbol rather than a stub pretending the
 *         namespace exists. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_NET_NET_NAMESPACE_H_
#define _LINUXKPI_NET_NET_NAMESPACE_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/net/net_namespace.h: `struct net`; an opaque tag for the face, since
 * nothing here reads a member of it. */
struct net;

/* [LNX] include/net/net_namespace.h:38: `extern struct net init_net;` - the initial
 * namespace the interface's device lookups default to. */
extern struct net init_net;

/* [LNX] include/net/net_namespace.h: with CONFIG_NET_NS off the interface's
 * possible_net_t is the bare pointer and read_pnet/write_pnet are the identity;
 * this port has one namespace and no registry, so that is the arm it carries.
 * The real include/net/cfg80211.h, genetlink.h and sock.h name the pair. */
typedef struct net *possible_net_t;
#define	read_pnet(pnet)		(*(pnet))
#define	write_pnet(pnet, net)	do { *(pnet) = (net); } while (0)

#endif /* _LINUXKPI_NET_NET_NAMESPACE_H_ */
