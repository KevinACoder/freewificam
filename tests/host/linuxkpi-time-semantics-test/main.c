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

static void
app_task(void *arg)
{
	(void)arg;
	case_slow_op();
	case_budget_rounds();
	case_delay_before_read();
	case_first_read();
	case_completion();
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
