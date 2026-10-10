/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * main.c - the software transmit-queue engine's cases.
 *
 * The contract under test is upstream net/mac80211's scheduler: a queue is
 * handed to the driver once per scheduling round, a round is what
 * ieee80211_txq_schedule_start opens, next_txq answers NULL while no round is
 * open, a queue is only on the active list while it has frames (or the caller
 * forced it), and return_txq is the same call as schedule_txq with the
 * caller's force flag.  The frames themselves are the pool's real skbs.
 *
 * This file's own platform hooks (the pool's irq save/restore, log and heap)
 * live in platform_host.c: they need <stdlib.h>, and <stdlib.h> and the
 * interface's <linux/kernel.h> both declare abs(), so they cannot share a
 * translation unit (the same split the bridge suite makes).
 *
 * Exit status is 0 only when every case held.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_impl.h"
#include "linuxkpi_core.h"
#include "linuxkpi_txq.h"
#include "skbuff/skbuff_pool.h"

#include <net/mac80211.h>

#ifndef CONFIG_LKPI_TXQ_MAX
#define CONFIG_LKPI_TXQ_MAX	256
#endif

/* ------------------------------------------------------------------ */
/* the case harness                                                    */
/* ------------------------------------------------------------------ */

static unsigned int g_fail;
static unsigned int g_cases;

static void
case_line(const char *name, int ok, const char *fmt, ...)
{
	va_list ap;

	g_cases++;
	if (!ok)
		g_fail++;
	printf("TXQ: %s %s", name, ok ? "PASS" : "FAIL");
	if (fmt != NULL && *fmt != '\0') {
		printf(" ");
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}
	printf("\n");
	fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* the objects the engine is handed                                    */
/* ------------------------------------------------------------------ */

#define DRV_AREA	16u	/* the driver-private bytes per queue */

static struct ieee80211_hw g_hw;
static struct ieee80211_vif g_vif;
static struct ieee80211_vif g_vif2;
static struct ieee80211_sta g_sta;

static struct sk_buff *
frame(unsigned int len)
{
	struct sk_buff *skb = alloc_skb(len + 64u, GFP_KERNEL);

	if (skb == NULL)
		return NULL;
	if (skb_put(skb, len) == NULL) {
		kfree_skb(skb);
		return NULL;
	}
	return skb;
}

/* one frame per case, released by the case that made it */
static void
drop(struct sk_buff *skb)
{

	if (skb != NULL)
		kfree_skb(skb);
}

/* ------------------------------------------------------------------ */
/* the cases                                                           */
/* ------------------------------------------------------------------ */

static void
case_alloc_shape(void)
{
	struct ieee80211_txq *txq;
	unsigned char *priv;
	unsigned int i;
	int ok;

	txq = lkpi_txq_alloc(&g_hw, &g_vif, NULL, 0);
	if (txq == NULL) {
		case_line("alloc-shape-and-drv-area", 0, "no queue");
		return;
	}
	ok = txq->vif == &g_vif && txq->sta == NULL && txq->tid == 0 &&
	    txq->ac == IEEE80211_AC_BE;
	/*
	 * The driver's area must be exactly what it asked for: a write past it
	 * is the failure the sanitizer is here to catch, and a short write is
	 * the one the driver would take for its own state.
	 */
	priv = txq->drv_priv;
	for (i = 0; i < DRV_AREA; i++)
		priv[i] = (unsigned char)(0xa5u + i);
	for (i = 0; i < DRV_AREA; i++)
		if (priv[i] != (unsigned char)(0xa5u + i))
			ok = 0;
	case_line("alloc-shape-and-drv-area", ok, "vif=%u tid=%u ac=%u priv=%u",
	    txq->vif == &g_vif, txq->tid, txq->ac, DRV_AREA);
	lkpi_txq_free(txq);
}

static void
case_sta_ac_map(void)
{
	/* wme.c:23-32: BE BK BK BE VI VI VO VO for the low three bits of the TID */
	static const u8 want[IEEE80211_NUM_TIDS] = {
		IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_BK,
		IEEE80211_AC_BE, IEEE80211_AC_VI, IEEE80211_AC_VI,
		IEEE80211_AC_VO, IEEE80211_AC_VO,
		IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_BK,
		IEEE80211_AC_BE, IEEE80211_AC_VI, IEEE80211_AC_VI,
		IEEE80211_AC_VO, IEEE80211_AC_VO,
	};
	struct ieee80211_txq *txq[IEEE80211_NUM_TIDS];
	unsigned int i;
	int ok = 1;

	for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
		txq[i] = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, (u8)i);
		if (txq[i] == NULL) {
			ok = 0;
			break;
		}
		if (txq[i]->ac != want[i] || txq[i]->tid != (u8)i ||
		    txq[i]->sta != &g_sta)
			ok = 0;
	}
	case_line("sta-queue-ac-map", ok, "tids=%u", i);
	while (i > 0) {
		i--;
		lkpi_txq_free(txq[i]);
	}
}

