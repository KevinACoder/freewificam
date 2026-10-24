/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   cmsis2.c
 * @brief  Case cmsis2: the CMSIS-RTOS2 conformance groups, one per kernel
 *         service family the driver layer consumes.
 *
 * The same source runs on the board (both kernels) and on the host tier (both
 * kernels' own host ports), so a semantic difference between the two backends
 * shows up as a different verdict for the same bytes - which is the point of
 * the case.
 *
 * Groups and what each one is allowed to claim:
 *   threads   create/priority/terminate, strict-priority order, terminal
 *             semaphore (the CMSIS-OS2 join attribute is not available on
 *             either backend - recorded in the platform ADR)
 *   sem       counting semaphore count and drain, empty-acquire timeout
 *   mutex     priority inheritance, measured as the high waiter's wait time
 *             against a medium-priority spinner
 *   queue     depth, full-put timeout, FIFO order, empty-get timeout
 *   timer     one-shot fires once, periodic period and jitter, callback cost
 *   flags     thread flags: OR wait, AND wait, timeout
 *   timebase  tick frequency and tick-vs-clock drift
 *
 * Every wait is bounded; a hung service fails its group instead of the image.
 */

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

/* Tolerances.  The board tier is the real-time claim: the tick is the kernel's
 * own hardware timer, so a 100 ms period is expected within 5 % and a 200 ms
 * delay within 10 ms.  The host tier runs the kernel's own host port on a
 * timeshared OS, where the port's tick arrives through signals: the same
 * numbers would measure the host scheduler, not the adapter, so the host gets
 * bounds that only catch a broken timer (a late or doubled period). */
#if defined(RTOS_HOST_TIER)
#define TIMER_JITTER_MAX_US	25000u
#define TIME_DRIFT_MAX_MS	40
#else
#define TIMER_JITTER_MAX_US	5000u
#define TIME_DRIFT_MAX_MS	10
#endif

#define PRIO_HI		osPriorityHigh
#define PRIO_MID	osPriorityNormal
#define PRIO_LO		osPriorityLow

static unsigned checks;
static unsigned failures;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		CASE_ANCHOR("cmsis2: check FAIL %s\n", what);
	}
}

/* --- threads ---------------------------------------------------------------- */

static osSemaphoreId_t th_exit_sem;
static char th_trace[8];
static volatile unsigned th_trace_n;

static void th_trace_add(char c)
{
	os_critical_state_t state = osCriticalEnter();

	if (th_trace_n < sizeof(th_trace)) {
		th_trace[th_trace_n++] = c;
	}
	osCriticalExit(state);
}

static void th_body(char c)
{
	th_trace_add(c);
	(void)osSemaphoreRelease(th_exit_sem);
	for (;;) {
		(void)osDelay(1000u);
	}
}

static void th_hi(void *arg)  { (void)arg; th_body('H'); }
static void th_mid(void *arg) { (void)arg; th_body('M'); }
static void th_lo(void *arg)  { (void)arg; th_body('L'); }

static void group_threads(void)
{
	const osThreadAttr_t hi = { .name = "hi", .priority = PRIO_HI,
				    .stack_size = 2048U };
	const osThreadAttr_t mid = { .name = "mid", .priority = PRIO_MID,
				     .stack_size = 2048U };
	const osThreadAttr_t lo = { .name = "lo", .priority = PRIO_LO,
				    .stack_size = 2048U };
	osThreadId_t ids[3];
	unsigned i;
	unsigned exits = 0u;
	char order[4];

	th_exit_sem = osSemaphoreNew(3u, 0u, NULL);
	check(th_exit_sem != NULL, "thread exit semaphore");
	if (th_exit_sem == NULL) {
		return;
	}

	th_trace_n = 0u;
	ids[0] = osThreadNew(th_hi, NULL, &hi);
	ids[1] = osThreadNew(th_mid, NULL, &mid);
	ids[2] = osThreadNew(th_lo, NULL, &lo);
	check((ids[0] != NULL) && (ids[1] != NULL) && (ids[2] != NULL),
	      "thread create");

	for (i = 0u; i < 3u; i++) {
		if (osSemaphoreAcquire(th_exit_sem, 2000u) == osOK) {
			exits++;
		}
	}

	order[0] = (th_trace_n > 0u) ? th_trace[0] : '-';
	order[1] = (th_trace_n > 1u) ? th_trace[1] : '-';
	order[2] = (th_trace_n > 2u) ? th_trace[2] : '-';
	order[3] = '\0';

	CASE_ANCHOR("cmsis2: threads order=%s exits=%u/3\n", order, exits);

	check(exits == 3u, "thread exit count");
	check((order[0] == 'H') && (order[1] == 'M') && (order[2] == 'L'),
	      "strict priority order");

	for (i = 0u; i < 3u; i++) {
		if (ids[i] != NULL) {
			(void)osThreadTerminate(ids[i]);
		}
	}
	(void)osSemaphoreDelete(th_exit_sem);
	th_exit_sem = NULL;
}

