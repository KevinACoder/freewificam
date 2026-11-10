/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   work.c
 * @brief  Workqueue semantics: FIFO order, coalescing, flush, real cancel.
 *
 * The four cases pin what the interface promises and what mac80211 relies on.
 * `work_cancel` is the one that used to fail: the port's cancel_work_sync() was
 * a flush (it ran the pending body instead of dropping it), which in a
 * teardown path is a use-after-free waiting to happen.  The body now marks the
 * queued item so the worker consumes it without running it.
 */
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"

#include "kpi_test.h"

static osSemaphoreId_t gate;
static osSemaphoreId_t done;
static osSemaphoreId_t cancel_done;
static volatile int blocker_running;

static void blocker_fn(struct work_struct *w)
{
	(void)w;
	blocker_running = 1;
	(void)osSemaphoreAcquire(gate, osWaitForever);
}

/* Park the single worker thread inside a callback.  Returns 0 when it is
 * parked; the caller must release `gate` before it returns. */
static int block_worker(struct work_struct *b)
{
	int i;

	blocker_running = 0;
	lkpi_work_init(b, blocker_fn);
	if (!schedule_work(b))
		return 1;
	for (i = 0; i < 500 && blocker_running == 0; i++)
		(void)osDelay(1);
	return blocker_running != 0 ? 0 : 1;
}

static int make_sems(void)
{
	if (gate == NULL)
		gate = osSemaphoreNew(1u, 0u, NULL);
	if (done == NULL)
		done = osSemaphoreNew(8u, 0u, NULL);
	return (gate != NULL && done != NULL) ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* order                                                               */
/* ------------------------------------------------------------------ */

static int order[3];
static int order_n;

static void order0(struct work_struct *w) { (void)w; order[order_n++] = 0; (void)osSemaphoreRelease(done); }
static void order1(struct work_struct *w) { (void)w; order[order_n++] = 1; (void)osSemaphoreRelease(done); }
static void order2(struct work_struct *w) { (void)w; order[order_n++] = 2; (void)osSemaphoreRelease(done); }

int kpi_case_work_order(void)
{
	struct work_struct a, b, c;
	int i;

	KPI_ASSERT(make_sems() == 0, "semaphores");
	order_n = 0;
	lkpi_work_init(&a, order0);
	lkpi_work_init(&b, order1);
	lkpi_work_init(&c, order2);
	KPI_ASSERT(schedule_work(&a), "queue a");
	KPI_ASSERT(schedule_work(&b), "queue b");
	KPI_ASSERT(schedule_work(&c), "queue c");
	for (i = 0; i < 3; i++)
		KPI_ASSERT(osSemaphoreAcquire(done, 1000u) == osOK, "callback ran");
	KPI_ASSERT_EQ(order_n, 3, "three callbacks");
	KPI_ASSERT(order[0] == 0 && order[1] == 1 && order[2] == 2,
		   "FIFO order");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* coalescing                                                          */
/* ------------------------------------------------------------------ */

static volatile int coalesce_runs;
static void coalesce_fn(struct work_struct *w)
{
	(void)w;
	coalesce_runs++;
	(void)osSemaphoreRelease(done);
}

int kpi_case_work_coalesce(void)
{
	struct work_struct b, w;

	KPI_ASSERT(make_sems() == 0, "semaphores");
	KPI_ASSERT(block_worker(&b) == 0, "worker parked");
	coalesce_runs = 0;
	lkpi_work_init(&w, coalesce_fn);
	KPI_ASSERT(schedule_work(&w), "first queue accepted");
	KPI_ASSERT(!schedule_work(&w), "second queue rejected (already queued)");
	(void)osSemaphoreRelease(gate);
	KPI_ASSERT(osSemaphoreAcquire(done, 1000u) == osOK, "callback ran");
	(void)osDelay(20);
	KPI_ASSERT_EQ(coalesce_runs, 1, "coalesced to one run");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* flush                                                               */
/* ------------------------------------------------------------------ */

static volatile int flush_ran;
static void flush_fn(struct work_struct *w)
{
	(void)w;
	flush_ran++;
	(void)osSemaphoreRelease(done);
}

int kpi_case_work_flush(void)
{
	struct work_struct b, w;

	KPI_ASSERT(make_sems() == 0, "semaphores");
	KPI_ASSERT(block_worker(&b) == 0, "worker parked");
	flush_ran = 0;
	lkpi_work_init(&w, flush_fn);
	KPI_ASSERT(schedule_work(&w), "queue accepted");
	(void)osSemaphoreRelease(gate);
	lkpi_work_flush(&w);
	KPI_ASSERT_EQ(flush_ran, 1, "flush returns after the body");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* cancel                                                              */
/* ------------------------------------------------------------------ */

static volatile int victim_ran;
static volatile int cancel_ret;
static struct work_struct victim;

static void victim_fn(struct work_struct *w)
{
	(void)w;
	victim_ran = 1;
}

static void canceller(void *arg)
{
	(void)arg;
	cancel_ret = cancel_work_sync(&victim);
	(void)osSemaphoreRelease(cancel_done);
	osThreadExit();		/* a FreeRTOS task must not return */
}

int kpi_case_work_cancel(void)
{
	struct work_struct b;
	osThreadId_t t;
	osThreadAttr_t attr;

	KPI_ASSERT(make_sems() == 0, "semaphores");
	if (cancel_done == NULL)
		cancel_done = osSemaphoreNew(1u, 0u, NULL);
	KPI_ASSERT(cancel_done != NULL, "cancel semaphore");

	KPI_ASSERT(block_worker(&b) == 0, "worker parked");
	victim_ran = 0;
	cancel_ret = -1;
	lkpi_work_init(&victim, victim_fn);
	KPI_ASSERT(schedule_work(&victim), "victim queued");

	memset(&attr, 0, sizeof(attr));
	attr.name = "canceller";
	attr.stack_size = 4096u;
	attr.priority = osPriorityNormal;
	t = osThreadNew((osThreadFunc_t)canceller, NULL, &attr);
	KPI_ASSERT(t != NULL, "canceller thread");

	(void)osDelay(20);			/* let cancel mark the item */
	(void)osSemaphoreRelease(gate);		/* worker reaches the item */
	KPI_ASSERT(osSemaphoreAcquire(cancel_done, 2000u) == osOK,
		   "cancel returned");
	KPI_ASSERT_EQ(cancel_ret, 1, "cancel reports it was pending");
	KPI_ASSERT_EQ(victim_ran, 0, "pending body was not run");
	return KPI_PASS;
}