static void
case_schedule_once_per_round(void)
{
	struct ieee80211_txq *txq, *first, *second, *after;
	struct sk_buff *f1, *f2;
	int ok;

	txq = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 0);
	f1 = frame(100);
	f2 = frame(200);
	if (txq == NULL || f1 == NULL || f2 == NULL) {
		case_line("schedule-once-per-round", 0, "setup");
		lkpi_txq_free(txq);
		drop(f1);
		drop(f2);
		return;
	}
	(void)lkpi_txq_enqueue(txq, f1);
	ieee80211_txq_schedule_start(&g_hw, txq->ac);
	ieee80211_schedule_txq(&g_hw, txq);
	first = ieee80211_next_txq(&g_hw, txq->ac);
	second = ieee80211_next_txq(&g_hw, txq->ac);	/* same round */
	/* the frame is still queued, so returning it puts it back on the list */
	ieee80211_return_txq(&g_hw, txq, false);
	ieee80211_txq_schedule_start(&g_hw, txq->ac);
	after = ieee80211_next_txq(&g_hw, txq->ac);
	ok = first == txq && second == NULL && after == txq;
	case_line("schedule-once-per-round", ok, "first=%u second=%u after=%u",
	    first == txq, second == NULL, after == txq);
	lkpi_txq_free(txq);
}

static void
case_no_round_no_handout(void)
{
	struct ieee80211_txq *txq;
	struct sk_buff *f;
	struct ieee80211_txq *got;
	int ok;

	txq = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 1);
	f = frame(64);
	if (txq == NULL || f == NULL) {
		case_line("no-round-no-handout", 0, "setup");
		lkpi_txq_free(txq);
		drop(f);
		return;
	}
	(void)lkpi_txq_enqueue(txq, f);
	ieee80211_schedule_txq(&g_hw, txq);
	/*
	 * No round has been opened on this AC, so the scheduler answers NULL -
	 * upstream's schedule_round[ac] == 0 guard (tx.c:3964).  The AC is the
	 * queue's own, so nothing else in this suite has opened it either: each
	 * case leaves the engine with empty lists.
	 */
	got = ieee80211_next_txq(&g_hw, txq->ac);
	ok = got == NULL;
	case_line("no-round-no-handout", ok, "got=%u", got != NULL);
	lkpi_txq_free(txq);
}

static void
case_empty_needs_force(void)
{
	struct ieee80211_txq *txq, *a, *b;
	int ok;

	txq = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 2);
	if (txq == NULL) {
		case_line("empty-queue-needs-force", 0, "setup");
		return;
	}
	/* no frames: the public form forces, the plain one does not */
	__ieee80211_schedule_txq(&g_hw, txq, false);
	ieee80211_txq_schedule_start(&g_hw, txq->ac);
	a = ieee80211_next_txq(&g_hw, txq->ac);
	ieee80211_schedule_txq(&g_hw, txq);
	b = ieee80211_next_txq(&g_hw, txq->ac);
	ok = a == NULL && b == txq;
	case_line("empty-queue-needs-force", ok, "plain=%u forced=%u",
	    a == NULL, b == txq);
	/* the forced queue answers with no frame, and an empty return drops it */
	ok = ieee80211_tx_dequeue(&g_hw, txq) == NULL;
	ieee80211_return_txq(&g_hw, txq, false);
	ieee80211_txq_schedule_start(&g_hw, txq->ac);
	case_line("return-skip-empty-drops",
	    ok && ieee80211_next_txq(&g_hw, txq->ac) == NULL, "dequeued-null=%u", ok);
	lkpi_txq_free(txq);
}

