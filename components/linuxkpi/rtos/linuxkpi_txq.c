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
#include "linuxkpi_txq_audit.h"
#include "linuxkpi_80211.h"

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

/* ------------------------------------------------------------------ */
/* audit (CONFIG_LKPI_TXQ_AUDIT)                                       */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_LKPI_TXQ_AUDIT

/* Bounds: the dump is a violation path, so it may walk, but it must not turn a
 * console that is already behind into a second stall. */
#define LKPI_TXQ_AUDIT_NODES	3u	/* nodes printed per suspicious slot */
#define LKPI_TXQ_AUDIT_SLOTS	8u	/* suspicious slots printed per dump */
#define LKPI_TXQ_AUDIT_FORWARD	64u	/* link-walk bound inside one queue */
#define LKPI_TXQ_AUDIT_WALK	4096u	/* the ledger's node bound, as the census */
#define LKPI_TXQ_AUDIT_CRUMBS	16u
#define LKPI_TXQ_AUDIT_DUMPS	8u

struct lkpi_txq_crumb {
	const char		*what;
	const void		*q;
	const void		*skb;
	u32			qlen;
	u32			slot;
};

static struct {
	u32			enq_bad;	/* queue disagreeable after enqueue */
	u32			deq_bad;	/* ... after dequeue */
	u32			free_bad;	/* slots reaped but queue not empty */
	u32			purge_bad;
	u32			alloc_race;	/* slot handed twice by alloc's window */
	u32			alloc_liveq;	/* slot reused with nodes still on it */
	u32			free_linked;	/* frame freed while still linked */
	u32			free_eng;	/* ... and the queue was the engine's */
	u32			reset_live;	/* reset of a queue that still linked */
	u32			op_bad;	/* a queue primitive left it disagreeing */
	u32			op_dumps;
	u32			wt;	/* a primitive refused to write through a
					 * neighbour that is not its own */
	u32			wt_dumps;
	u32			dumps;
	struct lkpi_txq_crumb	crumb[LKPI_TXQ_AUDIT_CRUMBS];
	u32			crumb_n;
	u32			dump_left;
	unsigned long		dead_nodes;	/* nodes in slots whose txq is NULL */
	unsigned long		walk_all;	/* nodes over every slot */
	bool			pending_dump;
} aud = { .dump_left = LKPI_TXQ_AUDIT_DUMPS };

/*
 * The invariant, O(1): an empty queue is the head pointing at itself with a
 * zero count; a non-empty one counts at least one frame, links away from the
 * head, and both ends' back pointers land on it.  A queue that fails this is
 * one no dequeue will walk correctly - either it links frames the count says
 * do not exist (the stranding shape: `q=0` above, thousands of nodes below), or
 * it counts frames it cannot reach.
 */
/* A slot's storage is static and zeroed: a slot the engine has never handed
 * out has a queue whose links are still NULL, and that is pristine, not a
 * violation.  `lkpi_skb_queue_reset()` is the shape initializer that runs when
 * the slot is taken (alloc), after which the empty form is head-at-itself. */
static bool
lkpi_txq_q_pristine(const struct sk_buff_head *q)
{

	return q->qlen == 0u && q->next == NULL && q->prev == NULL;
}

static bool
lkpi_txq_q_ok(const struct sk_buff_head *q)
{
	const struct sk_buff *h = (const struct sk_buff *)q;

	if (lkpi_txq_q_pristine(q))
		return true;
	if (q->qlen == 0u)
		return q->next == h && q->prev == h;
	if (q->next == h || q->next == NULL || q->prev == h || q->prev == NULL)
		return false;
	return q->next->prev == h && q->prev->next == h;
}

/* nodes the queue links, bounded: `cap` for the ledger, the small bound inside
 * a dump (a corrupt chain must not turn one print into a hang) */
static unsigned long
lkpi_txq_nodes(const struct sk_buff_head *q, unsigned long cap)
{
	const struct sk_buff *h = (const struct sk_buff *)q;
	const struct sk_buff *it = q->next;
	unsigned long n = 0;

	while (it != NULL && it != h && n < cap) {
		n++;
		it = it->next;
	}
	return n;
}

