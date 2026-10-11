/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_wiphy_work.c
 * @brief   The serializing wiphy work family: the queue/cancel/flush entry points
 *          the interface declares beside its two inline initializers.
 *
 * @design serializing work list + one drain item - the shape of upstream
 *         net/wireless/core.c:1258 (cfg80211_process_wiphy_works: pop the head,
 *         run it with the lock dropped, repeat), :1879-1893 (wiphy_work_queue:
 *         the list insert is idempotent - `if (list_empty(&work->entry))` - and
 *         the worker is kicked afterwards), :1895-1909 (cancel: unlink, and wait
 *         when the work is the one running), :1911-1926 (flush: process the list
 *         synchronously in the caller's context) and :1928-1960 (the delayed
 *         variant: a timer that queues the work, and cancel/flush that delete
 *         the timer first).
 *         One wiphy per image: the interface's per-wiphy list and its
 *         registered-device worker collapse to one list and one drain item on
 *         the system workqueue, and the wiphy handed to a work's callback is the
 *         one recorded by the queue call that enqueued it.
 * @date    10.10.2026
 */

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"

#include <net/cfg80211.h>

/*
 * The one list and its drain item.  `running` is the work the drain is inside
 * (so a cancel can tell "still queued" from "already running"), `done` is
 * signalled when it clears, and `drain_tid` is the drain thread itself - a work
 * that cancels itself must not wait for its own completion.
 */
static struct {
	struct work_struct	drain;
	struct list_head	list;
	struct wiphy_work	*running;
	struct wiphy		*wiphy;
	osThreadId_t		drain_tid;
	spinlock_t		lock;
	bool			init;
	u32			refused_uninit;
} ww;

static void	lkpi_wiphy_work_drain(struct work_struct *w);
static void	lkpi_wiphy_work_process(struct wiphy_work *stop);

static void
lkpi_wiphy_work_setup(void)
{
	os_critical_state_t csr;

	if (ww.init)
		return;
	csr = osCriticalEnter();
	if (!ww.init) {
		INIT_LIST_HEAD(&ww.list);
		spin_lock_init(&ww.lock);
		lkpi_work_init(&ww.drain, lkpi_wiphy_work_drain);
		ww.init = true;
	}
	osCriticalExit(csr);
}

/*
 * Run one queued work, or return without doing anything.  The callback runs
 * with the lock dropped (upstream does the same: a work may queue another one,
 * and the rtw89 family does), and `running` stays set across it so a concurrent
 * cancel waits for exactly this callback.
 */
static int
lkpi_wiphy_work_run_one(void)
{
	struct wiphy_work *work = NULL;
	struct wiphy *wiphy = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ww.lock, flags);
	if (!list_empty(&ww.list)) {
		work = list_first_entry(&ww.list, struct wiphy_work, entry);
		list_del_init(&work->entry);
		ww.running = work;
		wiphy = ww.wiphy;
	}
	spin_unlock_irqrestore(&ww.lock, flags);
	if (work == NULL)
		return 0;

	ww.drain_tid = osThreadGetId();
	if (work->func != NULL)
		work->func(wiphy, work);
	ww.drain_tid = NULL;

	spin_lock_irqsave(&ww.lock, flags);
	ww.running = NULL;
	spin_unlock_irqrestore(&ww.lock, flags);
	return 1;
}

static void
lkpi_wiphy_work_drain(struct work_struct *w)
{

	(void)w;
	while (lkpi_wiphy_work_run_one())
		;
}

/*
 * The synchronous drain: what the worker would do, done on the caller's thread
 * (upstream's flush path).  With `stop` non-NULL the walk ends after that work
 * has run, which is what makes flush(work) a "this one has run" guarantee.
 */
static void
lkpi_wiphy_work_process(struct wiphy_work *stop)
{

	for (;;) {
		struct wiphy_work *work = NULL;
		struct wiphy *wiphy = NULL;
		unsigned long flags;

		spin_lock_irqsave(&ww.lock, flags);
		if (!list_empty(&ww.list)) {
			work = list_first_entry(&ww.list, struct wiphy_work,
			    entry);
			list_del_init(&work->entry);
			ww.running = work;
			wiphy = ww.wiphy;
		}
		spin_unlock_irqrestore(&ww.lock, flags);
		if (work == NULL)
			return;
		/* the synchronous path is a drain thread too: a work that cancels
		 * itself must see "this is me" here as well */
		ww.drain_tid = osThreadGetId();
		if (work->func != NULL)
			work->func(wiphy, work);
		ww.drain_tid = NULL;
		spin_lock_irqsave(&ww.lock, flags);
		ww.running = NULL;
		spin_unlock_irqrestore(&ww.lock, flags);
		if (work == stop)
			return;
	}
}

