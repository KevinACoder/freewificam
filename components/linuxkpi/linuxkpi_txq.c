/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_txq.c
 * @brief   The software transmit-queue engine: the scheduling entry points the
 *          interface declares beside its queue records.
 *
 * @design per-AC round-robin over an active list - the shape of upstream
 *         net/mac80211/tx.c:3955 (ieee80211_next_txq: take the head of the
 *         AC's active list, skip what this round already handed out, and hand
 *         a queue out once per round), :4017 (__ieee80211_schedule_txq: append
 *         at the tail when the queue has frames, idempotent because the list
 *         node itself is the "already scheduled" marker), :4160
 *         (ieee80211_txq_schedule_start: bump the round counter, never to 0 -
 *         0 is the "no round running" value next_txq tests), and
 *         include/net/mac80211.h:7006-7018 (return_txq is schedule_txq with the
 *         caller's force flag) with :7829-7831 (txq_schedule_end is deprecated
 *         and empty).
 *         The per-queue state upstream keeps in struct txq_info (tx.c:1522 -
 *         the frame queue, the schedule node, the round it was last handed out
 *         in) has nowhere to live in the public record: its tail is the
 *         driver's drv_priv.  This file keeps that state in a side table keyed
 *         by the record pointer, and its lock is the queue's lock - upstream
 *         holds fq->lock for the same purpose (tx.c:3752).
 *         Divergence, recorded because it is the whole difference from
 *         upstream: upstream's ieee80211_tx_dequeue runs the stack's transmit
 *         processing (rate control, encryption, header build) before handing
 *         the frame back (tx.c:3774-3830).  In this port a frame is already
 *         prepared when it reaches the queue - the net80211 bridge built it -
 *         so dequeue returns it with the queue's vif written into the control
 *         block and nothing else.  A driver that reads more than the vif and
 *         its own txq back off a dequeued frame needs that missing stage named
 *         in an issue before it is written.
 * @date    10.10.2026
 */

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"
#include "linuxkpi_txq.h"

#include <net/mac80211.h>

#ifndef CONFIG_LKPI_TXQ_MAX
#define CONFIG_LKPI_TXQ_MAX	256
#endif

/*
 * The 802.1D priority to access-category map (wme.c:23-32) and its TID form:
 * the low three bits index the map (ieee80211_i.h:2259-2262).
 */
static const u8 lkpi_txq_1d_to_ac[8] = {
	IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_BK, IEEE80211_AC_BE,
	IEEE80211_AC_VI, IEEE80211_AC_VI, IEEE80211_AC_VO, IEEE80211_AC_VO,
};

static u8
lkpi_txq_ac_from_tid(u8 tid)
{
	return lkpi_txq_1d_to_ac[tid & 7];
}

/* ------------------------------------------------------------------ */
/* storage                                                             */
/* ------------------------------------------------------------------ */

/*
 * One side-table slot per live txq.  `queue` is never handed to the public skb
 * queue ops (which would take its own lock): every access is made under the
 * engine lock, so `reset` - the shape-only initializer - is the right one and
 * the slot's embedded lock is left alone.
 */
struct lkpi_txq {
	struct ieee80211_txq	*txq;		/* NULL = the slot is free */
	struct sk_buff_head	queue;
	struct list_head	order;		/* node on the AC's active list */
	unsigned long		bytes;		/* what `queue` holds */
	u32			round;		/* last round it was handed out in */
	unsigned int		active:1;
};

static struct {
	struct lkpi_txq		slots[CONFIG_LKPI_TXQ_MAX];
	struct list_head	active[IEEE80211_NUM_ACS];
	u32			round[IEEE80211_NUM_ACS];
	spinlock_t		lock;
	bool			init;
	u32			refusals;	/* allocations with no slot/memory */
	unsigned long		handed;		/* queues handed to a driver */
	unsigned long		orphans;	/* calls on an unknown txq */
} eng;

static void
lkpi_txq_setup(void)
{
	os_critical_state_t csr;
	unsigned int i;

	if (eng.init)
		return;
	csr = osCriticalEnter();
	if (!eng.init) {
		for (i = 0; i < IEEE80211_NUM_ACS; i++)
			INIT_LIST_HEAD(&eng.active[i]);
		spin_lock_init(&eng.lock);
		eng.init = true;
	}
	osCriticalExit(csr);
}

/* the caller holds the engine lock */
static struct lkpi_txq *
lkpi_txq_find_locked(struct ieee80211_txq *txq)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++)
		if (eng.slots[i].txq == txq)
			return &eng.slots[i];
	return NULL;
}

/* the queue is unreachable now, so it can be drained without the lock */
static void
lkpi_txq_drain(struct lkpi_txq *t)
{
	struct sk_buff *skb;

	while ((skb = lkpi_skb_do_dequeue(&t->queue)) != NULL)
		lkpi_skb_free(skb);
	t->bytes = 0;
}

/* ------------------------------------------------------------------ */
/* allocation                                                          */
/* ------------------------------------------------------------------ */