static void
lkpi_txq_crumb(const char *what, const void *q, const void *skb, u32 qlen,
    u32 slot)
{
	struct lkpi_txq_crumb *c = &aud.crumb[aud.crumb_n % LKPI_TXQ_AUDIT_CRUMBS];

	aud.crumb_n++;
	c->what = what;
	c->q = q;
	c->skb = skb;
	c->qlen = qlen;
	c->slot = slot;
}

/* the slot a queue head belongs to, by address arithmetic only */
static struct lkpi_txq *
lkpi_txq_slot_of(const struct sk_buff_head *q)
{
	uintptr_t base = (uintptr_t)&eng.slots[0].queue;
	uintptr_t addr = (uintptr_t)q;
	uintptr_t stride = (uintptr_t)sizeof(eng.slots[0]);
	uintptr_t off;

	if (addr < base)
		return NULL;
	off = addr - base;
	if (off >= (uintptr_t)sizeof(eng.slots))
		return NULL;
	if (off % stride != 0u)
		return NULL;
	return &eng.slots[off / stride];
}

static void lkpi_txq_audit_sweep(struct sk_buff *skb, const char *op);

/*
 * Write-through guards.  Every queue primitive edits its neighbours, and the
 * neighbour is whatever the frame's cached pointer says: once a frame has
 * been linked into a second queue (or freed and re-linked), that pointer names
 * a node of a DIFFERENT queue and the edit corrupts that queue instead.  Both
 * checks are O(1) and both refuse the edit rather than corrupt a live chain.
 */
/*
 * A queue initialized over its own frames: the shape is emptied, the frames
 * keep `list` pointing at it, and the next free of any of them edits whatever
 * chain its stale neighbours name.  That is the entry point this audit is
 * looking for, so it is checked on every reset (init-time resets are empty and
 * cost three compares).
 */
void
lkpi_txq_audit_reset(struct sk_buff_head *q)
{
	/*
	 * Engine queues only: this runs before the shape is written, so on a
	 * stack temporary the fields are garbage and following `next` would
	 * fault.  A slot's address is pure arithmetic, and the slot reset is
	 * the one that matters - it is how a reused slot forgets its frames.
	 */
	if (q == NULL || lkpi_txq_slot_of(q) == NULL)
		return;
	if (q->qlen == 0u || q->next == NULL ||
	    q->next == (struct sk_buff *)q)
		return;
	if (q->next->list != q)
		return;
	aud.reset_live++;
	if (aud.op_dumps++ >= LKPI_TXQ_AUDIT_DUMPS)
		return;
	lkpi_printf(ULOG_ALWAYS_LEVEL,
	    "txaudit: reset_live q=%p qlen=%lu next=%p prev=%p next_list=%p "
	    "kfree=%p\n", (void *)q, (unsigned long)q->qlen, (void *)q->next,
	    (void *)q->prev, (void *)q->next->list,
	    (void *)(uintptr_t)__builtin_return_address(0));
}

bool
lkpi_txq_audit_wt_link(struct sk_buff *skb, struct sk_buff_head *q,
    const char *op, bool head)
{
	struct sk_buff *at;

	if (q == NULL)
		return false;
	at = head ? q->next : q->prev;
	if (at == (struct sk_buff *)q || at->list == q)
		return false;	/* the node we splice into is ours */
	aud.wt++;
	if (aud.wt_dumps++ < LKPI_TXQ_AUDIT_DUMPS)
		lkpi_printf(ULOG_ALWAYS_LEVEL,
		    "txaudit: writethrough op=%s q=%p qlen=%lu q_next=%p "
		    "q_prev=%p at=%p at_list=%p skb=%p skb_list=%p\n",
		    op, (void *)q, (unsigned long)q->qlen, (void *)q->next,
		    (void *)q->prev, (void *)at, (void *)at->list,
		    (void *)skb, skb ? (void *)skb->list : NULL);
	return true;
}

bool
lkpi_txq_audit_wt_detach(struct sk_buff *skb, struct sk_buff_head *q,
    const char *op)
{
	if (skb == NULL || q == NULL)
		return false;
	if (skb->prev == (const struct sk_buff *)q)
		return false;	/* head node: the edit lands on the head */
	if (skb->prev != NULL && skb->prev->next == skb &&
	    skb->prev->list == q)
		return false;	/* middle/tail: our predecessor names us */
	aud.wt++;
	if (aud.wt_dumps++ < LKPI_TXQ_AUDIT_DUMPS)
		lkpi_printf(ULOG_ALWAYS_LEVEL,
		    "txaudit: writethrough op=%s q=%p qlen=%lu q_next=%p "
		    "q_prev=%p skb=%p skb_list=%p skb_prev=%p skb_next=%p\n",
		    op, (void *)q, (unsigned long)q->qlen, (void *)q->next,
		    (void *)q->prev, (void *)skb, (void *)skb->list,
		    (void *)skb->prev, (void *)skb->next);
	return true;
}

