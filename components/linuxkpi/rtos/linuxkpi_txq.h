/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_txq.h
 * @brief   The software transmit-queue engine's allocation and stack side,
 *          beside the interface entry points it serves.
 *
 * @design the scheduler entry points (ieee80211_schedule_txq, next_txq,
 *         return_txq, txq_schedule_start/end, tx_dequeue) are declared by the
 *         interface face in include/net/mac80211.h, which the driver includes;
 *         this header adds the two sides the interface itself does not name -
 *         who allocates a txq (upstream: sta_info_alloc and
 *         ieee80211_if_add, tx.c:1522) and who puts a frame on one (upstream:
 *         ieee80211_txq_enqueue, tx.c:1464).  Reference for the shape:
 *         components/linuxkpi/linuxkpi_wiphy_work.h:1-18.
 * @date    10.10.2026
 */

#ifndef _LINUXKPI_TXQ_H_
#define _LINUXKPI_TXQ_H_

#include "linuxkpi_core.h"

struct ieee80211_hw;
struct ieee80211_vif;
struct ieee80211_sta;
struct ieee80211_txq;

/*
 * Allocate a txq.  `sta == NULL` is the interface's own queue (tid 0, AC_BE:
 * tx.c:1536-1543); otherwise the queue is one TID of that station, with the
 * access category the 802.1D map gives it (tx.c:1559).  The record carries
 * hw->txq_data_size bytes of zeroed driver-private area after it, the size the
 * driver asked for at ieee80211_alloc_hw (rtw89 core.c:7476).  Returns NULL
 * when the engine's table is full - the interface never fails an interface or
 * station for a missing queue, it just has none.
 */
struct ieee80211_txq *lkpi_txq_alloc(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, struct ieee80211_sta *sta, u8 tid);
void	lkpi_txq_free(struct ieee80211_txq *txq);

/* Teardown sweeps: every queue of that vif (its own and its stations'), or
 * every queue of that station.  Both drop the frames still queued. */
void	lkpi_txq_free_vif(struct ieee80211_vif *vif);
void	lkpi_txq_free_sta(struct ieee80211_sta *sta);

/*
 * The stack side: put a frame on the txq's queue, which is what makes the txq
 * schedulable.  Returns 0, or -EINVAL when the txq is not one this engine
 * allocated (the frame is then the caller's again).
 */
int	lkpi_txq_enqueue(struct ieee80211_txq *txq, struct sk_buff *skb);

/* drop everything still queued on one txq */
void	lkpi_txq_purge(struct ieee80211_txq *txq);

/* how many txqs the engine holds, and the calls it refused for want of a slot */
unsigned int	lkpi_txq_in_use(void);
u32	lkpi_txq_alloc_refusals(void);
/* how many queues the scheduler handed to a driver, and how many calls named a
 * txq this engine never allocated (a frame the caller must take back) */
unsigned long	lkpi_txq_handed_out(void);
unsigned long	lkpi_txq_orphan_calls(void);

#endif /* _LINUXKPI_TXQ_H_ */
