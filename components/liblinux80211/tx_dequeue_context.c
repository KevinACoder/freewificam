/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    tx_dequeue_context.c
 * @brief   The port's answer to mac80211's "dequeue runs in softirq" contract.
 *
 * ieee80211_tx_dequeue() starts with WARN_ON_ONCE(softirq_count() == 0).  On
 * Linux the driver's iTXQ wake path is reached from softirq context; here it is
 * reached from iwlwifi's own work/IRQ context (iwl_mvm_mac_wake_tx_queue ->
 * iwl_mvm_mac_itxq_xmit), which is a deferred transmit context but not a
 * tasklet.  lkpi_in_softirq() therefore reads lkpi_tx_dequeue_depth, and this
 * shim raises it around the real dequeue.
 *
 * The wrap is a link seam, not a source edit: the stack keeps its WARN (it is
 * right on Linux), and the port states what its context is.  The same shape as
 * the NAPI poll depth in linuxkpi_netdev.c.
 */

#include "linuxkpi_core.h"

#include <net/mac80211.h>

extern unsigned int lkpi_tx_dequeue_depth;

struct sk_buff *__real_ieee80211_tx_dequeue(struct ieee80211_hw *hw,
					    struct ieee80211_txq *txq);

struct sk_buff *
__wrap_ieee80211_tx_dequeue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	struct sk_buff *skb;

	lkpi_tx_dequeue_depth++;
	skb = __real_ieee80211_tx_dequeue(hw, txq);
	lkpi_tx_dequeue_depth--;
	return skb;
}
