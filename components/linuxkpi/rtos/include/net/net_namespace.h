/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/net_namespace.h
 * @brief   The network-namespace tag and the one global instance the compiled set names.
 *
 * @design declaration-only compatibility face. The interface's header (include/net/
 *         net_namespace.h) owns the per-namespace registry and its bookkeeping; the only
 *         part the compiled set reaches is the initial namespace object a device lookup
 *         is anchored at (a driver asks its registry for a bridge device by name). The
 *         namespace is an opaque tag here - this port presents its interfaces through
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

#endif /* _LINUXKPI_NET_NET_NAMESPACE_H_ */