/* --- semaphore + mutex ------------------------------------------------------ */

static osMutexId_t pip_mtx;
static volatile uint64_t pip_lo_acq_us;
static volatile uint64_t pip_lo_rel_us;
static volatile uint64_t pip_hi_try_us;
static volatile uint64_t pip_hi_got_us;
static volatile uint64_t pip_wait_us;
static volatile unsigned pip_mid_spins;
static volatile int pip_acquired;

static void pip_lo(void *arg)
{
	uint64_t start;
	(void)arg;

	(void)osMutexAcquire(pip_mtx, osWaitForever);
	pip_lo_acq_us = rtos_clock_us();
	start = rtos_clock_us();
	while ((rtos_clock_us() - start) < 60000u) {
		/* Hold with work, not with sleep: a sleeping owner cannot
		 * inherit anything, so the measurement would be meaningless. */
	}
	(void)osMutexRelease(pip_mtx);
	pip_lo_rel_us = rtos_clock_us();

	for (;;) {
		(void)osDelay(1000u);
	}
}

static void pip_hi(void *arg)
{
	uint64_t start;
	(void)arg;

	(void)osDelay(10u);
	start = rtos_clock_us();
	pip_hi_try_us = start;
	(void)osMutexAcquire(pip_mtx, osWaitForever);
	pip_hi_got_us = rtos_clock_us();
	pip_wait_us = pip_hi_got_us - start;
	pip_acquired = 1;
	(void)osMutexRelease(pip_mtx);

	for (;;) {
		(void)osDelay(1000u);
	}
}

static void pip_mid(void *arg)
{
	uint64_t start = rtos_clock_us();
	(void)arg;

	while ((rtos_clock_us() - start) < 150000u) {
		pip_mid_spins++;
	}

	for (;;) {
		(void)osDelay(1000u);
	}
}

