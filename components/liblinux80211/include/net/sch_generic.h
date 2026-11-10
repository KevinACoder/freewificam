/* SPDX-License-Identifier: GPL-2.0 */
/*
 * net/sch_generic.h - the one queueing-discipline answer the stack names,
 * used-surface face.
 *
 * mac80211's scan state machine asks qdisc_all_tx_empty(dev) once, to know
 * whether the device's transmit queues have drained.  This port has no qdisc
 * layer at all: the egress path sends synchronously (components/liblinux80211/
 * net_egress.c), so no queue exists that could hold a frame, and the answer is
 * "empty" by construction.  Carrying the pinned 1543-line header would pull
 * rwsem/hashtable/xarray/flow_offload/gen_stats/dropreason into the build for
 * that one call; this face states the answer instead and the host's net_stubs
 * case pins it.
 */
#ifndef _LINUXKPI_NET_SCH_GENERIC_H_
#define _LINUXKPI_NET_SCH_GENERIC_H_

#include <linux/netdevice.h>

/* [LNX] include/net/sch_generic.h:850-865: the interface walks every tx queue
 * and tests its qdisc.  There is no qdisc on this port, so the walk's range is
 * empty and the interface's own answer for an empty range is true. */
static inline bool qdisc_all_tx_empty(const struct net_device *dev)
{
	(void)dev;
	return true;
}

#endif /* _LINUXKPI_NET_SCH_GENERIC_H_ */
