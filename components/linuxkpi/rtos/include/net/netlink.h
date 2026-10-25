/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/netlink.h
 * @brief   The attribute-builder helpers a vendor-event layer uses to lay out a netlink
 *          attribute stream.
 *
 * @design declaration-only compatibility face. The interface's header (include/net/
 *         netlink.h) owns the whole netlink attribute vocabulary - the record, the
 *         alignment arithmetic and the put/nest builders. What the compiled set names is
 *         the record, the length constant and the handful of builders a vendor event is
 *         assembled with; those are declared here with the interface's signatures and
 *         are declarations only, because the bodies are the port's when it has a netlink
 *         transport to write into (an unresolved symbol reports that, rather than a
 *         stream built into a buffer the port does not own). The values are the
 *         interface's. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_NET_NETLINK_H_
#define _LINUXKPI_NET_NETLINK_H_

#include "../../linuxkpi_core.h"

#include <linux/skbuff.h>

/* [LNX] include/uapi/linux/netlink.h:229-232: the attribute record. */
struct nlattr {
	__u16	nla_len;
	__u16	nla_type;
};

/* [LNX] include/uapi/linux/netlink.h:244-249: the attribute flag bits and the
 * alignment arithmetic. */
#define	NLA_F_NESTED		(1 << 15)
#define	NLA_F_NET_BYTEORDER	(1 << 14)
#define	NLA_TYPE_MASK		~(NLA_F_NESTED | NLA_F_NET_BYTEORDER)

#define	NLA_ALIGNTO		4
#define	NLA_ALIGN(len)		(((len) + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1))
#define	NLA_HDRLEN		((int) NLA_ALIGN(sizeof(struct nlattr)))

/* [LNX] include/net/netlink.h:567-570,1389,1455,2246-2261: the builders the compiled
 * set names. Declared as the interface declares them. */
int nla_put(struct sk_buff *skb, int attrtype, int attrlen, const void *data);
int nla_put_nohdr(struct sk_buff *skb, int attrlen, const void *data);
int nla_put_u8(struct sk_buff *skb, int attrtype, u8 value);
int nla_put_u32(struct sk_buff *skb, int attrtype, u32 value);
struct nlattr *nla_nest_start(struct sk_buff *skb, int attrtype);
int nla_nest_end(struct sk_buff *skb, struct nlattr *start);

#endif /* _LINUXKPI_NET_NETLINK_H_ */
