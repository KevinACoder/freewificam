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

/*
 * [LNX] include/net/net_namespace.h: `struct net`.  This port has one namespace
 * and no registry; the record carries only what the compiled set reads - the
 * generic-netlink socket the genlmsg_* inlines send on (net/genetlink.h's
 * genlmsg_multicast_netns_filtered() and friends dereference it).
 */
struct sock;
struct user_namespace;
struct net {
	struct sock	*genl_sock;
	struct user_namespace *user_ns;
	/* [LNX] include/net/net_namespace.h: the namespace identity record the
	 * sysfs class returns through to_ns_common(). */
	struct ns_common ns;
};

/* [LNX] include/net/net_namespace.h: `return &net->ns;`. */
static inline struct ns_common *to_ns_common(struct net *net)
{
	return &net->ns;
}

/* [LNX] include/net/net_namespace.h:38: `extern struct net init_net;` - the initial
 * namespace the interface's device lookups default to. */
extern struct net init_net;

/* [LNX] include/net/net_namespace.h: the pid-to-namespace lookup nl80211's
 * wiphy-netns command uses; the body is the port's (S1c). */
/* [LNX] include/net/net_namespace.h: one namespace, no reference count. */
#define	get_net(net)		(net)
#define	put_net(net)		do { (void)(net); } while (0)

struct net *get_net_ns_by_pid(pid_t pid);
struct net *get_net_ns_by_fd(int fd);
int dev_change_net_namespace(struct net_device *dev, struct net *net,
			     const char *pat);

/* [LNX] include/net/net_namespace.h: with CONFIG_NET_NS off the interface's
 * possible_net_t is the bare pointer and read_pnet/write_pnet are the identity;
 * this port has one namespace and no registry, so that is the arm it carries.
 * The real include/net/cfg80211.h, genetlink.h and sock.h name the pair. */
/* [LNX] include/net/net_namespace.h: the namespace identity test.  One
 * namespace on this port, so the interface's pointer comparison is the whole
 * answer; nl80211.c compares wiphy_net(wiphy) against sock_net(sk). */
static inline bool net_eq(const struct net *net1, const struct net *net2)
{
	return net1 == net2;
}

typedef struct net *possible_net_t;
#define	read_pnet(pnet)		(*(pnet))
#define	write_pnet(pnet, net)	do { *(pnet) = (net); } while (0)

#endif /* _LINUXKPI_NET_NET_NAMESPACE_H_ */
