/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   ctx.c
 * @brief  K2: the execution-context semantics - BH/tasklet, spin_lock_bh,
 *         RCU grace periods (docs/HANDOFF-S0-HOST.md §4).
 *
 * Each case is a model: the host has no interrupts and is SMP, so an "ISR" is
 * a high-priority task and a preemption is an explicit yield.  The cases pin
 * the port's own contracts (async delivery, coalescing, ordering, deferral,
 * cancellation, grace-period visibility), not the board's timing; the README
 * lists the difference.
 */
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"
#include "linux/interrupt.h"
#include "linux/semaphore.h"

#include "kpi_test.h"

static osSemaphoreId_t done;

static int sem_init(void)
{
	if (done == NULL)
		done = osSemaphoreNew(8u, 0u, NULL);
	return done != NULL ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* C1: a tasklet runs in the BH thread, not in the armer's             */
/* ------------------------------------------------------------------ */

static struct tasklet_struct tl1;
static volatile osThreadId_t cb_thread;
static volatile int cb_in_softirq;
static int seen;

static void tl1_cb(unsigned long data)
{
	(void)data;
	cb_thread = osThreadGetId();
	cb_in_softirq = in_softirq();
	seen++;
	(void)osSemaphoreRelease(done);
}

int kpi_case_ctx_tasklet_thread(void)
{
	osThreadId_t me = osThreadGetId();

	KPI_ASSERT(sem_init() == 0, "semaphore");
	seen = 0;
	cb_thread = NULL;
	tasklet_init(&tl1, tl1_cb, 0);
	tasklet_schedule(&tl1);
	KPI_ASSERT(osSemaphoreAcquire(done, 1000u) == osOK, "callback ran");
	KPI_ASSERT_EQ(seen, 1, "one callback");
	KPI_ASSERT(cb_thread != NULL && cb_thread != me,
		   "callback ran in the BH thread");
	KPI_ASSERT(cb_in_softirq != 0, "in_softirq() is true in the callback");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* C2: FIFO                                                            */
/* ------------------------------------------------------------------ */

static int fifo_log[2];
static int fifo_n;
static struct tasklet_struct tf[2];

static void fifo0(unsigned long d) { (void)d; fifo_log[fifo_n++] = 0; (void)osSemaphoreRelease(done); }
static void fifo1(unsigned long d) { (void)d; fifo_log[fifo_n++] = 1; (void)osSemaphoreRelease(done); }

int kpi_case_ctx_fifo(void)
{
	KPI_ASSERT(sem_init() == 0, "semaphore");
	fifo_n = 0;
	tasklet_init(&tf[0], fifo0, 0);
	tasklet_init(&tf[1], fifo1, 0);
	tasklet_schedule(&tf[0]);
	tasklet_schedule(&tf[1]);
	KPI_ASSERT(osSemaphoreAcquire(done, 1000u) == osOK, "first ran");
	KPI_ASSERT(osSemaphoreAcquire(done, 1000u) == osOK, "second ran");
	KPI_ASSERT(fifo_n == 2 && fifo_log[0] == 0 && fifo_log[1] == 1,
		   "tasklets are FIFO");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* C3: coalescing                                                      */
/* ------------------------------------------------------------------ */

static volatile int coal_runs;
static struct tasklet_struct tc;

static void coal_cb(unsigned long d)
{
	(void)d;
	coal_runs++;
	(void)osSemaphoreRelease(done);
}

int kpi_case_ctx_coalesce(void)
{
	KPI_ASSERT(sem_init() == 0, "semaphore");
	coal_runs = 0;
	tasklet_init(&tc, coal_cb, 0);
	local_bh_disable();
	tasklet_schedule(&tc);
	tasklet_schedule(&tc);
	(void)osDelay(20);
	KPI_ASSERT_EQ(coal_runs, 0, "BH did not run inside the section");
	local_bh_enable();
	KPI_ASSERT(osSemaphoreAcquire(done, 1000u) == osOK, "callback ran");
	(void)osDelay(20);
	KPI_ASSERT_EQ(coal_runs, 1, "two arms coalesced into one callback");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* C4: tasklet_kill waits for a running callback                       */
/* ------------------------------------------------------------------ */

static volatile int kill_started;
static volatile uint64_t kill_finished_ms;
static volatile uint64_t kill_returned_ms;
static struct tasklet_struct tk;
static osSemaphoreId_t kill_done;

static void kill_cb(unsigned long d)
{
	(void)d;
	kill_started = 1;
	kpi_delay_ms(50);
	kill_finished_ms = kpi_now_ms();
}

static void killer(void *arg)
{
	(void)arg;
	tasklet_kill(&tk);
	kill_returned_ms = kpi_now_ms();
	(void)osSemaphoreRelease(kill_done);
	osThreadExit();
}

int kpi_case_ctx_kill_running(void)
{
	osThreadId_t t;
	osThreadAttr_t attr;
	int i;

	KPI_ASSERT(sem_init() == 0, "semaphore");
	if (kill_done == NULL)
		kill_done = osSemaphoreNew(1u, 0u, NULL);
	KPI_ASSERT(kill_done != NULL, "kill semaphore");
	kill_started = 0;
	kill_finished_ms = 0;
	kill_returned_ms = 0;
	tasklet_init(&tk, kill_cb, 0);
	tasklet_schedule(&tk);
	for (i = 0; i < 500 && kill_started == 0; i++)
		(void)osDelay(1);
	KPI_ASSERT(kill_started != 0, "callback started");

	memset(&attr, 0, sizeof(attr));
	attr.name = "killer";
	attr.stack_size = 4096u;
	attr.priority = osPriorityNormal;
	t = osThreadNew((osThreadFunc_t)killer, NULL, &attr);
	KPI_ASSERT(t != NULL, "killer thread");
	KPI_ASSERT(osSemaphoreAcquire(kill_done, 2000u) == osOK,
		   "tasklet_kill returned");
	KPI_ASSERT(kill_returned_ms >= kill_finished_ms,
		   "kill returned after the callback finished");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* C5: disable / enable                                                */
/* ------------------------------------------------------------------ */

static volatile int dis_runs;
static struct tasklet_struct td;

static void dis_cb(unsigned long d)
{
	(void)d;
	dis_runs++;
	(void)osSemaphoreRelease(done);
}

int kpi_case_ctx_disable_enable(void)
{
	KPI_ASSERT(sem_init() == 0, "semaphore");
	dis_runs = 0;
	tasklet_init(&td, dis_cb, 0);
	tasklet_disable(&td);
	tasklet_schedule(&td);
	(void)osDelay(30);
	KPI_ASSERT_EQ(dis_runs, 0, "disabled tasklet did not run");
	tasklet_enable(&td);
	KPI_ASSERT(osSemaphoreAcquire(done, 1000u) == osOK,
		   "enable re-kicked the pending arm");
	KPI_ASSERT_EQ(dis_runs, 1, "one run after enable");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* C6: local_bh_disable defers the BH, not system work                 */
/* ------------------------------------------------------------------ */

static volatile int bhdefer_runs;
static volatile int syswork_runs;
static struct tasklet_struct tb;
static struct work_struct sysw;
static osSemaphoreId_t sys_done;

static void bhdefer_cb(unsigned long d)
{
	(void)d;
	bhdefer_runs++;
	(void)osSemaphoreRelease(done);
}

static void sysw_cb(struct work_struct *w)
{
	(void)w;
	syswork_runs++;
	(void)osSemaphoreRelease(sys_done);
}

int kpi_case_ctx_bh_defers(void)
{
	KPI_ASSERT(sem_init() == 0, "semaphore");
	if (sys_done == NULL)
		sys_done = osSemaphoreNew(1u, 0u, NULL);
	KPI_ASSERT(sys_done != NULL, "work semaphore");
	bhdefer_runs = 0;
	syswork_runs = 0;
	tasklet_init(&tb, bhdefer_cb, 0);
	lkpi_work_init(&sysw, sysw_cb);
	local_bh_disable();
	tasklet_schedule(&tb);
	schedule_work(&sysw);
	KPI_ASSERT(osSemaphoreAcquire(sys_done, 1000u) == osOK,
		   "system work still runs while BH is disabled");
	(void)osDelay(20);
	KPI_ASSERT_EQ(bhdefer_runs, 0, "tasklet deferred inside the section");
	local_bh_enable();
	KPI_ASSERT(osSemaphoreAcquire(done, 1000u) == osOK,
		   "tasklet ran after enable");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* C7: nesting                                                         */
/* ------------------------------------------------------------------ */

int kpi_case_ctx_nesting(void)
{
	KPI_ASSERT(sem_init() == 0, "semaphore");
	bhdefer_runs = 0;
	tasklet_init(&tb, bhdefer_cb, 0);
	local_bh_disable();
	local_bh_disable();
	local_bh_disable();
	tasklet_schedule(&tb);
	(void)osDelay(20);
	local_bh_enable();
	(void)osDelay(20);
	KPI_ASSERT_EQ(bhdefer_runs, 0, "two levels are still closed");
	local_bh_enable();
	(void)osDelay(20);
	KPI_ASSERT_EQ(bhdefer_runs, 0, "one level is still closed");
	local_bh_enable();
	KPI_ASSERT(osSemaphoreAcquire(done, 1000u) == osOK,
		   "the last enable opens the BH");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* C8: in_softirq                                                      */
/* ------------------------------------------------------------------ */

int kpi_case_ctx_in_softirq(void)
{
	KPI_ASSERT(in_softirq() == 0, "false in a task");
	/* the true arm is C1's callback */
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* C9: spin_lock_bh exclusion (host proxy)                             */
/* ------------------------------------------------------------------ */

static spinlock_t sl;
static volatile int isr_acquired;
static osSemaphoreId_t isr_done;

static void isr_task(void *arg)
{
	(void)arg;
	spin_lock_bh(&sl);
	isr_acquired = 1;
	spin_unlock_bh(&sl);
	(void)osSemaphoreRelease(isr_done);
	osThreadExit();
}

int kpi_case_ctx_spin_bh(void)
{
	osThreadId_t t;
	osThreadAttr_t attr;

	if (isr_done == NULL)
		isr_done = osSemaphoreNew(1u, 0u, NULL);
	KPI_ASSERT(isr_done != NULL, "semaphore");
	spin_lock_init(&sl);
	isr_acquired = 0;

	/*
	 * Host limitation, stated rather than papered over: the host's
	 * osCriticalEnter() is a process-wide pthread mutex, not an interrupt
	 * mask, and the POSIX scheduler delivers task switches with signals - so a
	 * task that *sleeps inside the section* deadlocks the host port in a way
	 * the target's DAIF section cannot.  The case therefore pins the nesting
	 * contract (take twice, release twice) and the cross-context hand-off
	 * after the section, not "another context is excluded while held"; that
	 * half is target-only (C2) and is recorded as such in the ADR.
	 */
	spin_lock_bh(&sl);
	spin_lock_bh(&sl);
	spin_unlock_bh(&sl);
	spin_unlock_bh(&sl);

	memset(&attr, 0, sizeof(attr));
	attr.name = "isr-like";
	attr.stack_size = 4096u;
	attr.priority = osPriorityNormal;
	t = osThreadNew((osThreadFunc_t)isr_task, NULL, &attr);
	KPI_ASSERT(t != NULL, "isr thread");
	KPI_ASSERT(osSemaphoreAcquire(isr_done, 1000u) == osOK,
		   "another context enters after the release (no deadlock)");
	KPI_ASSERT_EQ(isr_acquired, 1, "it acquired the lock");
	return KPI_PASS;
}

/* ------------------------------------------------------------------ */
/* C10: RCU - the count model's grace period, and deferred release     */
/* ------------------------------------------------------------------ */

static volatile int *hazard_ptr;
static volatile int rcu_freed;
static volatile int rcu_reader_done;
static osSemaphoreId_t rcu_got;
static osSemaphoreId_t rcu_release;
static osSemaphoreId_t rcu_writer_done;
static osSemaphoreId_t rcu_reader_done_sem;
static osSemaphoreId_t rcu_cb_done;
static struct rcu_head rcu_head;

static void rcu_free_cb(struct rcu_head *h)
{
	(void)h;
	rcu_freed = 1;
	(void)osSemaphoreRelease(rcu_cb_done);
}

static void rcu_reader(void *arg)
{
	int *p;

	(void)arg;
	rcu_read_lock();
	p = (int *)hazard_ptr;
	(void)osSemaphoreRelease(rcu_got);
	(void)osSemaphoreAcquire(rcu_release, osWaitForever);
	if (p != NULL)
		*p = 0x5a5a;
	rcu_read_unlock();
	rcu_reader_done = 1;
	(void)osSemaphoreRelease(rcu_reader_done_sem);
	osThreadExit();
}

static void rcu_writer(void *arg)
{
	(void)arg;
	(void)osSemaphoreAcquire(rcu_got, osWaitForever);
	/* Defer the callback, then wait for the grace period.  The reader is
	 * blocked inside its section, so this must block here - if it returned,
	 * the callback would run while the reader still holds the object. */
	call_rcu(&rcu_head, rcu_free_cb);
	lkpi_synchronize_rcu();
	(void)osSemaphoreRelease(rcu_writer_done);
	osThreadExit();
}

int kpi_case_ctx_rcu(void)
{
	osThreadId_t r, w;
	osThreadAttr_t attr;
	int *obj;

	KPI_ASSERT(sem_init() == 0, "semaphore");
	if (rcu_got == NULL)
		rcu_got = osSemaphoreNew(1u, 0u, NULL);
	if (rcu_release == NULL)
		rcu_release = osSemaphoreNew(1u, 0u, NULL);
	if (rcu_writer_done == NULL)
		rcu_writer_done = osSemaphoreNew(1u, 0u, NULL);
	if (rcu_reader_done_sem == NULL)
		rcu_reader_done_sem = osSemaphoreNew(1u, 0u, NULL);
	if (rcu_cb_done == NULL)
		rcu_cb_done = osSemaphoreNew(1u, 0u, NULL);
	KPI_ASSERT(rcu_got && rcu_release && rcu_writer_done &&
		   rcu_reader_done_sem && rcu_cb_done, "rcu semaphores");

	obj = kmalloc(sizeof(*obj), GFP_KERNEL);
	KPI_ASSERT(obj != NULL, "object");
	*obj = 0x1234;
	hazard_ptr = obj;
	rcu_freed = 0;
	rcu_reader_done = 0;

	memset(&attr, 0, sizeof(attr));
	attr.stack_size = 8192u;
	attr.priority = osPriorityNormal;
	attr.name = "rcu-reader";
	r = osThreadNew((osThreadFunc_t)rcu_reader, NULL, &attr);
	KPI_ASSERT(r != NULL, "reader");
	attr.name = "rcu-writer";
	w = osThreadNew((osThreadFunc_t)rcu_writer, NULL, &attr);
	KPI_ASSERT(w != NULL, "writer");

	/* reader is inside the section, the writer is waiting for the grace
	 * period: the callback must not have run */
	(void)osDelay(50);
	KPI_ASSERT_EQ(rcu_freed, 0, "no callback while a reader is active");
	(void)osSemaphoreRelease(rcu_release);
	KPI_ASSERT(osSemaphoreAcquire(rcu_reader_done_sem, 2000u) == osOK,
		   "reader left the section");
	KPI_ASSERT(osSemaphoreAcquire(rcu_writer_done, 2000u) == osOK,
		   "synchronize_rcu() returned after the reader left");
	KPI_ASSERT(osSemaphoreAcquire(rcu_cb_done, 2000u) == osOK,
		   "deferred callback ran");
	KPI_ASSERT_EQ(rcu_freed, 1, "callback observed its own object");
	KPI_ASSERT(rcu_reader_done != 0, "reader completed");
	kfree(obj);	/* the callback only flags; the test owns the object */
	return KPI_PASS;
}
