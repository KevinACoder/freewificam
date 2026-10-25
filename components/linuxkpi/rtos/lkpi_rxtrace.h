/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_rxtrace.h
 * @brief   The bounded receive boundary trace: the completion side and the
 *          delivery side of one receive path, kept in memory and dumped on
 *          the first anomalous frame.
 *
 * @design bring-up diagnostic, built on the entry points the two sides already
 *         have - the completion record at the point where the drain thread
 *         publishes a transfer's status and length (linuxkpi_usb.c,
 *         lkpi_usb_drain_one), and the delivery record at the single point
 *         every driver-delivered frame crosses (linuxkpi_80211_helpers.c,
 *         ieee80211_rx_napi).  Both are called unconditionally by the board
 *         images and compile away everywhere else (CONFIG_LKPI_RXTRACE).
 *
 * Why: a receive path that hands up malformed frames has two candidate owners
 * - the transport completed a transfer whose length or content was wrong, or
 * it completed a correct transfer the driver's aggregate/descriptor parse then
 * walked off.  One snapshot of both sides at the same moment names the owner:
 * a completion record with a sane length and head whose delivery record is
 * anomalous puts the parse on the driver side; both anomalous puts it on the
 * transport side.
 * @date    10.10.2026
 */

#ifndef LKPI_RXTRACE_H
#define LKPI_RXTRACE_H

#include "linuxkpi_core.h"

/*
 * Record one completed transfer (drain-thread context).  `status` is the
 * published urb status (0 = success), `len` its actual_length, `data` the
 * buffer those bytes came from (the first bytes are copied).  A non-zero
 * status records too: an errored receive completion is itself a finding.
 */
#ifdef CONFIG_LKPI_RXTRACE
void	lkpi_rxtrace_completion(unsigned int ep_addr, int status, u32 len,
	    const void *data);
void	lkpi_rxtrace_delivery(struct sk_buff *skb);
void	lkpi_rxtrace_stats(u32 *completions, u32 *deliveries,
	    u32 *anomalies, u32 *shorts, u32 *dumps);
/*
 * The first delivery-side anomaly, kept for the ledger print so the decisive
 * datum survives even if the console ring drops the live dump: `len` what was
 * handed up, `ptr` the skb, `st` its fcs_bad flag, `head` its first 16 bytes.
 * Returns 0 when no delivery anomaly happened.
 */
int	lkpi_rxtrace_first_anom(u32 *len, u32 *ptr, int *st, u8 head[16]);
#else
static inline void
lkpi_rxtrace_completion(unsigned int ep_addr, int status, u32 len,
    const void *data)
{

	(void)ep_addr;
	(void)status;
	(void)len;
	(void)data;
}
static inline void
lkpi_rxtrace_delivery(struct sk_buff *skb)
{

	(void)skb;
}
static inline void
lkpi_rxtrace_stats(u32 *completions, u32 *deliveries, u32 *anomalies,
    u32 *shorts, u32 *dumps)
{

	if (completions != NULL)
		*completions = 0;
	if (deliveries != NULL)
		*deliveries = 0;
	if (anomalies != NULL)
		*anomalies = 0;
	if (shorts != NULL)
		*shorts = 0;
	if (dumps != NULL)
		*dumps = 0;
}
static inline int
lkpi_rxtrace_first_anom(u32 *len, u32 *ptr, int *st, u8 head[16])
{

	(void)len;
	(void)ptr;
	(void)st;
	(void)head;
	return 0;
}
#endif /* CONFIG_LKPI_RXTRACE */

#endif /* LKPI_RXTRACE_H */
