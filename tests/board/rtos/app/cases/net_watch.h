/* SPDX-License-Identifier: Apache-2.0 */
/*
 * @file   net_watch.h
 * @brief  The wedge ledger's API (observation image only, NET_WATCH).
 *
 * The 8188EU netio arm freezes between one and five seconds into a TCP cell
 * while the scheduler keeps running.  The earlier round had a heartbeat but no
 * layer boundaries, so the stall could not be placed.  net_watch is that
 * placement: a watchdog thread samples the layers a frame crosses and prints
 * only scalars, so the first layer that stops advancing names the fault.
 *
 * Everything it reads is a plain counter or gauge owned by the layer it
 * describes; nothing here takes, releases or blocks on a lock, and no path the
 * data plane uses is changed by being observed (the two places that had to grow
 * a counter say so in their own comments and are compiled only with this
 * lever).  The prints are console anchors, so in a healthy boot they interleave
 * with the case's own lines and the run's timeline stays one stream.
 */

#ifndef NET_WATCH_H
#define NET_WATCH_H

/*
 * Start the watchdog.  Idempotent; call once the line is up (after the netif
 * has an address) so a watch thread never races the bring-up.
 */
void net_watch_start(void);

/*
 * A NETIO cell is in flight (1) or finished (0).  While one is in flight an
 * unchanged set of counters is a stall; with none in flight a quiet link is
 * normal and nothing is reported.
 */
void net_watch_session(int active);

#endif /* NET_WATCH_H */
