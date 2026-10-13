/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * main.c - the polling-budget and completion-layout cases.
 *
 * Two contracts the target build depends on and cannot check for itself:
 *
 *   1. read_poll_timeout_atomic charges only the delay against its timeout,
 *      never the operation's own execution time (upstream include/linux/iopoll.h
 *      poll_timeout_us_atomic: `__left_ns -= __delay_ns` per round plus one
 *      nanosecond).  A wall-clock budget reports a slow-but-successful read as
 *      -ETIMEDOUT - the rtw89 W2 board round lost a C2H answer that took
 *      5.2-6.1 ms against a 4 ms window exactly that way.
 *   2. the completion object's slots hold the port's mutex and condvar shells
 *      (linuxkpi_impl.h pins them; the same board round proved a shell larger
 *      than its slot aliases `done` and complete_all's waiter loop then reads
 *      the sticky marker as its trip count).
 *
 * The clock on this port is the tick (1 kHz: lkpi_ktime_get is
 * osKernelGetTickCount * 1e6 ns, lkpi_udelay is one tick), so the cases are
 * written in ticks: delay 1000 us = 1 tick, timeout 3000 us = 3 ticks.
 *
 * Exit status is 0 only when every case held.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"
#include "linux/iopoll.h"

/* the completion layout, pinned at compile time beside the port's own pins */
_Static_assert(sizeof(((struct completion *)0)->_mtx) == 8 * sizeof(void *),
    "the completion's mutex slot must hold the port mutex shell");
_Static_assert(sizeof(((struct completion *)0)->_cv) == 12 * sizeof(void *),
    "the completion's condvar slot must hold the port condvar shell");

static unsigned int g_fail;
static unsigned int g_cases;

