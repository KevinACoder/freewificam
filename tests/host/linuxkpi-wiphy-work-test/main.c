/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * main.c - the serializing wiphy work cases.
 *
 * The contract under test is upstream net/wireless/core.c's: one ordered list
 * drained by one worker, an idempotent queue, a cancel that unlinks or waits
 * for the running callback, a flush that processes synchronously on the
 * caller's thread, and a delayed variant whose timer queues the work.
 *
 * Exit status is 0 only when every case held.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"
#include "linuxkpi_wiphy_work.h"

#include <net/cfg80211.h>

static struct wiphy g_wiphy;

static unsigned int g_fail;
static unsigned int g_cases;

static void
case_line(const char *name, int ok, const char *fmt, ...)
{
	va_list ap;

	g_cases++;
	if (!ok)
		g_fail++;
	printf("WIPHYWORK: %s %s", name, ok ? "PASS" : "FAIL");
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
/* the counted work                                                    */
/* ------------------------------------------------------------------ */

static unsigned int g_ran;	/* the counted work's runs */
static osThreadId_t g_ran_tid;
static struct wiphy *g_seen_wiphy;
static struct wiphy_work g_work;

static void
counting_work(struct wiphy *wiphy, struct wiphy_work *work)
{

	(void)work;
	g_ran++;
	g_ran_tid = osThreadGetId();
	g_seen_wiphy = wiphy;
}

static void
reset_work(void)
{

	g_ran = 0;
	g_ran_tid = NULL;
	g_seen_wiphy = NULL;
	memset(&g_work, 0, sizeof(g_work));
	wiphy_work_init(&g_work, counting_work);
}

static void
case_queue_and_idempotent(void)
{
	reset_work();
	wiphy_work_queue(&g_wiphy, &g_work);
	wiphy_work_queue(&g_wiphy, &g_work);
	wiphy_work_flush(&g_wiphy, &g_work);
	case_line("queue-once", g_ran == 1, "ran=%u", g_ran);
	case_line("queue-twice-idempotent", g_ran == 1, "ran=%u", g_ran);
	case_line("queue-passes-the-wiphy", g_seen_wiphy == &g_wiphy,
	    "wiphy=%p want=%p", (void *)g_seen_wiphy, (void *)&g_wiphy);
}

static void
case_cancel_before_run(void)
{
	reset_work();
	wiphy_work_queue(&g_wiphy, &g_work);
	wiphy_work_cancel(&g_wiphy, &g_work);
	wiphy_work_flush(&g_wiphy, NULL);
	case_line("cancel-before-run", g_ran == 0, "ran=%u", g_ran);
}

static void
case_flush_synchronous(void)
{
	reset_work();
	wiphy_work_queue(&g_wiphy, &g_work);
	wiphy_work_flush(&g_wiphy, &g_work);
	/* the flush path runs the callback on the caller's thread (upstream's
	 * cfg80211_process_wiphy_works does exactly that) */
	case_line("flush-synchronous",
	    g_ran == 1 && g_ran_tid == osThreadGetId(),
	    "ran=%u tid_match=%d", g_ran, g_ran_tid == osThreadGetId());
}

/* ------------------------------------------------------------------ */
/* the blocking work (cancel waits for the running callback)           */
/* ------------------------------------------------------------------ */

static osSemaphoreId_t g_entered;
static osSemaphoreId_t g_release;
static osSemaphoreId_t g_in_cancel;
static struct wiphy_work g_blocking;
static unsigned int g_ran;
static int g_cancel_saw_ran;		/* g_ran's value when the cancel returned */
static int g_cancel_returned;

static void
blocking_work(struct wiphy *wiphy, struct wiphy_work *work)
{

	(void)wiphy;
	(void)work;
	(void)osSemaphoreRelease(g_entered);
	(void)osSemaphoreAcquire(g_release, osWaitForever);
	g_ran++;
}

static void
canceller_task(void *arg)
{

	(void)arg;
	/* wait until the callback is inside the work, then cancel it: the cancel
	 * must return only after the callback finished */
	(void)osSemaphoreAcquire(g_entered, osWaitForever);
	(void)osSemaphoreRelease(g_in_cancel);
	wiphy_work_cancel(&g_wiphy, &g_blocking);
	g_cancel_saw_ran = (int)g_ran;
	g_cancel_returned = 1;
	osThreadExit();		/* a task must not return to its caller */
}

static void
case_cancel_running_waits(void)
{
	osSemaphoreAttr_t sattr = { 0 };
	osThreadAttr_t attr = { 0 };
	osThreadId_t t;

	g_ran = 0;
	g_cancel_saw_ran = 0;
	g_cancel_returned = 0;
	sattr.name = "ww-sem";
	g_entered = osSemaphoreNew(1, 0, &sattr);
	g_release = osSemaphoreNew(1, 0, &sattr);
	g_in_cancel = osSemaphoreNew(1, 0, &sattr);
	if (g_entered == NULL || g_release == NULL || g_in_cancel == NULL) {
		case_line("cancel-running-waits", 0, "semaphore create failed");
		return;
	}
	wiphy_work_init(&g_blocking, blocking_work);
	attr.name = "ww-cancel";
	attr.stack_size = 8192u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(canceller_task, NULL, &attr);
	wiphy_work_queue(&g_wiphy, &g_blocking);
	if (t == NULL) {
		case_line("cancel-running-waits", 0, "no canceller thread");
		return;
	}
	/* the canceller is inside the cancel by now; let the callback finish, so
	 * the cancel can return - and it must report that it saw the finished
	 * callback, which is what proves it waited */
	(void)osSemaphoreAcquire(g_in_cancel, osWaitForever);
	(void)osDelay(2);
	(void)osSemaphoreRelease(g_release);
	(void)osDelay(4);
	case_line("cancel-running-waits",
	    g_ran == 1 && g_cancel_returned == 1 && g_cancel_saw_ran == 1,
	    "ran=%u returned=%d saw_ran=%d", g_ran, g_cancel_returned,
	    g_cancel_saw_ran);
	osSemaphoreDelete(g_entered);
	osSemaphoreDelete(g_release);
	osSemaphoreDelete(g_in_cancel);
}

/* ------------------------------------------------------------------ */
/* the delayed variant                                                 */
/* ------------------------------------------------------------------ */

static struct wiphy_delayed_work g_dwork;

static void
case_delayed(void)
{
	osStatus_t st;
	unsigned int ran;

	g_ran = 0;
	memset(&g_dwork, 0, sizeof(g_dwork));
	wiphy_delayed_work_init(&g_dwork, counting_work);
	wiphy_delayed_work_queue(&g_wiphy, &g_dwork, 2);
	/* not yet: the timer has two ticks to run */
	(void)osDelay(1);
	ran = g_ran;
	st = osDelay(5);
	case_line("delayed-fires-late", ran == 0, "ran_at_1tick=%u st=%d", ran,
	    (int)st);
	case_line("delayed-fires", g_ran == 1, "ran=%u", g_ran);

	g_ran = 0;
	memset(&g_dwork, 0, sizeof(g_dwork));
	wiphy_delayed_work_init(&g_dwork, counting_work);
	wiphy_delayed_work_queue(&g_wiphy, &g_dwork, 3);
	wiphy_delayed_work_cancel(&g_wiphy, &g_dwork);
	(void)osDelay(6);
	case_line("delayed-cancel", g_ran == 0, "ran=%u", g_ran);
}

/* ------------------------------------------------------------------ */
/* the self-cancelling work and the uninitialized work                 */
/* ------------------------------------------------------------------ */

static struct wiphy_work g_self;
static unsigned int g_self_done;

static void
self_cancel_work(struct wiphy *wiphy, struct wiphy_work *work)
{

	wiphy_work_cancel(wiphy, work);	/* cancelling itself must not wait */
	g_self_done = 1;
}

static void
case_cancel_from_work(void)
{
	g_self_done = 0;
	memset(&g_self, 0, sizeof(g_self));
	wiphy_work_init(&g_self, self_cancel_work);
	wiphy_work_queue(&g_wiphy, &g_self);
	wiphy_work_flush(&g_wiphy, &g_self);
	case_line("cancel-from-work", g_self_done == 1, "done=%u", g_self_done);
}

static void
case_uninit_refused(void)
{
	struct wiphy_work uninit;
	u32 before = lkpi_wiphy_work_uninit_refusals();

	memset(&uninit, 0, sizeof(uninit));	/* func == NULL: never initialized */
	wiphy_work_queue(&g_wiphy, &uninit);
	wiphy_work_flush(&g_wiphy, NULL);
	case_line("uninit-queue-refused",
	    lkpi_wiphy_work_uninit_refusals() == before + 1,
	    "refusals=%u", lkpi_wiphy_work_uninit_refusals() - before);
}

static void
app_task(void *arg)
{

	(void)arg;
	case_queue_and_idempotent();
	case_cancel_before_run();
	case_flush_synchronous();
	case_cancel_running_waits();
	case_delayed();
	case_cancel_from_work();
	case_uninit_refused();
	printf("WIPHYWORK: summary cases=%u failed=%u\n", g_cases, g_fail);
	fflush(stdout);
	cmsis2_host_end_scheduler();
}

int
main(void)
{
	osThreadAttr_t attr = { 0 };
	osThreadId_t t;

	if (osKernelInitialize() != osOK) {
		printf("WIPHYWORK: kernel init failed\n");
		return 2;
	}
	lkpi_core_init();
	attr.name = "wiphy-work";
	attr.stack_size = 16384u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("WIPHYWORK: cannot create the case task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return g_fail == 0 ? 0 : 1;
}