static void group_sem_mutex(void)
{
	osSemaphoreId_t sem = osSemaphoreNew(3u, 0u, NULL);
	osThreadId_t lo = NULL;
	osThreadId_t mid = NULL;
	osThreadId_t hi = NULL;
	unsigned i;
	unsigned released = 0u;
	unsigned drained = 0u;
	osStatus_t empty;

	/* Counting semaphore: release three, read the count, drain it. */
	check(sem != NULL, "semaphore create");
	if (sem != NULL) {
		for (i = 0u; i < 3u; i++) {
			if (osSemaphoreRelease(sem) == osOK) {
				released++;
			}
		}
		check(osSemaphoreGetCount(sem) == 3u, "semaphore count");
		for (i = 0u; i < 3u; i++) {
			if (osSemaphoreAcquire(sem, 10u) == osOK) {
				drained++;
			}
		}
		empty = osSemaphoreAcquire(sem, 5u);
		CASE_ANCHOR("cmsis2: sem released=%u/3 drained=%u/3 empty=%s\n",
			    released, drained,
			    (empty == osErrorTimeout) ? "timeout" :
			    (empty == osOK) ? "acquired" : "error");
		check(released == 3u, "semaphore release count");
		check(drained == 3u, "semaphore drain count");
		check(empty == osErrorTimeout, "semaphore empty timeout");
		(void)osSemaphoreDelete(sem);
	}

	/* Mutex with priority inheritance. */
	pip_mtx = osMutexNew(NULL);
	check(pip_mtx != NULL, "mutex create");
	if (pip_mtx == NULL) {
		return;
	}

	pip_wait_us = 0u;
	pip_mid_spins = 0u;
	pip_acquired = 0;

	lo = osThreadNew(pip_lo, NULL,
			 &(const osThreadAttr_t){ .name = "pip-lo",
						  .priority = PRIO_LO,
						  .stack_size = 2048U });
	check(lo != NULL, "pip low thread");
	(void)osDelay(5u);

	mid = osThreadNew(pip_mid, NULL,
			  &(const osThreadAttr_t){ .name = "pip-mid",
						   .priority = PRIO_MID,
						   .stack_size = 2048U });
	check(mid != NULL, "pip mid thread");
	(void)osDelay(5u);

	hi = osThreadNew(pip_hi, NULL,
			 &(const osThreadAttr_t){ .name = "pip-hi",
						  .priority = PRIO_HI,
						  .stack_size = 2048U });
	check(hi != NULL, "pip high thread");

	for (i = 0u; (i < 500u) && (pip_acquired == 0); i++) {
		(void)osDelay(1u);
	}
	/* The owner writes its release stamp after the release call, which on
	 * this kernel hands the mutex straight to the waiter; the owner can run
	 * only after the medium spinner's window closes, so wait for the stamp
	 * instead of assuming a delay is long enough. */
	for (i = 0u; (i < 400u) && (pip_lo_rel_us == 0u); i++) {
		(void)osDelay(1u);
	}

	CASE_ANCHOR("cmsis2: mutex acquire=%d wait_us=%lu hold_us=%lu try_us=%lu spins=%lu\n",
		    pip_acquired, (unsigned long)pip_wait_us,
		    (unsigned long)(pip_lo_rel_us - pip_lo_acq_us),
		    (unsigned long)(pip_hi_try_us - pip_lo_acq_us),
		    (unsigned long)pip_mid_spins);

	check(pip_acquired != 0, "mutex acquire after hold");

#if defined(RTOS_HOST_TIER)
	/* The host ports only switch threads at kernel calls: the medium
	 * spinner's pure user-code loop cannot be preempted, so priority
	 * inheritance cannot be observed here (it is the board's tick
	 * interrupt that makes the difference).  The numbers are printed for
	 * the record; the verdict belongs to the board tier. */
	CASE_ANCHOR("cmsis2: mutex inherit not-measured (host port does not preempt user code)\n");
#else
	/* The high waiter must have queued behind the low owner (contention
	 * existed) and must have got the mutex soon after the owner released
	 * (inheritance beat the medium spinner to the CPU).  Without
	 * inheritance the owner does not run again until the spinner's 150 ms
	 * window ends. */
	check((pip_hi_try_us >= pip_lo_acq_us) &&
	      (pip_hi_try_us < pip_lo_rel_us), "mutex contention existed");
	check(pip_wait_us < 100000u, "mutex priority inheritance");
#endif

	if (hi != NULL) {
		(void)osThreadTerminate(hi);
	}
	if (mid != NULL) {
		(void)osThreadTerminate(mid);
	}
	if (lo != NULL) {
		(void)osThreadTerminate(lo);
	}
	(void)osMutexDelete(pip_mtx);
	pip_mtx = NULL;
}

/* --- message queue ---------------------------------------------------------- */

