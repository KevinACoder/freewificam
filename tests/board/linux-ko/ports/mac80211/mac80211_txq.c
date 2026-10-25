/*
 * @file
 * @brief The minimal TXQ engine the iwlwifi arm needs.
 *
 * mac80211 hands a frame to a driver through a TXQ (ieee80211_txq), not through
 * the .tx callback, when the driver has woken it: the driver's wake_tx_queue
 * callback then calls ieee80211_tx_dequeue() until it returns NULL.  iwl_mvm
 * uses that path for management frames too (STA_MMPDU_TXQ): its mgmt queue is
 * allocated lazily by the first wake_tx_queue, and a direct .tx call leaves
 * mvmsta->tid_data[IWL_MAX_TID_COUNT].txq_id invalid ("Invalid TXQ id").
 *
 * The queue storage is deliberately outside struct ieee80211_txq: the driver
 * owns txq->drv_priv, so this side keeps a small table keyed by the txq
 * pointer.  One module, one adapter, a handful of queues (vif data, vif mgmt,
 * 17 station TIDs) - 64 slots are plenty.
 */

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <net/mac80211.h>

#include "wlan_linux.h"

#define WLAN_TXQ_SLOTS 64

struct wlan_txq_slot {
	struct ieee80211_txq *txq;
	struct sk_buff_head q;
	bool used;
};

static struct wlan_txq_slot wlan_txq_slots[WLAN_TXQ_SLOTS];
static DEFINE_SPINLOCK(wlan_txq_lock);

static struct wlan_txq_slot *wlan_txq_slot(struct ieee80211_txq *txq,
    bool create)
{
	struct wlan_txq_slot *free_slot = NULL;
	int i;

	for (i = 0; i < WLAN_TXQ_SLOTS; i++) {
		if (wlan_txq_slots[i].used &&
		    wlan_txq_slots[i].txq == txq) {
			return &wlan_txq_slots[i];
		}
		if (!wlan_txq_slots[i].used && free_slot == NULL) {
			free_slot = &wlan_txq_slots[i];
		}
	}
	if (!create || free_slot == NULL) {
		return NULL;
	}
	free_slot->used = true;
	free_slot->txq = txq;
	skb_queue_head_init(&free_slot->q);
	return free_slot;
}

int wlan_txq_enqueue(struct ieee80211_txq *txq, struct sk_buff *skb)
{
	struct wlan_txq_slot *slot;
	unsigned long flags;

	if (txq == NULL || skb == NULL) {
		return -EINVAL;
	}
	spin_lock_irqsave(&wlan_txq_lock, flags);
	slot = wlan_txq_slot(txq, true);
	if (slot == NULL) {
		spin_unlock_irqrestore(&wlan_txq_lock, flags);
		return -ENOSPC;
	}
	__skb_queue_tail(&slot->q, skb);
	spin_unlock_irqrestore(&wlan_txq_lock, flags);
	return 0;
}

unsigned long wlan_txq_len(struct ieee80211_txq *txq)
{
	struct wlan_txq_slot *slot;
	unsigned long flags, n;

	spin_lock_irqsave(&wlan_txq_lock, flags);
	slot = wlan_txq_slot(txq, false);
	n = slot != NULL ? (unsigned long)skb_queue_len(&slot->q) : 0;
	spin_unlock_irqrestore(&wlan_txq_lock, flags);
	return n;
}

struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq)
{
	struct wlan_txq_slot *slot;
	struct sk_buff *skb = NULL;
	unsigned long flags;

	(void)hw;
	if (txq == NULL) {
		return NULL;
	}
	spin_lock_irqsave(&wlan_txq_lock, flags);
	slot = wlan_txq_slot(txq, false);
	if (slot != NULL) {
		skb = __skb_dequeue(&slot->q);
	}
	spin_unlock_irqrestore(&wlan_txq_lock, flags);
	if (skb != NULL) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (info->control.vif == NULL) {
			info->control.vif = txq->vif;
		}
		/*
		 * The station is not carried in tx_info in this kernel: the
		 * driver's wake_tx_queue path takes it from txq->sta.
		 */
	}
	return skb;
}

void ieee80211_txq_get_depth(struct ieee80211_txq *txq,
    unsigned long *frame_cnt, unsigned long *byte_cnt)
{
	struct wlan_txq_slot *slot;
	struct sk_buff *skb;
	unsigned long flags, frames = 0, bytes = 0;

	spin_lock_irqsave(&wlan_txq_lock, flags);
	slot = wlan_txq_slot(txq, false);
	if (slot != NULL) {
		skb_queue_walk(&slot->q, skb) {
			frames++;
			bytes += skb->len;
		}
	}
	spin_unlock_irqrestore(&wlan_txq_lock, flags);
	if (frame_cnt != NULL) {
		*frame_cnt = frames;
	}
	if (byte_cnt != NULL) {
		*byte_cnt = bytes;
	}
}

/* The scheduler is a no-op: this module has one vif and one peer. */
void __ieee80211_schedule_txq(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq, bool force)
{
	(void)hw;
	(void)txq;
	(void)force;
}

struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, u8 ac)
{
	(void)hw;
	(void)ac;
	return NULL;
}

void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac)
{
	(void)hw;
	(void)ac;
}

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	(void)hw;
}

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	(void)hw;
}

/*
 * mac80211's helper for drivers that do not implement wake_tx_queue: drain
 * the txq and call .tx.  iwlwifi implements wake_tx_queue and calls
 * ieee80211_tx_dequeue itself; this is here for the other arms / closure.
 */
void ieee80211_handle_wake_tx_queue(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	struct sk_buff *skb;

	if (w == NULL || w->ops == NULL || w->ops->tx == NULL) {
		return;
	}
	while ((skb = ieee80211_tx_dequeue(hw, txq)) != NULL) {
		struct ieee80211_tx_control control = {
			.sta = txq->sta,
		};

		w->ops->tx(hw, &control, skb);
	}
}