struct ieee80211_txq *
lkpi_txq_alloc(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, u8 tid)
{
	struct ieee80211_txq *txq;
	struct lkpi_txq *t = NULL;
	unsigned int i;
	unsigned long flags;

	if (hw == NULL || vif == NULL)
		return NULL;
	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++)
		if (eng.slots[i].txq == NULL) {
			t = &eng.slots[i];
			break;
		}
	spin_unlock_irqrestore(&eng.lock, flags);
	if (t == NULL) {
		eng.refusals++;
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi_txq: table full (%u queues)\n",
		    (unsigned)CONFIG_LKPI_TXQ_MAX);
		return NULL;
	}
	txq = kzalloc(offsetof(struct ieee80211_txq, drv_priv) +
	    (size_t)hw->txq_data_size, GFP_KERNEL);
	if (txq == NULL) {
		eng.refusals++;
		return NULL;
	}
	txq->vif = vif;
	if (sta == NULL) {
		/* the interface's own queue, and the one its beacons ride */
		txq->sta = NULL;
		txq->tid = 0;
		txq->ac = IEEE80211_AC_BE;
	} else {
		txq->sta = sta;
		txq->tid = tid;
		txq->ac = lkpi_txq_ac_from_tid(tid);
	}
	spin_lock_irqsave(&eng.lock, flags);
	t->txq = txq;
	lkpi_skb_queue_reset(&t->queue);
	INIT_LIST_HEAD(&t->order);
	t->bytes = 0;
	t->round = 0;
	t->active = false;
	spin_unlock_irqrestore(&eng.lock, flags);
	return txq;
}

void
lkpi_txq_free(struct ieee80211_txq *txq)
{
	struct lkpi_txq *t;
	unsigned long flags;

	if (txq == NULL)
		return;
	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	t = lkpi_txq_find_locked(txq);
	if (t != NULL) {
		if (t->active)
			list_del_init(&t->order);
		t->active = false;
		t->txq = NULL;		/* unreachable: the drain is safe below */
	}
	spin_unlock_irqrestore(&eng.lock, flags);
	if (t != NULL)
		lkpi_txq_drain(t);
	kfree(txq);
}

void
lkpi_txq_purge(struct ieee80211_txq *txq)
{
	struct lkpi_txq *t;
	unsigned long flags;

	if (txq == NULL)
		return;
	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	t = lkpi_txq_find_locked(txq);
	if (t != NULL && t->active) {
		/*
		 * A purged queue that stays on the active list would be handed
		 * out once more and answered with a NULL frame; upstream drops
		 * it from the schedule order for the same reason
		 * (tx.c:1568-1571, ieee80211_txq_purge).
		 */
		list_del_init(&t->order);
		t->active = false;
	}
	spin_unlock_irqrestore(&eng.lock, flags);
	if (t != NULL)
		lkpi_txq_drain(t);
}

/* the sweep both teardowns share: one match at a time, freed outside the lock */
static void
lkpi_txq_sweep(struct ieee80211_vif *vif, struct ieee80211_sta *sta, bool by_vif)
{
	struct ieee80211_txq *txq;
	unsigned int i;
	unsigned long flags;

	lkpi_txq_setup();
	for (;;) {
		spin_lock_irqsave(&eng.lock, flags);
		txq = NULL;
		for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++) {
			struct ieee80211_txq *cand = eng.slots[i].txq;

			if (cand == NULL)
				continue;
			if (by_vif ? (cand->vif == vif) : (cand->sta == sta)) {
				txq = cand;
				break;
			}
		}
		spin_unlock_irqrestore(&eng.lock, flags);
		if (txq == NULL)
			return;
		lkpi_txq_free(txq);
	}
}

void
lkpi_txq_free_vif(struct ieee80211_vif *vif)
{

	if (vif == NULL)
		return;
	/* the vif's own queue and every station queue it owns */
	lkpi_txq_sweep(vif, NULL, true);
}

void
lkpi_txq_free_sta(struct ieee80211_sta *sta)
{

	if (sta == NULL)
		return;
	lkpi_txq_sweep(NULL, sta, false);
}

/* ------------------------------------------------------------------ */
/* the stack side                                                      */
/* ------------------------------------------------------------------ */

int
lkpi_txq_enqueue(struct ieee80211_txq *txq, struct sk_buff *skb)
{
	struct lkpi_txq *t;
	unsigned long flags;

	if (txq == NULL || skb == NULL)
		return -EINVAL;
	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	t = lkpi_txq_find_locked(txq);
	if (t != NULL) {
		u32 before = t->queue.qlen;

		lkpi_skb_do_queue_tail(&t->queue, skb);
		/* the pool refuses an skb another queue still owns; count the
		 * frame only when it actually landed */
		if (t->queue.qlen != before)
			t->bytes += skb->len;
	}
	spin_unlock_irqrestore(&eng.lock, flags);
	if (t == NULL) {
		eng.orphans++;
		return -EINVAL;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* the scheduling entry points                                         */
/* ------------------------------------------------------------------ */

void
__ieee80211_schedule_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq,
    bool force)
{
	struct lkpi_txq *t;
	unsigned long flags;

	(void)hw;
	if (txq == NULL)
		return;
	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	t = lkpi_txq_find_locked(txq);
	if (t != NULL && !t->active &&
	    (force || !skb_queue_empty(&t->queue))) {
		list_add_tail(&t->order, &eng.active[txq->ac]);
		t->active = true;
	}
	spin_unlock_irqrestore(&eng.lock, flags);
}

void
ieee80211_schedule_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	/* upstream include/net/mac80211.h:6998-7002 */
	__ieee80211_schedule_txq(hw, txq, true);
}

