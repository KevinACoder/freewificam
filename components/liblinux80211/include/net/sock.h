/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * net/sock.h - the socket record and the helpers the imported stack names,
 * used-surface face.
 *
 * The pinned header is the whole AF_INET socket layer (3184 lines, twenty-odd
 * kernel subsystems behind it).  This port has no socket subsystem and the
 * stack names exactly five helpers: sock_net(), net_eq() (net_namespace.h),
 * sk_requests_wifi_status(), sk_pacing_shift_update() and sock_release()
 * (linux/net.h).  So this face carries the record those helpers read and the
 * answers a port without sockets must give:
 *
 *   - sk_requests_wifi_status() is false: nobody sets SOCK_WIFI_STATUS here;
 *   - sk_pacing_shift_update() stores into the record (the field is the
 *     interface's) so a repeated update is a no-op;
 *   - sock_net() reads the record's namespace tag, which the netdev/wiphy
 *     faces already carry as possible_net_t.
 *
 * net_eq() lives in net_namespace.h, where the interface declares it.
 */
#ifndef _LINUXKPI_NET_SOCK_H_
#define _LINUXKPI_NET_SOCK_H_

#include "../../linuxkpi_core.h"
#include <net/net_namespace.h>

/* [LNX] include/net/sock.h:365-...: the record.  Only the members the
 * helpers below read are carried; the interface's TCP/queue/accounting blocks
 * have no counterpart on this port and no compiled unit names them. */
struct sock {
	struct net	*sk_net;	/* namespace tag (possible_net_t arm) */
	unsigned long	sk_flags;	/* sock_flag() word */
	u8		sk_pacing_shift;	/* TCP small-queues scaling */
};

/* [LNX] include/net/sock.h: enum sock_flags, the one bit the compiled set names. */
enum sock_flags {
	SOCK_WIFI_STATUS,		/* wifi status reporting requested */
};

/* [LNX] include/net/sock.h:711: `return sk->sk_net;` (the CONFIG_NET_NS off arm). */
static inline struct net *sock_net(const struct sock *sk)
{
	return sk->sk_net;
}

/* [LNX] include/net/sock.h: `sock_flag(sk, flag)` tests the flag word. */
static inline bool sock_flag(const struct sock *sk, enum sock_flags flag)
{
	return !!(sk->sk_flags & (1ul << flag));
}

/* [LNX] include/net/sock.h:3041-3044: `sk && sk_fullsock(sk) &&
 * sock_flag(sk, SOCK_WIFI_STATUS)`.  This port has no socket states, so the
 * full-socket test the interface applies is the record's existence. */
static inline bool sk_requests_wifi_status(struct sock *sk)
{
	return sk != NULL && sock_flag(sk, SOCK_WIFI_STATUS);
}

/* [LNX] include/net/sock.h:3116-3121: store the pacing shift when it changed. */
static inline void sk_pacing_shift_update(struct sock *sk, int val)
{
	if (sk == NULL || sk->sk_pacing_shift == (u8)val)
		return;
	sk->sk_pacing_shift = (u8)val;
}

#endif /* _LINUXKPI_NET_SOCK_H_ */