static void
case_dequeue_fifo_and_depth(void)
{
	struct ieee80211_txq *txq;
	struct sk_buff *in[3], *out[3];
	unsigned long frames = 99, bytes = 99;
	unsigned int i;
	int ok = 1;

	txq = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 3);
	if (txq == NULL) {
		case_line("dequeue-fifo-and-depth", 0, "setup");
		return;
	}
	for (i = 0; i < 3; i++) {
		in[i] = frame(100u + 100u * i);
		if (in[i] == NULL || lkpi_txq_enqueue(txq, in[i]) != 0) {
			case_line("dequeue-fifo-and-depth", 0, "enqueue %u", i);
			lkpi_txq_free(txq);
			return;
		}
	}
	ieee80211_txq_get_depth(txq, &frames, &bytes);
	if (frames != 3 || bytes != 100u + 200u + 300u)
		ok = 0;
	for (i = 0; i < 3; i++)
		out[i] = ieee80211_tx_dequeue(&g_hw, txq);
	/* first in, first out, and the same skbs come back */
	for (i = 0; i < 3; i++)
		if (out[i] != in[i])
			ok = 0;
	ieee80211_txq_get_depth(txq, &frames, &bytes);
	if (frames != 0 || bytes != 0)
		ok = 0;
	if (ieee80211_tx_dequeue(&g_hw, txq) != NULL)
		ok = 0;
	case_line("dequeue-fifo-and-depth", ok, "depth=%lu/%lu",
	    (unsigned long)frames, (unsigned long)bytes);
	for (i = 0; i < 3; i++)
		drop(out[i]);
	lkpi_txq_free(txq);
}

static void
case_dequeue_tags_vif(void)
{
	struct ieee80211_txq *txq;
	struct sk_buff *f, *got;
	struct ieee80211_tx_info *info;
	int ok;

	txq = lkpi_txq_alloc(&g_hw, &g_vif2, &g_sta, 4);
	f = frame(80);
	if (txq == NULL || f == NULL) {
		case_line("dequeue-tags-the-vif", 0, "setup");
		lkpi_txq_free(txq);
		drop(f);
		return;
	}
	(void)lkpi_txq_enqueue(txq, f);
	got = ieee80211_tx_dequeue(&g_hw, txq);
	info = got != NULL ? IEEE80211_SKB_CB(got) : NULL;
	ok = got == f && info != NULL && info->control.vif == &g_vif2;
	case_line("dequeue-tags-the-vif", ok, "vif=%u",
	    info != NULL && info->control.vif == &g_vif2);
	drop(got);
	lkpi_txq_free(txq);
}

static void
case_round_robin(void)
{
	struct ieee80211_txq *q0, *q3, *a, *b, *c;
	struct sk_buff *f0, *f3;
	int ok;

	q0 = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 0);
	q3 = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 3);
	f0 = frame(60);
	f3 = frame(60);
	if (q0 == NULL || q3 == NULL || f0 == NULL || f3 == NULL) {
		case_line("round-robin-across-queues", 0, "setup");
		lkpi_txq_free(q0);
		lkpi_txq_free(q3);
		drop(f0);
		drop(f3);
		return;
	}
	/* both are AC_BE: the round walks them in the order they were scheduled */
	(void)lkpi_txq_enqueue(q0, f0);
	(void)lkpi_txq_enqueue(q3, f3);
	ieee80211_txq_schedule_start(&g_hw, q0->ac);
	ieee80211_schedule_txq(&g_hw, q0);
	ieee80211_schedule_txq(&g_hw, q3);
	a = ieee80211_next_txq(&g_hw, q0->ac);
	b = ieee80211_next_txq(&g_hw, q0->ac);
	c = ieee80211_next_txq(&g_hw, q0->ac);
	ok = a == q0 && b == q3 && c == NULL;
	case_line("round-robin-across-queues", ok, "a=%u b=%u c-null=%u",
	    a == q0, b == q3, c == NULL);
	lkpi_txq_free(q0);
	lkpi_txq_free(q3);
}