static void group_queue(void)
{
	osMessageQueueId_t q = osMessageQueueNew(4u, sizeof(uint32_t), NULL);
	uint32_t i;
	uint32_t value;
	uint32_t t0;
	uint32_t put_ticks = 0u;
	uint32_t get_ticks = 0u;
	osStatus_t full;
	osStatus_t empty;
	int fifo = 1;

	check(q != NULL, "queue create");
	if (q == NULL) {
		return;
	}

	for (i = 0u; i < 4u; i++) {
		check(osMessageQueuePut(q, &i, 0u, 0u) == osOK, "queue put");
	}
	check(osMessageQueueGetCount(q) == 4u, "queue count");

	value = 0xFFFFu;
	t0 = osKernelGetTickCount();
	full = osMessageQueuePut(q, &value, 0u, 1u);
	put_ticks = osKernelGetTickCount() - t0;

	for (i = 0u; i < 4u; i++) {
		value = 0xFFFFu;
		if (osMessageQueueGet(q, &value, NULL, 10u) != osOK) {
			fifo = 0;
		} else if (value != i) {
			fifo = 0;
		}
	}

	value = 0u;
	t0 = osKernelGetTickCount();
	empty = osMessageQueueGet(q, &value, NULL, 5u);
	get_ticks = osKernelGetTickCount() - t0;

	CASE_ANCHOR("cmsis2: queue full=%s put_ticks=%lu empty=%s get_ticks=%lu fifo=%d\n",
		    (full == osErrorTimeout) ? "timeout" : "other",
		    (unsigned long)put_ticks,
		    (empty == osErrorTimeout) ? "timeout" : "other",
		    (unsigned long)get_ticks, fifo);

	check(full == osErrorTimeout, "queue full put timeout");
	check(put_ticks <= 2u, "queue put timeout within one tick");
	check(empty == osErrorTimeout, "queue empty get timeout");
	check((get_ticks >= 5u) && (get_ticks <= 6u),
	      "queue get timeout within one tick");
	check(fifo == 1, "queue FIFO order");

	(void)osMessageQueueDelete(q);
}

/* --- timers ----------------------------------------------------------------- */

static volatile unsigned oneshot_count;
static volatile unsigned oneshot_extra;
static volatile unsigned period_count;
static volatile uint64_t period_at[16];
static volatile uint64_t oneshot_entry_us;
static volatile uint64_t oneshot_exit_us;

static void oneshot_cb(void *arg)
{
	(void)arg;
	oneshot_entry_us = rtos_clock_us();
	oneshot_count++;
	oneshot_exit_us = rtos_clock_us();
}

static void period_cb(void *arg)
{
	(void)arg;
	if (period_count < 16u) {
		period_at[period_count] = rtos_clock_us();
	}
	period_count++;
}

static void group_timer(void)
{
	osTimerId_t oneshot = osTimerNew(oneshot_cb, osTimerOnce, NULL, NULL);
	osTimerId_t periodic = osTimerNew(period_cb, osTimerPeriodic, NULL, NULL);
	unsigned i;
	unsigned gaps = 0u;
	uint64_t jitter_max_us = 0u;
	uint64_t cb_max_us = 0u;

	check((oneshot != NULL) && (periodic != NULL), "timer create");
	if ((oneshot == NULL) || (periodic == NULL)) {
		return;
	}

	oneshot_count = 0u;
	period_count = 0u;

	check(osTimerStart(oneshot, 100u) == osOK, "oneshot start");
	check(osTimerStart(periodic, 100u) == osOK, "periodic start");

	/* Ten periods plus slack: 1.3 s at 100 Hz tick. */
	(void)osDelay(1300u);

	(void)osTimerStop(periodic);
	(void)osDelay(100u);
	oneshot_extra = oneshot_count;

	for (i = 1u; (i < period_count) && (i < 12u); i++) {
		uint64_t delta = period_at[i] - period_at[i - 1u];
		uint64_t err = (delta > 100000u) ? (delta - 100000u)
						 : (100000u - delta);

		if (err > jitter_max_us) {
			jitter_max_us = err;
		}
		gaps++;
	}

	if (oneshot_exit_us > oneshot_entry_us) {
		cb_max_us = oneshot_exit_us - oneshot_entry_us;
	}

	CASE_ANCHOR("cmsis2: timer oneshot=%u extra=%u periods=%u jitter_us=%lu cb_us=%lu\n",
		    oneshot_count, oneshot_extra, gaps,
		    (unsigned long)jitter_max_us, (unsigned long)cb_max_us);

	check(oneshot_count == 1u, "oneshot fired exactly once");
	check(oneshot_extra == 1u, "oneshot did not repeat");
	check(gaps >= 8u, "periodic fired at least 8 times");
	check(jitter_max_us <= TIMER_JITTER_MAX_US, "periodic jitter bound");
	check(cb_max_us < 100u, "timer callback is short");

	(void)osTimerDelete(oneshot);
	(void)osTimerDelete(periodic);
}

