/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   sync.c
 * @brief  Completion, mutex and semaphore semantics.
 *
 * `sync_mutex` checks the interface's answer, not a re-entrant one: Linux's
 * mutex is not recursive, so a second take from another context waits and a
 * trylock from the owner fails.  `sync_completion` checks the timeout arm and
 * the sticky complete() arm.
 */
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"
#include <linux/semaphore.h>

#include "kpi_test.h"

int kpi_case_sync_completion(void)
{
	struct completion c;
	unsigned long left;

	init_completion(&c);
	left = wait_for_completion_timeout(&c, msecs_to_jiffies(50));
	KPI_ASSERT_EQ(left, 0, "timeout returns 0");
	complete(&c);
	left = wait_for_completion_timeout(&c, msecs_to_jiffies(50));
	KPI_ASSERT(left != 0, "complete() wakes the waiter");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* mutex                                                               */
/* ------------------------------------------------------------------ */

static struct mutex mtx;
static osSemaphoreId_t mutex_done;
static volatile int mutex_try_ret = -1;

static void mutex_helper(void *arg)
{
	(void)arg;
	/* the owner holds it: a trylock from another context must fail.  Do not
	 * block here - the owner is waiting on this case's semaphore, so a
	 * blocking take would deadlock the test, not the component. */
	mutex_try_ret = mutex_trylock(&mtx);
	(void)osSemaphoreRelease(mutex_done);
	osThreadExit();		/* a FreeRTOS task must not return */
}

int kpi_case_sync_mutex(void)
{
	osThreadId_t t;
	osThreadAttr_t attr;

	mutex_init(&mtx);
	if (mutex_done == NULL)
		mutex_done = osSemaphoreNew(1u, 0u, NULL);
	KPI_ASSERT(mutex_done != NULL, "semaphore");

	mutex_lock(&mtx);
	memset(&attr, 0, sizeof(attr));
	attr.name = "mutex-helper";
	attr.stack_size = 4096u;
	attr.priority = osPriorityNormal;
	t = osThreadNew((osThreadFunc_t)mutex_helper, NULL, &attr);
	KPI_ASSERT(t != NULL, "helper thread");
	KPI_ASSERT(osSemaphoreAcquire(mutex_done, 2000u) == osOK,
		   "helper finished");
	KPI_ASSERT_EQ(mutex_try_ret, 0, "trylock fails while held");
	mutex_unlock(&mtx);
	KPI_ASSERT(mutex_trylock(&mtx) != 0, "trylock succeeds after release");
	mutex_unlock(&mtx);
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* semaphore                                                           */
/* ------------------------------------------------------------------ */

int kpi_case_sync_semaphore(void)
{
	struct semaphore s;

	sema_init(&s, 2);
	down(&s);
	down(&s);
	KPI_ASSERT(down_trylock(&s) != 0, "count exhausted");
	up(&s);
	KPI_ASSERT(down_trylock(&s) == 0, "up restores one permit");
	up(&s);
	return KPI_PASS;
}
