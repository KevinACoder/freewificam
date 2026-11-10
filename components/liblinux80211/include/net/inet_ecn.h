/* SPDX-License-Identifier: GPL-2.0 */
/*
 * net/inet_ecn.h - the ECN marking helper the codel implementation names,
 * declaration-only face.
 *
 * The pinned header carries the full ECN bit arithmetic for IPv4/IPv6.  The
 * only name the compiled set reaches is INET_ECN_set_ce(), inside
 * codel_impl.h's inline bodies; its body belongs to the port's first ECN
 * consumer (S1c), and a caller that reaches it before then gets an unresolved
 * symbol rather than a silent no-op that would claim a packet was marked.
 */
#ifndef _NET_INET_ECN_H_
#define _NET_INET_ECN_H_

#include "../../linuxkpi_core.h"

struct sk_buff;

bool INET_ECN_set_ce(struct sk_buff *skb);

/* [LNX] include/net/inet_ecn.h:205-219: the DSCP/ECN byte of the packet's IP
 * header.  This port's egress path does not parse IP headers, so the answer is
 * the "not ECN-capable" byte (0); codel's ECN marking is therefore never
 * applied - the conservative answer, and codel's ecn parameter is off by
 * default on this port. */
static inline int skb_get_dsfield(struct sk_buff *skb)
{
	(void)skb;
	return 0;
}

#endif /* _NET_INET_ECN_H_ */
