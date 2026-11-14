/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    c2h_inline.h
 * @brief   Run one driver's C2H work at the point the packet arrives.
 *
 * @design The port's mac80211 workqueue is a Normal-priority thread.  The
 *         rtw88 driver defers its firmware-command (C2H) handling onto that
 *         queue; its rate-report handling is then purged after 500 ms when
 *         tcpip (priority 40) has kept the queue from running, and mac80211's
 *         rate control loses the per-frame report.  This seam lets the driver
 *         name its C2H work once (lkpi_c2h_inline_register) and the link-time
 *         --wrap of ieee80211_queue_work runs that one work inline, in the
 *         context that received the packet, instead of deferring it.
 *
 *         It is not a workqueue priority change: every other work (scan,
 *         auth, TX, the driver's own) keeps the mac80211 queue's ordering.
 *         The inline call runs under the driver's own C2H mutex and the skb
 *         queue's lock, so a concurrent queued instance serializes with it.
 */
#ifndef _LINUX80211_C2H_INLINE_H_
#define _LINUX80211_C2H_INLINE_H_

struct work_struct;

/* NULL = no inline seam; the wrapped queue_work passes every work through. */
extern void (*lkpi_c2h_work_func)(struct work_struct *work);

/* Name the one work to inline (the driver's C2H work); NULL clears it. */
void lkpi_c2h_inline_register(void (*func)(struct work_struct *work));

#endif /* _LINUX80211_C2H_INLINE_H_ */
