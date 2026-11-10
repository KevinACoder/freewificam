/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   timer.c
 * @brief  Timer semantics: one-shot expiry, re-arm, cancel.
 *
 * Tolerances are loose on purpose: the host tick is the POSIX port's, not the
 * board's GIC tick; the cases pin ordering and the "does not fire" answers, not
 * jitter (README.md, "what this bench cannot prove").
 */
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"

#include "kpi_test.h"

static osSemaphoreId_t fired;

static void timer_cb(struct timer_list *t)
{
	(void)t;
	(void)osSemaphoreRelease(fired);
}

static int ensure_sem(void)
{
	if (fired == NULL)
		fired = osSemaphoreNew(4u, 0u, NULL);
	return fired != NULL ? 0 : 1;
}

int kpi_case_timer_due(void)
{
	struct timer_list t;
	uint64_t t0, dt;

	KPI_ASSERT(ensure_sem() == 0, "semaphore");
	memset(&t, 0, sizeof(t));
	timer_setup(&t, timer_cb, 0);
	t0 = kpi_now_ms();
	KPI_ASSERT(mod_timer(&t, jiffies + msecs_to_jiffies(50)) == 0 ||
		   mod_timer(&t, jiffies + msecs_to_jiffies(50)) == 1,
		   "timer armed");
	KPI_ASSERT(osSemaphoreAcquire(fired, 1000u) == osOK, "timer fired");
	dt = kpi_now_ms() - t0;
	KPI_ASSERT(dt >= 40u, "did not fire early");
	KPI_ASSERT(osSemaphoreAcquire(fired, 100u) != osOK,
		   "one-shot did not fire twice");
	del_timer_sync(&t);
	return KPI_PASS;
}

int kpi_case_timer_rearm(void)
{
	struct timer_list t;
	uint64_t t0;

	KPI_ASSERT(ensure_sem() == 0, "semaphore");
	(void)osSemaphoreAcquire(fired, 1u);	/* drain */
	memset(&t, 0, sizeof(t));
	timer_setup(&t, timer_cb, 0);
	(void)mod_timer(&t, jiffies + msecs_to_jiffies(300));
	(void)osDelay(20);
	t0 = kpi_now_ms();
	(void)mod_timer(&t, jiffies + msecs_to_jiffies(80));
	KPI_ASSERT(osSemaphoreAcquire(fired, 1000u) == osOK, "re-armed timer fired");
	KPI_ASSERT(kpi_now_ms() - t0 < 250u, "fired at the new expiry");
	del_timer_sync(&t);
	return KPI_PASS;
}

int kpi_case_timer_cancel(void)
{
	struct timer_list t;

	KPI_ASSERT(ensure_sem() == 0, "semaphore");
	(void)osSemaphoreAcquire(fired, 1u);	/* drain */
	memset(&t, 0, sizeof(t));
	timer_setup(&t, timer_cb, 0);
	(void)mod_timer(&t, jiffies + msecs_to_jiffies(80));
	KPI_ASSERT(del_timer_sync(&t) != 0, "cancel saw the pending timer");
	KPI_ASSERT(osSemaphoreAcquire(fired, 200u) != osOK,
		   "cancelled timer did not fire");
	return KPI_PASS;
}