/*
 * The queue primitives' own check: called after each tail/head link, detach
 * and dequeue on any queue.  It is the only place that can say WHICH
 * primitive broke the invariant - the exit checks above only say that by the
 * time the engine's call returned, the queue already disagreed.
 */
void
lkpi_txq_audit_op(struct sk_buff *skb, struct sk_buff_head *q, const char *op)
{
	struct lkpi_txq *t;

	if (q == NULL)
		return;
	t = lkpi_txq_slot_of(q);
	if (t == NULL)
		return;
	if (lkpi_txq_q_ok(q)) {
		lkpi_txq_audit_sweep(skb, op);
		return;
	}
	aud.op_bad++;
	if (aud.op_dumps++ >= LKPI_TXQ_AUDIT_DUMPS)
		return;
	lkpi_txq_crumb(op, q, NULL, q->qlen, (u32)(t - eng.slots));
	/* its own line and its own budget: the exit checks' dumps are spent on
	 * the state they saw, and this is the one that names the primitive */
	{
		const struct sk_buff *h = (const struct sk_buff *)q;
		const struct sk_buff *it = q->next;
		unsigned int n;

		lkpi_printf(ULOG_ALWAYS_LEVEL,
		    "txaudit: op=%s slot=%u qlen=%lu next=%p prev=%p "
		    "nodes=%lu skb=%p skb_prev=%p skb_next=%p skb_list=%p\n",
		    op, (unsigned)(t - eng.slots), (unsigned long)q->qlen,
		    (void *)q->next, (void *)q->prev,
		    lkpi_txq_nodes(q, LKPI_TXQ_AUDIT_FORWARD),
		    (void *)skb, skb ? (void *)skb->prev : NULL,
		    skb ? (void *)skb->next : NULL,
		    skb ? (void *)skb->list : NULL);
		for (n = 0; n < LKPI_TXQ_AUDIT_NODES && it != NULL && it != h;
		    n++, it = it->next)
			lkpi_printf(ULOG_ALWAYS_LEVEL,
			    "txaudit:   node=%p len=%u list=%p next=%p prev=%p\n",
			    (const void *)it, (unsigned)it->len,
			    (const void *)it->list, (const void *)it->next,
			    (const void *)it->prev);
	}
	if (aud.op_dumps < LKPI_TXQ_AUDIT_DUMPS)
		lkpi_txq_audit_sweep(skb, op);
}

/*
 * Cross-slot sweep.  The damage this is chasing is a write THROUGH a stale
 * neighbour pointer: the primitive touches its own queue correctly and
 * breaks a different one.  Checking only the queue the caller passed would
 * credit the violation to the next operation that happens to look at the
 * victim; this names the operation that caused it.
 */
static void
lkpi_txq_audit_sweep(struct sk_buff *skb, const char *op)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++) {
		struct sk_buff_head *q = &eng.slots[i].queue;

		if (q->qlen == 0u && q->next == (struct sk_buff *)q &&
		    q->prev == (struct sk_buff *)q)
			continue;
		if (lkpi_txq_q_ok(q))
			continue;
		aud.op_bad++;
		if (aud.op_dumps++ >= LKPI_TXQ_AUDIT_DUMPS)
			return;
		lkpi_txq_crumb(op, q, skb, q->qlen, i);
		lkpi_printf(ULOG_ALWAYS_LEVEL,
		    "txaudit: victim=%u op=%s q=%p qlen=%lu next=%p prev=%p "
		    "nodes=%lu skb=%p skb_list=%p skb_prev=%p skb_next=%p\n",
		    i, op, (void *)q, (unsigned long)q->qlen, (void *)q->next,
		    (void *)q->prev, lkpi_txq_nodes(q, LKPI_TXQ_AUDIT_FORWARD),
		    (void *)skb, skb ? (void *)skb->list : NULL,
		    skb ? (void *)skb->prev : NULL,
		    skb ? (void *)skb->next : NULL);
	}
}