/* ------------------------------------------------------------------ */
/* the seven entry points                                              */
/* ------------------------------------------------------------------ */

void
wiphy_work_queue(struct wiphy *wiphy, struct wiphy_work *work)
{
	unsigned long flags;

	if (work == NULL)
		return;
	if (work->func == NULL) {
		/* the interface's init stores the callback; a work that reaches
		 * the queue without one was never initialized, and upstream
		 * would call a NULL pointer */
		ww.refused_uninit++;
		return;
	}
	lkpi_wiphy_work_setup();
	spin_lock_irqsave(&ww.lock, flags);
	ww.wiphy = wiphy;
	/* idempotent: a work already on the list is queued once */
	if (list_empty(&work->entry))
		list_add_tail(&work->entry, &ww.list);
	spin_unlock_irqrestore(&ww.lock, flags);
	(void)lkpi_queue_work(lkpi_system_wq, &ww.drain);
}

void
wiphy_work_cancel(struct wiphy *wiphy, struct wiphy_work *work)
{
	unsigned long flags;
	bool running;

	(void)wiphy;
	if (work == NULL)
		return;
	lkpi_wiphy_work_setup();
	spin_lock_irqsave(&ww.lock, flags);
	if (!list_empty(&work->entry))
		list_del_init(&work->entry);
	running = (ww.running == work);
	spin_unlock_irqrestore(&ww.lock, flags);
	/*
	 * A cancel issued from the work itself (rtw89 cancels its own delayed
	 * work) must return instead of waiting for its own callback.  The wait
	 * itself polls the running marker rather than a completion object: this
	 * port's completion is sticky by contract (complete_all survives every
	 * waiter, which is what makes it usable as a latch), so it cannot gate
	 * "the callback that is running now"; the marker the drain clears after
	 * the callback returned is exactly that fact.  The callback is a work
	 * item, so the wait is short.
	 */
	if (running && ww.drain_tid != osThreadGetId()) {
		while (running) {
			(void)osDelay(1);
			spin_lock_irqsave(&ww.lock, flags);
			running = (ww.running == work);
			spin_unlock_irqrestore(&ww.lock, flags);
		}
	}
}

void
wiphy_work_flush(struct wiphy *wiphy, struct wiphy_work *work)
{
	unsigned long flags;
	bool queued;

	(void)wiphy;
	lkpi_wiphy_work_setup();
	if (work == NULL) {
		lkpi_wiphy_work_process(NULL);
		return;
	}
	spin_lock_irqsave(&ww.lock, flags);
	queued = !list_empty(&work->entry) || ww.running == work;
	spin_unlock_irqrestore(&ww.lock, flags);
	if (queued && ww.drain_tid != osThreadGetId())
		lkpi_wiphy_work_process(work);
}

static void
lkpi_wiphy_delayed_expire(struct timer_list *t)
{
	struct wiphy_delayed_work *dwork =
	    container_of(t, struct wiphy_delayed_work, timer);

	wiphy_work_queue(dwork->wiphy, &dwork->work);
}

void
wiphy_delayed_work_queue(struct wiphy *wiphy, struct wiphy_delayed_work *dwork,
    unsigned long delay)
{

	if (dwork == NULL)
		return;
	dwork->wiphy = wiphy;
	if (delay == 0) {
		/* upstream's short circuit: no timer round for an immediate
		 * queue */
		wiphy_work_queue(wiphy, &dwork->work);
		return;
	}
	lkpi_wiphy_work_setup();
	lkpi_timer_setup(&dwork->timer, lkpi_wiphy_delayed_expire);
	(void)lkpi_mod_timer(&dwork->timer, jiffies + delay);
}

void
wiphy_delayed_work_cancel(struct wiphy *wiphy, struct wiphy_delayed_work *dwork)
{

	if (dwork == NULL)
		return;
	(void)lkpi_del_timer_sync(&dwork->timer);
	wiphy_work_cancel(wiphy, &dwork->work);
}

void
wiphy_delayed_work_flush(struct wiphy *wiphy, struct wiphy_delayed_work *dwork)
{

	if (dwork == NULL)
		return;
	(void)lkpi_del_timer_sync(&dwork->timer);
	wiphy_work_flush(wiphy, &dwork->work);
}

/* the queue's own evidence: how many uninitialized works were refused */
u32
lkpi_wiphy_work_uninit_refusals(void)
{

	return ww.refused_uninit;
}
