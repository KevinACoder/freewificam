/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/scm.h
 * @brief   The SCM credential record netlink's skb control block carries.
 *
 * @design declaration-only compatibility face.  The interface's net/scm.h owns
 *         the socket control-message machinery (file-descriptor passing, the
 *         scm_fp_list record, the SCM_RIGHTS path).  This port has no Unix
 *         sockets and no receive side, so the only name the compiled stack
 *         reaches through that header is the credential record netlink puts in
 *         an skb's control block (include/linux/netlink.h: struct
 *         netlink_skb_parms).  It is stated here with the interface's fields;
 *         the descriptor-passing surface is deliberately absent, and a caller
 *         that needs it is the falsifier for this face.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_NET_SCM_H_
#define _LINUXKPI_NET_SCM_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/net/scm.h:13-17: who sent the control message.  The port's
 * kernel runs in one context and has no uid/gid model, so the fields are the
 * interface's shapes sized like a 32-bit kernel's kuid_t/kgid_t (an int each),
 * which is what makes the structure's size match the interface's on the same
 * ABI. */
typedef unsigned int	kuid_t;
typedef unsigned int	kgid_t;

struct scm_creds {
	kuid_t	uid;
	kgid_t	gid;
};

#endif /* _LINUXKPI_NET_SCM_H_ */