static void
case_line(const char *name, int ok, const char *fmt, ...)
{
	va_list ap;

	g_cases++;
	if (!ok)
		g_fail++;
	printf("TIMESEM: %s %s", name, ok ? "PASS" : "FAIL");
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
/* the polling cases                                                   */
/* ------------------------------------------------------------------ */

static unsigned int g_op_rounds;

/* a slow read: two ticks of work per round (the C2H-answer shape) */
static int
slow_read(void)
{

	g_op_rounds++;
	(void)osDelay(2);
	return g_op_rounds >= 3;	/* the answer arrives on the third round */
}

static int
never_true(void)
{

	g_op_rounds++;
	(void)osDelay(1);
	return 0;
}

static int
first_true(void)
{

	g_op_rounds++;
	return 1;
}

static void
case_slow_op(void)
{
	int val = 0;
	int rc;

	g_op_rounds = 0;
	rc = read_poll_timeout_atomic(slow_read, val, val, 1000, 3000, false);
	/* The operation ran three rounds of 2 ticks each (6 ms of real time)
	 * against a 3 ms budget: only a budget that charges the delay alone
	 * can still return the answer. */
	case_line("atomic-slow-op-succeeds", rc == 0 && val != 0,
	    "rc=%d rounds=%u", rc, g_op_rounds);
}

static void
case_budget_rounds(void)
{
	int val = 0;
	int rc;

	g_op_rounds = 0;
	rc = read_poll_timeout_atomic(never_true, val, val, 1000, 3000, false);
	/* 3000 us of budget at 1000 us of delay per round: five reads (the
	 * fifth sees the expired budget the fourth left). */
	case_line("atomic-never-true-times-out", rc == -ETIMEDOUT,
	    "rc=%d rounds=%u", rc, g_op_rounds);
	case_line("atomic-budget-counts-the-delay-only",
	    g_op_rounds >= 4 && g_op_rounds <= 6, "rounds=%u", g_op_rounds);
}

static void
case_delay_before_read(void)
{
	int val = 0;
	unsigned int with_before, without_before;
	int rc;

	g_op_rounds = 0;
	(void)read_poll_timeout_atomic(never_true, val, val, 1000, 3000, false);
	without_before = g_op_rounds;
	g_op_rounds = 0;
	rc = read_poll_timeout_atomic(never_true, val, val, 1000, 3000, true);
	with_before = g_op_rounds;
	/* delay_before_read charges its delay to the budget up front, so the
	 * loop gets one fewer round. */
	case_line("atomic-delay-before-read-charges-the-budget",
	    rc == -ETIMEDOUT && with_before == without_before - 1,
	    "before=%u after=%u rc=%d", with_before, without_before, rc);
}

static void
case_first_read(void)
{
	int val = 0;
	int rc;

	g_op_rounds = 0;
	rc = read_poll_timeout_atomic(first_true, val, val, 1000, 3000, false);
	case_line("atomic-first-read-wins", rc == 0 && g_op_rounds == 1,
	    "rc=%d rounds=%u", rc, g_op_rounds);
}

/* ------------------------------------------------------------------ */
/* the completion cases                                                */
/* ------------------------------------------------------------------ */

static void
case_completion(void)
{
	struct completion c;
	unsigned int observed = 0;

	lkpi_completion_init(&c);
	lkpi_complete_all(&c);
	/* complete_all is sticky: every waiter (and every later waiter) sees it
	 * without consuming it, and each wait returns exactly once */
	lkpi_wait_for_completion(&c);
	observed++;
	lkpi_wait_for_completion(&c);
	observed++;
	/* reinit clears the sticky marker, and a plain complete then releases
	 * one waiter */
	lkpi_completion_reinit(&c);
	lkpi_complete(&c);
	lkpi_wait_for_completion(&c);
	observed++;
	lkpi_completion_destroy(&c);
	case_line("completion-sticky-and-reinit", observed == 3,
	    "observed=%u", observed);
}

/* ------------------------------------------------------------------ */
/* the wait_event_timeout cases                                        */
/* ------------------------------------------------------------------ */

/*
 * The wrapped wait's result has to reach the caller the right way round: a
 * condition that came true - because it already held, or because a wake-up
 * delivered it - is 1, and only a real timeout is 0. The wake-up leg is the one
 * the vendored driver's firmware-chunk wait rides (it tests the result for
 * zero, third-party/iwlwifi/pcie/gen1_2/trans.c:646), and every leg is bounded:
 * an implementation that re-arms the whole budget after a missed wake-up would
 * turn a bounded wait into an unbounded one.
 */

struct we_case {
	struct wait_queue_head	wq;
	volatile int		cond;
};

static struct we_case we;

static void
we_waker(void *arg)
{
	struct we_case *c = arg;

	osDelay(50);
	c->cond = 1;
	lkpi_waitqueue_wake(&c->wq);
	/* a task entry must not return (the port's own assertion says so) */
	osThreadExit();
}

static void
case_wait_event(void)
{
	osThreadAttr_t attr = { 0 };
	unsigned long t0, dt;
	osThreadId_t t;
	int rc;

	lkpi_waitqueue_init(&we.wq);

	/* (1) the condition already holds: no wait happens, the result is "true" */
	we.cond = 1;
	t0 = jiffies;
	rc = wait_event_timeout(we.wq, we.cond, msecs_to_jiffies(200));
	dt = jiffies - t0;
	case_line("wait-event-satisfied", rc == 1 && dt < 100,
	    "rc=%d dt=%lums", rc, dt);

	/* (2) a wake-up delivers the condition: the waiter must report "true",
	 * and it must return at the wake-up, not at the deadline */
	we.cond = 0;
	attr.name = "we-waker";
	attr.stack_size = 4096u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(we_waker, &we, &attr);
	if (t == NULL) {
		case_line("wait-event-woken", 0, "the waker did not start");
		return;
	}
	t0 = jiffies;
	rc = wait_event_timeout(we.wq, we.cond, msecs_to_jiffies(2000));
	dt = jiffies - t0;
	case_line("wait-event-woken", rc == 1 && dt >= 30 && dt < 1000,
	    "rc=%d dt=%lums", rc, dt);

	/* (3) nothing delivers the condition: the wait must time out and say so,
	 * within the budget it was given */
	we.cond = 0;
	t0 = jiffies;
	rc = wait_event_timeout(we.wq, we.cond, msecs_to_jiffies(200));
	dt = jiffies - t0;
	case_line("wait-event-times-out", rc == 0 && dt >= 180 && dt < 800,
	    "rc=%d dt=%lums", rc, dt);
}

static void
app_task(void *arg)
{
	(void)arg;
	case_slow_op();
	case_budget_rounds();
	case_delay_before_read();
	case_first_read();
	case_completion();
	case_wait_event();
	printf("TIMESEM: summary cases=%u failed=%u\n", g_cases, g_fail);
	fflush(stdout);
	cmsis2_host_end_scheduler();
}

int
main(void)
{
	osThreadAttr_t attr = { 0 };
	osThreadId_t t;

	if (osKernelInitialize() != osOK) {
		printf("TIMESEM: kernel init failed\n");
		return 2;
	}
	attr.name = "time-semantics";
	attr.stack_size = 16384u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("TIMESEM: cannot create the case task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return g_fail == 0 ? 0 : 1;
}