/* the caller holds the engine lock */
static void lkpi_txq_dump_locked(const char *tag);

/* the caller holds the engine lock */
static void
lkpi_txq_dump_locked(const char *tag)
{
	const struct sk_buff *h;
	const struct sk_buff *it;
	unsigned int i, used = 0, nulls = 0, dead = 0, shown = 0;
	unsigned int n;
	struct lkpi_txq *t;
	bool suspicious;

	if (aud.dumps++ >= LKPI_TXQ_AUDIT_DUMPS) {
		aud.dump_left = 0u;
		return;
	}
	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++) {
		t = &eng.slots[i];
		h = (const struct sk_buff *)&t->queue;
		if (t->txq == NULL) {
			nulls++;
			if (!lkpi_txq_q_pristine(&t->queue) &&
			    !lkpi_txq_q_ok(&t->queue))
				dead++;
			continue;
		}
		used++;
	}
	aud.dead_nodes = 0ul;
	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++)
		if (eng.slots[i].txq == NULL)
			aud.dead_nodes += lkpi_txq_nodes(&eng.slots[i].queue,
			    LKPI_TXQ_AUDIT_FORWARD);
	lkpi_printf(ULOG_ALWAYS_LEVEL,
	    "txaudit: dump tag=%s used=%u null=%u dead_slots=%u dead_nodes=%lu "
	    "enq_bad=%u deq_bad=%u free_bad=%u purge_bad=%u alloc_race=%u "
	    "alloc_liveq=%u free_linked=%u free_eng=%u reset_live=%u "
	    "op_bad=%u\n",
	    tag, used, nulls, dead, aud.dead_nodes, aud.enq_bad, aud.deq_bad,
	    aud.free_bad, aud.purge_bad, aud.alloc_race, aud.alloc_liveq,
	    aud.free_linked, aud.free_eng, aud.reset_live, aud.op_bad);
	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++) {
		t = &eng.slots[i];
		h = (const struct sk_buff *)&t->queue;
		suspicious = !lkpi_txq_q_ok(&t->queue) &&
		    !lkpi_txq_q_pristine(&t->queue);
		if (!suspicious)
			continue;
		if (shown >= LKPI_TXQ_AUDIT_SLOTS) {
			lkpi_printf(ULOG_ALWAYS_LEVEL,
			    "txaudit: dump tag=%s truncated\n", tag);
			break;
		}
		shown++;
		lkpi_printf(ULOG_ALWAYS_LEVEL,
		    "txaudit: slot=%u txq=%p ac=%u act=%u qlen=%lu next=%p "
		    "prev=%p nodes=%lu\n",
		    i, (void *)t->txq,
		    t->txq != NULL ? (unsigned)t->txq->ac : 0u,
		    (unsigned)t->active, (unsigned long)t->queue.qlen,
		    (void *)t->queue.next, (void *)t->queue.prev,
		    lkpi_txq_nodes(&t->queue, LKPI_TXQ_AUDIT_FORWARD));
		for (n = 0, it = t->queue.next; n < LKPI_TXQ_AUDIT_NODES &&
		    it != NULL && it != h; n++, it = it->next)
			lkpi_printf(ULOG_ALWAYS_LEVEL,
			    "txaudit:   node=%p len=%u list=%p next=%p prev=%p\n",
			    (const void *)it, (unsigned)it->len,
			    (const void *)it->list, (const void *)it->next,
			    (const void *)it->prev);
	}
}

void
lkpi_txq_dump(const char *tag)
{
	unsigned long flags;

	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	lkpi_txq_dump_locked(tag);
	spin_unlock_irqrestore(&eng.lock, flags);
}

unsigned long
lkpi_txq_walk_all(void)
{
	unsigned long n = 0, dead = 0;
	unsigned long flags;
	unsigned int i;

	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++) {
		unsigned long nodes = lkpi_txq_nodes(&eng.slots[i].queue,
		    LKPI_TXQ_AUDIT_WALK);

		n += nodes;
		if (eng.slots[i].txq == NULL)
			dead += nodes;
	}
	aud.walk_all = n;
	aud.dead_nodes = dead;
	spin_unlock_irqrestore(&eng.lock, flags);
	return n;
}

