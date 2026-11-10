/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_txq_audit.h
 * @brief   The transmit-queue engine's invariant audit (CONFIG_LKPI_TXQ_AUDIT).
 *
 * @design the 2026-11-06 AGG-04 wedge left one reading that no counter
 *         explains: `netio: tx q=0` beside `netio: txqwalk qlen_walk=4096`,
 *         a pool holding 128/128 headers, and a bulk-out endpoint idle.  The
 *         engine's own bookkeeping (`qlen`) said empty, the node walk said
 *         thousands, and nothing in the image ever compared the two at the
 *         moment the disagreement started.  This audit compares them at every
 *         engine exit - O(1) per call, the walk only on a violation - and
 *         writes a slot dump the first few times it fails, so the boot that
 *         breaks names its own first break instead of its final state.
 *         It is observation only: no branch, no lock and no order changes.
 *         It exists only while the switch is on, because the comparison and
 *         the dump are the cost.
 * @date    06.11.2026
 */

#ifndef _LINUXKPI_TXQ_AUDIT_H_
#define _LINUXKPI_TXQ_AUDIT_H_

#include "linuxkpi_core.h"

struct sk_buff_head;

#ifdef CONFIG_LKPI_TXQ_AUDIT

/* A frame is released while it is still linked into a queue: the queue's
 * counter and its links are about to be repaired by the unlink, and if the
 * queue is one of the engine's, the counters say which. */
void	lkpi_txq_audit_free_linked(struct sk_buff_head *q, struct sk_buff *skb,
				   void *ra);

/* Called by the queue primitives (tail/head/detach/dequeue) after they finish:
 * the one place that can name which primitive broke a queue's invariant. */
void	lkpi_txq_audit_op(struct sk_buff *skb, struct sk_buff_head *q,
			   const char *op);

/* Called BEFORE a primitive edits its neighbour; true = that neighbour is not
 * part of this queue, so the edit would corrupt whichever chain it does
 * belong to.  The caller must then leave the queue alone. */
/* Called by the queue initializer before it empties the shape. */
void	lkpi_txq_audit_reset(struct sk_buff_head *q);

bool	lkpi_txq_audit_wt_link(struct sk_buff *skb, struct sk_buff_head *q,
			      const char *op, bool head);
bool	lkpi_txq_audit_wt_detach(struct sk_buff *skb, struct sk_buff_head *q,
				const char *op);

/* The counters line plus the all-slots walk, for the heartbeat and the cell's
 * own ledgers.  Safe to call from process context. */
void	lkpi_txq_audit_line(const char *tag);

/* Walk every slot - including the ones whose txq is NULL - and return the
 * number of nodes they link.  The engine skips the NULL slots (`lkpi_txq_list_len`),
 * so a nonzero count here over that one is frames no queue will ever reach. */
unsigned long lkpi_txq_walk_all(void);

/* Full slot dump; a violation calls the locked form itself. */
void	lkpi_txq_dump(const char *tag);

#else /* !CONFIG_LKPI_TXQ_AUDIT */

static __always_inline __unused void
lkpi_txq_audit_free_linked(struct sk_buff_head *q, struct sk_buff *skb,
    void *ra)
{
	(void)ra;
	(void)q;
	(void)skb;
}

static __always_inline __unused void
lkpi_txq_audit_reset(struct sk_buff_head *q)
{
	(void)q;
}

static __always_inline __unused bool
lkpi_txq_audit_wt_link(struct sk_buff *skb, struct sk_buff_head *q,
    const char *op, bool head)
{
	(void)skb;
	(void)q;
	(void)op;
	(void)head;
	return false;
}

static __always_inline __unused bool
lkpi_txq_audit_wt_detach(struct sk_buff *skb, struct sk_buff_head *q,
    const char *op)
{
	(void)skb;
	(void)q;
	(void)op;
	return false;
}

static __always_inline __unused void
lkpi_txq_audit_op(struct sk_buff *skb, struct sk_buff_head *q, const char *op)
{
	(void)skb;
	(void)q;
	(void)op;
}

static __always_inline __unused void
lkpi_txq_audit_line(const char *tag)
{
	(void)tag;
}

static __always_inline __unused unsigned long
lkpi_txq_walk_all(void)
{
	return 0;
}

static __always_inline __unused void
lkpi_txq_dump(const char *tag)
{
	(void)tag;
}

#endif /* CONFIG_LKPI_TXQ_AUDIT */

#endif /* _LINUXKPI_TXQ_AUDIT_H_ */