void
ieee80211_return_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq,
    bool skip_empty)
{
	/* upstream include/net/mac80211.h:7014-7019: the same call, with the
	 * driver's force flag as the "has frames" answer */
	__ieee80211_schedule_txq(hw, txq, skip_empty);
}

void
ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac)
{
	unsigned long flags;

	(void)hw;
	if (ac >= IEEE80211_NUM_ACS)
		return;
	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	eng.round[ac]++;
	/* 0 is the "no round running" value next_txq tests (tx.c:4167-4170) */
	if (eng.round[ac] == 0)
		eng.round[ac] = 1;
	spin_unlock_irqrestore(&eng.lock, flags);
}

void
ieee80211_txq_schedule_end(struct ieee80211_hw *hw, u8 ac)
{

	/* upstream include/net/mac80211.h:7829-7831: deprecated and empty */
	(void)hw;
	(void)ac;
}

struct ieee80211_txq *
ieee80211_next_txq(struct ieee80211_hw *hw, u8 ac)
{
	struct lkpi_txq *t, *tmp;
	struct ieee80211_txq *ret = NULL;
	unsigned long flags;

	(void)hw;
	if (ac >= IEEE80211_NUM_ACS)
		return NULL;
	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	if (eng.round[ac] == 0)
		goto out;
	list_for_each_entry_safe(t, tmp, &eng.active[ac], order) {
		if (t->round == eng.round[ac])
			continue;	/* this round already served it */
		list_del_init(&t->order);
		t->active = false;
		t->round = eng.round[ac];
		eng.handed++;
		ret = t->txq;
		break;
	}
out:
	spin_unlock_irqrestore(&eng.lock, flags);
	return ret;
}

void
ieee80211_txq_get_depth(struct ieee80211_txq *txq, unsigned long *frame_cnt,
    unsigned long *byte_cnt)
{
	struct lkpi_txq *t;
	unsigned long flags;
	unsigned long frames = 0, bytes = 0;

	if (txq != NULL) {
		lkpi_txq_setup();
		spin_lock_irqsave(&eng.lock, flags);
		t = lkpi_txq_find_locked(txq);
		if (t != NULL) {
			frames = t->queue.qlen;
			bytes = t->bytes;
		}
		spin_unlock_irqrestore(&eng.lock, flags);
	}
	if (frame_cnt != NULL)
		*frame_cnt = frames;
	if (byte_cnt != NULL)
		*byte_cnt = bytes;
}

struct sk_buff *
ieee80211_tx_dequeue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	struct ieee80211_tx_info *info;
	struct lkpi_txq *t;
	struct sk_buff *skb = NULL;
	unsigned long flags;

	(void)hw;
	if (txq == NULL)
		return NULL;
	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	t = lkpi_txq_find_locked(txq);
	if (t != NULL) {
		skb = lkpi_skb_do_dequeue(&t->queue);
		if (skb != NULL)
			t->bytes = (t->bytes >= skb->len) ?
			    t->bytes - skb->len : 0;
	}
	spin_unlock_irqrestore(&eng.lock, flags);
	if (skb == NULL)
		return NULL;
	/*
	 * The interface records the queue's vif on the frame before the driver
	 * sees it (tx.c:3761 in the txq path, tx.c:1761 in the .tx path); the
	 * driver's own push reads the vif and the station straight off the txq
	 * (rtw89 core.c:4749-4750), and its .tx entry reads the vif off the
	 * control block (rtw89 mac80211.c:27).
	 */
	info = (struct ieee80211_tx_info *)skb->cb;
	info->control.vif = txq->vif;
	return skb;
}

struct sk_buff *
ieee80211_tx_dequeue_ni(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{

	/*
	 * upstream include/net/mac80211.h:6933-6949: the process-context
	 * spelling, which only wraps the same call in a bottom-half disable.
	 * There are no bottom halves here, so it is the same call.
	 */
	return ieee80211_tx_dequeue(hw, txq);
}

/* ------------------------------------------------------------------ */
/* evidence                                                            */
/* ------------------------------------------------------------------ */

unsigned int
lkpi_txq_in_use(void)
{
	unsigned int i, n = 0;

	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++)
		if (eng.slots[i].txq != NULL)
			n++;
	return n;
}

u32
lkpi_txq_alloc_refusals(void)
{

	return eng.refusals;
}

unsigned long
lkpi_txq_handed_out(void)
{

	return eng.handed;
}

unsigned long
lkpi_txq_orphan_calls(void)
{

	return eng.orphans;
}