void
lkpi_txq_audit_line(const char *tag)
{
	lkpi_printf(ULOG_ALWAYS_LEVEL,
	    "txaudit: tag=%s enq_bad=%u deq_bad=%u free_bad=%u purge_bad=%u "
	    "alloc_race=%u alloc_liveq=%u free_linked=%u free_eng=%u "
	    "reset_live=%u op_bad=%u dumps=%u dead_nodes=%lu walk_all=%lu\n",
	    tag, aud.enq_bad, aud.deq_bad, aud.free_bad, aud.purge_bad,
	    aud.alloc_race, aud.alloc_liveq, aud.free_linked, aud.free_eng,
	    aud.reset_live, aud.op_bad, aud.dumps, aud.dead_nodes,
	    aud.walk_all);
	if (aud.pending_dump) {
		aud.pending_dump = false;
		lkpi_txq_dump("pending");
	}
}

void
lkpi_txq_audit_free_linked(struct sk_buff_head *q, struct sk_buff *skb,
    void *ra)
{
	unsigned int i;

	if (q == NULL) {
		aud.free_linked++;
		lkpi_txq_crumb("free_linked_nullq", NULL, skb, 0u, 0u);
		return;
	}
	aud.free_linked++;
	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++) {
		if (&eng.slots[i].queue == q) {
			aud.free_eng++;
			lkpi_txq_crumb("free_linked", q, skb, q->qlen, i);
			/* the pool's own lock is held here; the engine lock is
			 * not ours to take (the drain holds it above us), so
			 * the dump waits for the next census */
			aud.pending_dump = true;
			return;
		}
	}
	lkpi_txq_crumb("free_linked_foreign", q, skb, q->qlen,
	    (u32)CONFIG_LKPI_TXQ_MAX);
	if (aud.wt_dumps++ < LKPI_TXQ_AUDIT_DUMPS)
		lkpi_printf(ULOG_ALWAYS_LEVEL,
		    "txaudit: free_linked q=%p qlen=%lu q_next=%p skb=%p "
		    "skb_list=%p skb_prev=%p skb_next=%p ra=%p\n",
		    (void *)q, (unsigned long)q->qlen, (void *)q->next,
		    (void *)skb, (void *)skb->list, (void *)skb->prev,
		    (void *)skb->next, ra);
}

#endif /* CONFIG_LKPI_TXQ_AUDIT */

/* the caller holds the engine lock: holding it across the drain is what keeps a
 * reuse of the slot from resetting the queue mid-walk */
static void
lkpi_txq_drain_locked(struct lkpi_txq *t)
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
		/*
		 * The interface's own queues, as it sets them up (tx.c:1526-1544):
		 * `sdata->vif.txq` is the no-station data queue (BE) and
		 * `sdata->vif.txq_mgmt` is the management queue no station owns
		 * (the tid == IEEE80211_NUM_TIDS form, VO).
		 */
		txq->sta = NULL;
		txq->tid = tid;
		txq->ac = (tid == IEEE80211_NUM_TIDS) ? IEEE80211_AC_VO :
		    IEEE80211_AC_BE;
	} else {
		txq->sta = sta;
		txq->tid = tid;
		/*
		 * The station's management MPDU queue rides VO (tx.c:1556-1558);
		 * a per-TID queue rides its access category.
		 */
		txq->ac = (tid == IEEE80211_NUM_TIDS) ? IEEE80211_AC_VO :
		    lkpi_txq_ac_from_tid(tid);
	}
	spin_lock_irqsave(&eng.lock, flags);
#ifdef CONFIG_LKPI_TXQ_AUDIT
	/*
	 * The slot was picked under an earlier take of this lock and only the
	 * kzalloc above ran without it, so a second allocator can have taken
	 * the same slot in that window - and the first queue's frames are then
	 * still linked in the slot whose record the reset below will zero.
	 */
	if (t->txq != NULL) {
		aud.alloc_race++;
		lkpi_txq_crumb("alloc_race", &t->queue, txq, t->queue.qlen, i);
		lkpi_txq_dump_locked("alloc_race");
	}
	if (t->queue.qlen != 0u ||
	    (t->queue.next != (struct sk_buff *)&t->queue &&
	    t->queue.next != NULL)) {
		aud.alloc_liveq++;
		lkpi_txq_crumb("alloc_liveq", &t->queue, txq, t->queue.qlen, i);
		lkpi_txq_dump_locked("alloc_liveq");
	}
