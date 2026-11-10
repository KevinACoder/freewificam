/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * net/inet_connection_sock.h - the INET connection helpers nl80211.c names,
 * used-surface face.
 *
 * The pinned header is the TCP connection layer; nl80211.c includes it for the
 * WoWLAN TCP-trigger path, which asks for a local port and reads the bound
 * port number:
 *
 *	inet_csk_get_port(cfg->sock->sk, port)
 *	inet_sk(cfg->sock->sk)->inet_num
 *
 * This port has no TCP stack: the declarations below let the path compile,
 * and a caller that reaches them at run time gets an unresolved symbol rather
 * than a stub pretending a socket was bound.  struct inet_sock's first member
 * is the sock, which is what makes the interface's container_of() spelling the
 * plain cast here.
 */
#ifndef _INET_CONNECTION_SOCK_H
#define _INET_CONNECTION_SOCK_H

#include "./sock.h"
#include <linux/net.h>
#include <linux/container_of.h>

/* [LNX] include/net/inet_sock.h: the record inet_sk() projects.  Only the
 * members the compiled set reads are carried (the interface's options,
 * cork/timestamp state and address block have no consumer here). */
struct inet_sock {
	struct sock		sk;		/* must stay first: inet_sk() is a container_of */
	unsigned short		inet_num;	/* bound local port */
};

/* [LNX] include/net/inet_sock.h: `container_of(sk, struct inet_sock, sk)`. */
static inline struct inet_sock *inet_sk(const struct sock *sk)
{
	return container_of(sk, struct inet_sock, sk);
}

/* [LNX] include/net/inet_connection_sock.h: the bind-port probe nl80211 uses
 * before it creates the trigger socket.  Body: the port's (S1c). */
int inet_csk_get_port(struct sock *sk, unsigned short snum);

#endif /* _INET_CONNECTION_SOCK_H */