static void
case_return_force_next_round(void)
{
	struct ieee80211_txq *txq, *first, *same_round, *next_round;
	struct sk_buff *f;
	int ok;

	txq = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 5);
	f = frame(120);
	if (txq == NULL || f == NULL) {
		case_line("return-force-requeues-next-round", 0, "setup");
		lkpi_txq_free(txq);
		drop(f);
		return;
	}
	(void)lkpi_txq_enqueue(txq, f);
	ieee80211_txq_schedule_start(&g_hw, txq->ac);
	ieee80211_schedule_txq(&g_hw, txq);
	first = ieee80211_next_txq(&g_hw, txq->ac);
	drop(ieee80211_tx_dequeue(&g_hw, txq));	/* the queue is empty now */
	ieee80211_return_txq(&g_hw, txq, true);	/* force: still worth serving */
	same_round = ieee80211_next_txq(&g_hw, txq->ac);
	ieee80211_txq_schedule_start(&g_hw, txq->ac);
	next_round = ieee80211_next_txq(&g_hw, txq->ac);
	ok = first == txq && same_round == NULL && next_round == txq;
	case_line("return-force-requeues-next-round", ok,
	    "first=%u same-round-null=%u next=%u", first == txq,
	    same_round == NULL, next_round == txq);
	lkpi_txq_free(txq);
}

static void
case_purge(void)
{
	struct ieee80211_txq *txq;
	unsigned long frames = 99, bytes = 99;
	int ok;

	txq = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 6);
	if (txq == NULL) {
		case_line("purge-drops-and-unschedules", 0, "setup");
		return;
	}
	(void)lkpi_txq_enqueue(txq, frame(50));
	(void)lkpi_txq_enqueue(txq, frame(70));
	ieee80211_txq_schedule_start(&g_hw, txq->ac);
	ieee80211_schedule_txq(&g_hw, txq);
	lkpi_txq_purge(txq);
	ieee80211_txq_get_depth(txq, &frames, &bytes);
	ok = frames == 0 && bytes == 0 &&
	    ieee80211_next_txq(&g_hw, txq->ac) == NULL;
	case_line("purge-drops-and-unschedules", ok, "depth=%lu/%lu",
	    (unsigned long)frames, (unsigned long)bytes);
	lkpi_txq_free(txq);
}

static void
case_free_sta(void)
{
	struct ieee80211_txq *txq[IEEE80211_NUM_TIDS];
	struct lkpi_skb_pool_stats before, after;
	unsigned int base, i, made = 0;
	int ok;

	base = lkpi_txq_in_use();
	lkpi_skb_pool_stats(&before);
	for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
		txq[i] = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, (u8)i);
		if (txq[i] == NULL)
			break;
		made++;
	}
	ok = made == IEEE80211_NUM_TIDS &&
	    lkpi_txq_in_use() == base + IEEE80211_NUM_TIDS;
	/* a frame still on one of them must go with the station */
	(void)lkpi_txq_enqueue(txq[3], frame(90));
	lkpi_txq_free_sta(&g_sta);
	lkpi_skb_pool_stats(&after);
	ok = ok && lkpi_txq_in_use() == base &&
	    after.skb_used == before.skb_used;
	case_line("free-sta-releases-its-queues", ok,
	    "made=%u in-use=%u skb=%u", made, lkpi_txq_in_use(),
	    after.skb_used - before.skb_used);
}

static void
case_free_vif_sweeps(void)
{
	struct ieee80211_txq *q;
	unsigned int base;
	int ok;

	base = lkpi_txq_in_use();
	q = lkpi_txq_alloc(&g_hw, &g_vif2, NULL, 0);
	ok = q != NULL;
	ok = ok && lkpi_txq_alloc(&g_hw, &g_vif2, &g_sta, 0) != NULL;
	ok = ok && lkpi_txq_alloc(&g_hw, &g_vif2, &g_sta, 5) != NULL;
	ok = ok && lkpi_txq_in_use() == base + 3;
	/* the sweep takes the vif's own queue and its stations' queues */
	lkpi_txq_free_vif(&g_vif2);
	ok = ok && lkpi_txq_in_use() == base;
	case_line("free-vif-sweeps-stations", ok, "in-use=%u", lkpi_txq_in_use());
}