#endif
	t->txq = txq;
	/* a free slot's queue is empty: free() drains it under this same lock
	 * before the slot can be handed out again (lkpi_txq_drain_locked) */
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
		t->txq = NULL;
		/* still under the lock: see lkpi_txq_drain_locked */
		lkpi_txq_drain_locked(t);
#ifdef CONFIG_LKPI_TXQ_AUDIT
		if (!lkpi_txq_q_ok(&t->queue)) {
			aud.free_bad++;
			lkpi_txq_crumb("free_bad", &t->queue, txq, t->queue.qlen,
			    (u32)(t - eng.slots));
			lkpi_txq_dump_locked("free_bad");
		}
#endif
	}
	spin_unlock_irqrestore(&eng.lock, flags);
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
	if (t != NULL)
		lkpi_txq_drain_locked(t);
#ifdef CONFIG_LKPI_TXQ_AUDIT
	if (t != NULL && !lkpi_txq_q_ok(&t->queue)) {
		aud.purge_bad++;
		lkpi_txq_crumb("purge_bad", &t->queue, txq, t->queue.qlen,
		    (u32)(t - eng.slots));
		lkpi_txq_dump_locked("purge_bad");
	}
#endif
	spin_unlock_irqrestore(&eng.lock, flags);
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
	if (skb->len >= 500) {
		static unsigned trace_n;

		if (trace_n < 6) {
			const unsigned char *d = skb->data;

			trace_n++;
			lkpi_printf(ULOG_ERROR_LEVEL,
			    "txq: enq len=%u hr=%u first="
			    "%02x %02x %02x %02x %02x %02x %02x %02x "
			    "skb=%p txq=%p\n",
			    (unsigned)skb->len, skb_headroom(skb),
			    d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
			    skb, txq);
		}
	}
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
#ifdef CONFIG_LKPI_TXQ_AUDIT
		/*
		 * The refusal case matters as much as the success one: the
		 * pool's `queue_linkable` drops a frame it thinks another queue
		 * owns, so a queue that counts nothing may still have gained a
		 * link - or an skb whose list pointer is stale splices a cycle
		 * into a live list.
		 */
		if (!lkpi_txq_q_ok(&t->queue)) {
			aud.enq_bad++;
			lkpi_txq_crumb("enq_bad", &t->queue, skb, t->queue.qlen,
			    (u32)(t - eng.slots));
			lkpi_txq_dump_locked("enq_bad");
		}
#endif
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

/*
 * The engine's own census, for a board whose transmit stops under load.
 * `queued` is what sits in the per-txq queues right now - frames the stack
 * handed down that no driver dequeue has taken - which is the one number that
 * tells "the driver stopped taking frames" from "the frames left and never
 * came back".  The counters beside it separate the two failure shapes: a
 * refusal means the slot table was full, an orphan means a dequeue named a
 * queue the engine does not know, and `handed` is how often a dequeue was
 * served at all.
 */
void
lkpi_txq_census(unsigned long *queued, unsigned long *handed,
    unsigned long *orphans, unsigned long *refusals)
{
	unsigned long flags;
	unsigned long q = 0;
	unsigned int i;

	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++)
		if (eng.slots[i].txq != NULL)
			q += eng.slots[i].queue.qlen;
	spin_unlock_irqrestore(&eng.lock, flags);
	if (queued != NULL)
		*queued = q;
	if (handed != NULL)
		*handed = eng.handed;
	if (orphans != NULL)
		*orphans = eng.orphans;
	if (refusals != NULL)
		*refusals = eng.refusals;
}

/*
 * How many nodes the slot queues actually link, versus what qlen says.  A
 * frame that is linked but not counted is a frame no dequeue will reach: the
 * pool keeps its slots, and the seam's spare gate refuses forever - the
 * 2026-11-06 P0 shape (134 live socket buffers pointing at one queue while
 * the counter read 0).
 */
unsigned long
lkpi_txq_list_len(void)
{
	struct sk_buff *skb;
	unsigned long n = 0;
	unsigned long flags;
	unsigned int i;

	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	for (i = 0; i < CONFIG_LKPI_TXQ_MAX; i++) {
		if (eng.slots[i].txq == NULL)
			continue;
		skb_queue_walk(&eng.slots[i].queue, skb) {
			if (++n >= 4096ul)
				break;
		}
	}
	spin_unlock_irqrestore(&eng.lock, flags);
	return n;
}

