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
#include <linux/workqueue.h>
#include <net/mac80211.h>

#include "wlan_linux.h"

#define WLAN_TXQ_SLOTS 64

/*
 * The transmit-queue stop/wake pair.  mac80211's real ieee80211_stop_queues /
 * ieee80211_wake_queues drive the driver's queue state and, on the wake, the
 * reason to schedule transmission again.  rtl8xxxu uses the pair as its
 * backpressure: it stops when the tx-urb pool crosses the low water
 * (core.c:5119) and wakes above the high water (core.c:5139).  The empty
 * stubs this replaces swallowed both halves, so the driver's own protection
 * never reached the producer and the pool ran dry ("Unable to allocate tx
 * urb", measured on the board 2026-10-31 in both the ko and the RTOS arms).
 */
static void
wlan_tx_wake_work(struct work_struct *work)
{
	struct wlan_linux_hw *w =
	    container_of(work, struct wlan_linux_hw, tx_wake_work);

	w->tx_wakes++;
	if (w->tx_wake_cb != NULL)
		w->tx_wake_cb(w->tx_wake_arg);
}

void
wlan_tx_wake_init(struct wlan_linux_hw *w)
{

	INIT_WORK(&w->tx_wake_work, wlan_tx_wake_work);
}

void
wlan_tx_wake_set_cb(struct ieee80211_hw *hw, void (*cb)(void *arg), void *arg)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	if (w == NULL)
		return;
	w->tx_wake_cb = cb;
	w->tx_wake_arg = arg;
}

int
wlan_tx_queue_stopped(struct ieee80211_hw *hw)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	return w != NULL && w->queue_stopped;
}

struct wlan_txq_slot {
	struct ieee80211_txq *txq;
	struct sk_buff_head q;
	bool used;
	bool scheduled;
	bool in_service;
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
	slot->scheduled = true;
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
	struct wlan_linux_hw *w = wlan_hw_single();
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
	if (frames > 0 && w != NULL) {
		w->tx_depth_calls++;
		w->tx_depth_frames += frames;
		if (frames > w->tx_depth_max) {
			w->tx_depth_max = frames;
		}
	}
	if (frame_cnt != NULL) {
		*frame_cnt = frames;
	}
	if (byte_cnt != NULL) {
		*byte_cnt = bytes;
	}
}

/*
 * A TXQ is scheduled by enqueue or by ieee80211_schedule_txq(); the driver's
 * txq work asks next_txq() for one, then returns it through return_txq()
 * (which maps to __ieee80211_schedule_txq() with force=false).  A driver that
 * uses the generic rtw89 txq work needs all three to move a frame; the old
 * NULL/empty stubs left every data frame parked in its slot.
 */
void __ieee80211_schedule_txq(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq, bool force)
{
	struct wlan_txq_slot *slot;
	unsigned long flags;

	(void)hw;
	if (txq == NULL) {
		return;
	}
	spin_lock_irqsave(&wlan_txq_lock, flags);
	slot = wlan_txq_slot(txq, force);
	if (slot != NULL) {
		slot->scheduled = force;
		slot->in_service = false;
	}
	spin_unlock_irqrestore(&wlan_txq_lock, flags);
}

struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, u8 ac)
{
	struct ieee80211_txq *txq = NULL;
	struct wlan_txq_slot *slot;
	unsigned long flags;
	int i;

	(void)hw;
	spin_lock_irqsave(&wlan_txq_lock, flags);
	for (i = 0; i < WLAN_TXQ_SLOTS; i++) {
		slot = &wlan_txq_slots[i];
		if (!slot->used || !slot->scheduled || slot->in_service ||
		    slot->txq == NULL || slot->txq->ac != ac ||
		    skb_queue_empty(&slot->q)) {
			continue;
		}
		slot->scheduled = false;
		slot->in_service = true;
		txq = slot->txq;
		break;
	}
	spin_unlock_irqrestore(&wlan_txq_lock, flags);
	return txq;
}

void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac)
{
	(void)hw;
	(void)ac;
}

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	if (w == NULL)
		return;
	/*
	 * Clearing the stop is only half of the pair: the frames the stop held
	 * back are the bridge's, and the bridge must be told to drain.  The
	 * driver calls this from its transmit completion (under priv->tx_urb_lock,
	 * a spinlock), where the bridge serializer cannot be taken - defer the
	 * delivery to the module workqueue, the same rule the receive path uses.
	 * The work's pending flag coalesces repeated wakes.
	 */
	w->queue_stopped = 0;
	schedule_work(&w->tx_wake_work);
}

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	if (w != NULL)
		w->queue_stopped = 1;
}

/*
 * mac80211's helper for drivers that do not implement wake_tx_queue: drain
 * the txq and call .tx.  iwlwifi implements wake_tx_queue and calls
 * ieee80211_tx_dequeue itself; this is here for the other arms / closure.
 *
 * The queue-stop gate: this entry is the substitute's enqueue path, not the
 * driver's queue wake.  A stopped queue keeps the frame in its txq until the
 * driver's own wake (ieee80211_wake_queues above) re-enters; draining it now
 * would hand the driver another frame while it has no room - the exact
 * defect the board measured.
 */
void ieee80211_handle_wake_tx_queue(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	struct sk_buff *skb;

	if (w == NULL || w->ops == NULL || w->ops->tx == NULL) {
		return;
	}
	if (w->queue_stopped)
		return;
	while (!w->queue_stopped &&
	    (skb = ieee80211_tx_dequeue(hw, txq)) != NULL) {
		struct ieee80211_tx_control control = {
			.sta = txq->sta,
		};

		w->ops->tx(hw, &control, skb);
	}
}
