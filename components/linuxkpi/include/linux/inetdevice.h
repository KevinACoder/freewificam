/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/inetdevice.h
 * @brief   The IPv4 interface-address hooks a driver's address handling registers.
 *
 * @design declaration-only compatibility face. The interface (include/linux/
 *         inetdevice.h) is the IPv4 half of the device registry: the per-interface
 *         address records, the lookup helpers and the address-change notifier chains.
 *         The part the vendored bytes reach is two-fold: the address record an
 *         address-change notifier is handed (the compiled set reads the interface, its
 *         device and its label out of it) and registration against the address-change
 *         chains so the driver can refresh its own addressing state. The records are
 *         transcribed field for field from the interface - `struct in_ifaddr` whole,
 *         because its members are all plain words and the compiled set reads three of
 *         them; `struct in_device` as the pair the compiled set walks through
 *         (`dev`, and the address chain it is the head of), with the interface's
 *         remaining members (the multicast and configuration state) left out because no
 *         compiled unit names one and their types belong to the IPv4 configuration
 *         layer this port does not have. The entry points are the interface's, with no
 *         body, because "the set of interface addresses changed" is an event only a
 *         stack with interfaces can raise - this project presents them through lwIP, and
 *         whether that stack can raise it is the port's answer, so an unregistered
 *         caller gets an unresolved symbol rather than a registration that silently
 *         never fires. The notifier record itself comes from <linux/notifier.h>, which
 *         the interface's own header includes for the same reason. Reference for the
 *         face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_INETDEVICE_H_
#define _LINUXKPI_LINUX_INETDEVICE_H_

#include "../../linuxkpi_core.h"

#include <linux/if.h>
#include <linux/notifier.h>

struct net_device;

/*
 * [LNX] include/linux/inetdevice.h:193-256 (struct in_device), carried as its head: the
 * device it belongs to and the address chain it heads. The interface's record also
 * carries the tracker, the reference count and the multicast/configuration state; none
 * is named by a compiled unit and the types behind them are the IPv4 configuration
 * layer's, so they are not carried.
 */
struct in_device {
	struct net_device	*dev;
	struct in_ifaddr	__rcu *ifa_list;	/* IP ifaddr chain */
};

/* [LNX] include/linux/inetdevice.h:127-152 (struct in_ifaddr), field for field. The
 * record is written by the address layer and read by the notifiers; the compiled set
 * reads the owning device, the address and the label, and the whole record is carried
 * because every member is a plain word and a partial copy of a record the interface
 * hands out whole would be a different record. */
struct in_ifaddr {
	struct hlist_node	addr_lst;
	struct in_ifaddr	__rcu *ifa_next;
	struct in_device	*ifa_dev;
	struct rcu_head		rcu_head;
	__be32			ifa_local;
	__be32			ifa_address;
	__be32			ifa_mask;
	__u32			ifa_rt_priority;
	__be32			ifa_broadcast;
	unsigned char		ifa_scope;
	unsigned char		ifa_prefixlen;
	unsigned char		ifa_proto;
	__u32			ifa_flags;
	char			ifa_label[IFNAMSIZ];

	/* In seconds, relative to tstamp. Expiry is at tstamp + HZ * lft. */
	__u32			ifa_valid_lft;
	__u32			ifa_preferred_lft;
	unsigned long		ifa_cstamp; /* created timestamp */
	unsigned long		ifa_tstamp; /* updated timestamp */
};

/* [LNX] include/linux/inetdevice.h:172-173 */
int register_inetaddr_notifier(struct notifier_block *nb);
int unregister_inetaddr_notifier(struct notifier_block *nb);

#endif /* _LINUXKPI_LINUX_INETDEVICE_H_ */