static void
case_foreign_refused(void)
{
	static unsigned char foreign_buf[sizeof(struct ieee80211_txq) + 32];
	struct ieee80211_txq *foreign = (struct ieee80211_txq *)foreign_buf;
	struct sk_buff *f;
	unsigned long orphans = lkpi_txq_orphan_calls();
	int ok;

	memset(foreign_buf, 0, sizeof(foreign_buf));
	f = frame(40);
	if (f == NULL) {
		case_line("foreign-txq-refused", 0, "no frame");
		return;
	}
	ok = lkpi_txq_enqueue(foreign, f) == -EINVAL &&
	    lkpi_txq_orphan_calls() == orphans + 1;
	/* the frame never landed, so it is still this file's to release */
	drop(f);
	case_line("foreign-txq-refused", ok, "orphans=%lu",
	    lkpi_txq_orphan_calls() - orphans);
}

static void
case_table_full(void)
{
	static struct ieee80211_txq *held[CONFIG_LKPI_TXQ_MAX + 8];
	struct ieee80211_txq *again;
	u32 refusals = lkpi_txq_alloc_refusals();
	unsigned int base = lkpi_txq_in_use();
	unsigned int n = 0;
	int ok;

	while (n < CONFIG_LKPI_TXQ_MAX + 8u) {
		struct ieee80211_txq *q =
		    lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 0);

		if (q == NULL)
			break;
		held[n++] = q;
	}
	ok = n == CONFIG_LKPI_TXQ_MAX &&
	    lkpi_txq_alloc_refusals() == refusals + 1;
	case_line("table-full-refused", ok, "held=%u cap=%u refusals=%u", n,
	    (unsigned)CONFIG_LKPI_TXQ_MAX, lkpi_txq_alloc_refusals() - refusals);
	/* one slot given back is a slot that works again */
	if (n > 0) {
		lkpi_txq_free(held[n - 1]);
		held[n - 1] = NULL;
	}
	again = lkpi_txq_alloc(&g_hw, &g_vif, &g_sta, 0);
	case_line("table-slot-reusable", again != NULL, "again=%u", again != NULL);
	lkpi_txq_free(again);
	while (n > 0) {
		n--;
		lkpi_txq_free(held[n]);
	}
	ok = lkpi_txq_in_use() == base;
	case_line("table-drained", ok, "in-use=%u", lkpi_txq_in_use());
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static void
app_task(void *arg)
{

	(void)arg;
	case_alloc_shape();
	case_sta_ac_map();
	case_schedule_once_per_round();
	case_no_round_no_handout();
	case_empty_needs_force();
	case_dequeue_fifo_and_depth();
	case_dequeue_tags_vif();
	case_round_robin();
	case_return_force_next_round();
	case_purge();
	case_free_sta();
	case_free_vif_sweeps();
	case_foreign_refused();
	case_table_full();
	printf("TXQ: summary cases=%u failed=%u\n", g_cases, g_fail);
	fflush(stdout);
	cmsis2_host_end_scheduler();
}

int
main(void)
{
	osThreadAttr_t attr = { 0 };
	osThreadId_t t;

	if (osKernelInitialize() != osOK) {
		printf("TXQ: kernel init failed\n");
		return 2;
	}
	lkpi_core_init();
	lkpi_skb_pool_init();
	memset(&g_hw, 0, sizeof(g_hw));
	g_hw.txq_data_size = (int)DRV_AREA;
	memset(&g_vif, 0, sizeof(g_vif));
	memset(&g_vif2, 0, sizeof(g_vif2));
	memset(&g_sta, 0, sizeof(g_sta));
	attr.name = "lkpi-txq";
	attr.stack_size = 16384u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("TXQ: cannot create the case task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return g_fail == 0 ? 0 : 1;
}
