/* SPDX-License-Identifier: GPL-2.0 */
/*
 * net/pkt_sched.h - include-by-name face.
 *
 * mac80211's wme.c includes this header and names no symbol from it (the port
 * has no queueing discipline; net_egress.c sends synchronously).  Keeping the
 * include resolvable is the whole contract here; the first compiled consumer
 * that names a declaration gets it added, not the pinned qdisc tree.
 */
#ifndef _LINUXKPI_NET_PKT_SCHED_H_
#define _LINUXKPI_NET_PKT_SCHED_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_NET_PKT_SCHED_H_ */