/* --- event flags ----------------------------------------------------------- */

static void group_flags(void)
{
	osEventFlagsId_t group = osEventFlagsNew(NULL);
	uint32_t r_or;
	uint32_t r_and;
	uint32_t r_timeout;
	uint32_t r_cleared;
	uint32_t value;

	check(group != NULL, "event flags create");
	if (group == NULL) {
		return;
	}

	/* OR wait: one of two requested bits is set. */
	check(osEventFlagsSet(group, 0x1u) == 0x1u, "event flags set one");
	r_or = osEventFlagsWait(group, 0x3u, osFlagsWaitAny, 100u);

	/* AND wait: both bits must be present.  The OR wait consumed 0x1, so
	 * both are set again here - otherwise the AND wait would be asking for
	 * a bit that is already gone. */
	check(osEventFlagsSet(group, 0x3u) == 0x3u, "event flags set both");
	r_and = osEventFlagsWait(group, 0x3u, osFlagsWaitAll, 100u);

	/* Nothing set: the wait must time out, not return an error code. */
	r_timeout = osEventFlagsWait(group, 0x4u, osFlagsWaitAny, 20u);

	/* Clear semantics: a set bit that is cleared must not satisfy a wait. */
	(void)osEventFlagsSet(group, 0x8u);
	(void)osEventFlagsClear(group, 0x8u);
	r_cleared = osEventFlagsWait(group, 0x8u, osFlagsWaitAny, 10u);

	(void)osEventFlagsSet(group, 0x10u);
	value = osEventFlagsGet(group);

	CASE_ANCHOR("cmsis2: flags or=0x%lx and=0x%lx timeout=0x%lx cleared=0x%lx get=0x%lx\n",
		    (unsigned long)r_or, (unsigned long)r_and,
		    (unsigned long)r_timeout, (unsigned long)r_cleared,
		    (unsigned long)value);

	check(r_or == 0x1u, "event flags OR wait result");
	check(r_and == 0x3u, "event flags AND wait result");
	check(r_timeout == osFlagsErrorTimeout, "event flags wait timeout");
	check(r_cleared == osFlagsErrorTimeout, "event flags clear removes the bit");
	check((value & 0x10u) != 0u, "event flags get reads the set bits");

	(void)osEventFlagsDelete(group);
}

/* --- time base -------------------------------------------------------------- */

static void group_timebase(void)
{
	uint32_t freq = osKernelGetTickFreq();
	uint32_t t0;
	uint32_t t1;
	uint64_t us0;
	uint64_t us1;
	long drift_ms;

	if (freq != 1000u) {
		CASE_ANCHOR("cmsis2: timebase freq=%lu want=1000\n",
			    (unsigned long)freq);
		check(0, "tick frequency is 1000 Hz");
		return;
	}

	t0 = osKernelGetTickCount();
	us0 = rtos_clock_us();
	(void)osDelay(200u);
	t1 = osKernelGetTickCount();
	us1 = rtos_clock_us();

	drift_ms = (long)((us1 - us0) / 1000u) - 200;

	CASE_ANCHOR("cmsis2: timebase tick_ms=%lu wall_ms=%ld drift_ms=%ld\n",
		    (unsigned long)(t1 - t0), (long)((us1 - us0) / 1000u),
		    drift_ms);

	check((t1 - t0) == 200u, "200 tick delay");
	check((drift_ms > -TIME_DRIFT_MAX_MS) && (drift_ms < TIME_DRIFT_MAX_MS),
	      "tick against the clock");
}

/* --- the case --------------------------------------------------------------- */

int case_cmsis2_run(void)
{
	checks = 0u;
	failures = 0u;

	group_threads();
	group_sem_mutex();
	group_queue();
	group_timer();
	group_flags();
	group_timebase();

	CASE_ANCHOR("cmsis2: checks=%u failures=%u\n", checks, failures);
	CASE_ANCHOR("cmsis2: %s\n", (failures == 0u) ? "PASS" : "FAIL");

	return (failures == 0u) ? 0 : -1;
}