#define LKPI_TXQ_KICK_BATCH	16

void
lkpi_txq_kick_pending(struct ieee80211_hw *hw,
    void (*kick)(struct ieee80211_hw *hw, struct ieee80211_txq *txq, void *arg),
    void *arg)
{
	struct ieee80211_txq *batch[LKPI_TXQ_KICK_BATCH];
	unsigned int n = 0;
	unsigned int i;
	unsigned long flags;

	if (kick == NULL)
		return;
	lkpi_txq_setup();
	spin_lock_irqsave(&eng.lock, flags);
	for (i = 0; i < CONFIG_LKPI_TXQ_MAX && n < LKPI_TXQ_KICK_BATCH; i++)
		if (eng.slots[i].txq != NULL && eng.slots[i].queue.qlen > 0)
			batch[n++] = eng.slots[i].txq;
	spin_unlock_irqrestore(&eng.lock, flags);
	/* outside the engine lock: the driver's wake op takes its own lock and
	 * may run the driver's transmit path inline for the VO class */
	for (i = 0; i < n; i++)
		kick(hw, batch[i], arg);
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
#ifdef CONFIG_LKPI_TXQ_AUDIT
		if (!lkpi_txq_q_ok(&t->queue)) {
			aud.deq_bad++;
			lkpi_txq_crumb("deq_bad", &t->queue, skb, t->queue.qlen,
			    (u32)(t - eng.slots));
			lkpi_txq_dump_locked("deq_bad");
		}
#endif
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
	if (skb->len >= 500) {
		static unsigned trace_n;

		if (trace_n < 6) {
			const unsigned char *d = skb->data;

			trace_n++;
			lkpi_printf(ULOG_ERROR_LEVEL,
			    "txq: deq len=%u hr=%u first="
			    "%02x %02x %02x %02x %02x %02x %02x %02x "
			    "skb=%p\n",
			    (unsigned)skb->len, skb_headroom(skb),
			    d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
			    skb);
		}
	}
	/*
	 * The driver takes the frame from here straight into its own transmit
	 * path (rtw88's txq_push -> rtw_txq_push_skb -> hci tx_write), which
	 * pushes the chip descriptor on and then zeroes it without looking at
	 * the answer.  A frame that was queued with less room than the driver
	 * declared turns that zeroing into a write at the address the refusal
	 * returned; the board died of it on 2026-11-04.  Name the frame once
	 * and drop it: a short frame is a builder's bug, not a transmit.
	 */
	if (skb_headroom(skb) < hw->extra_tx_headroom) {
		static unsigned long short_frames;
		static bool said;

		short_frames++;
		if (!said) {
			const unsigned char *d = skb->data;

			said = true;
			lkpi_printf(ULOG_ERROR_LEVEL,
			    "lkpi_txq: dequeue fc=0x%04x len=%u has %u bytes of headroom, the driver declared %u - dropped\n",
			    (unsigned int)d[0] | ((unsigned int)d[1] << 8), skb->len,
			    skb_headroom(skb), hw->extra_tx_headroom);
			lkpi_printf(ULOG_ERROR_LEVEL,
			    "lkpi_txq: frame prio=%u qm=%u data=%08x head=%08x tail=%08x end=%08x bytes="
			    "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			    skb->priority, skb->queue_mapping,
			    (unsigned int)(unsigned long)skb->data,
			    (unsigned int)(unsigned long)skb->head,
			    (unsigned int)(unsigned long)skb->tail,
			    (unsigned int)(unsigned long)skb->end,
			    d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
			    d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
		}
		/*
		 * Do not free here: a frame at exactly the driver's descriptor
		 * size of headroom has already been through the driver's push
		 * (its owner still holds it), and freeing a second queue entry
		 * of it would hand the pool a live buffer.  Leaking one frame
		 * is the cheap side of that trade; the count says how often.
		 *
		 * Returning NULL ends the driver's dequeue loop, and nothing
		 * would come back for the frames behind this one: the queue then
		 * holds them and its pool slots with them (the 2026-11-04 S2C
		 * stall: txq stuck at 130, OUT endpoint idle).  Ask the port for
		 * another wake so the drop costs one frame, not the queue.
		 */
		lkpi80211_tx_wake_defer(hw);
		return NULL;
	}
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
