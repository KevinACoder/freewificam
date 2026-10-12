/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/ndisc.h
 * @brief   The ICMPv6 neighbour-discovery message codes a bridge/NAT layer switches on.
 *
 * @design declaration-only compatibility face. The interface's header (include/net/
 *         ndisc.h) owns the neighbour-discovery machinery - the option parsing, the
 *         neighbour table and the send entry points. What the compiled set reaches is
 *         the message-code enumeration the vendored bridge extension switches on when it
 *         rewrites a link-layer address; the codes are the ICMPv6 type values and are
 *         carried with the interface's names and values. The entry points arrive with a
 *         caller. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_NET_NDISC_H_
#define _LINUXKPI_NET_NDISC_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/net/ndisc.h:9-13: the ICMPv6 codes for neighbour-discovery messages. */
#define	NDISC_ROUTER_SOLICITATION	133
#define	NDISC_ROUTER_ADVERTISEMENT	134
#define	NDISC_NEIGHBOUR_SOLICITATION	135
#define	NDISC_NEIGHBOUR_ADVERTISEMENT	136
#define	NDISC_REDIRECT			137

#endif /* _LINUXKPI_NET_NDISC_H_ */
