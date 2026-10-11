/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_core.c
 * @brief   The linuxkpi service bodies: allocation, logging, time and delays, locks,
 *          completion, workqueues, timers and wait queues, on the CMSIS-RTOS2 facade.
 *
 * @design adapter over one OS surface. The contract header declares the Linux services
 *         as `lkpi_*` functions and the Linux names as macros over them; this file is
 *         the single implementation behind that face, and it reaches the OS only through
 *         the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h), which is what lets one body serve
 *         the board's ThreadX and the host's FreeRTOS build. Two mechanisms deserve
 *         naming: the objects are fixed-size shells cast onto the contract's opaque
 *         storage slots (linuxkpi_impl.h pins the sizes), and the completion carries a
 *         sticky marker for complete_all() so every later waiter passes without the
 *         marker ever being consumed. Reference for the family shapes: the Linux
 *         semantics quoted in the contract header's comments and the FreeBSD linuxkpi
 *         file family (sys/compat/linuxkpi/common/src/linux_work.c, linux_lock.c,
 *         linux_compat.c) this project's port tradition follows.
 * @date    08.10.2026
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"

#include <linux/sched.h>

/* ------------------------------------------------------------------ */
/* logging                                                             */
/* ------------------------------------------------------------------ */

static void (*lkpi_sink)(ulog_level_t level, const char *line);

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line))
{
	lkpi_sink = sink;
}

/*
 * Assemble one line and hand it to the sink. The buffer is bounded on purpose: a log
 * line is a diagnostic, and a driver that formats more than this loses the tail of a
 * line rather than the line. The level travels with the line so the sink can filter
 * (the sink owns the threshold; this component only states what the call site meant).
 */
static int lkpi_vlog(ulog_level_t level, const char *fmt, va_list ap)
{
	char line[256];
	int n;

	if (lkpi_sink == NULL)
		return 0;

	n = vsnprintf(line, sizeof(line), fmt, ap);
	lkpi_sink(level, line);
	return n;
}

int lkpi_printf(ulog_level_t level, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = lkpi_vlog(level, fmt, ap);
	va_end(ap);
	return n;
}

void lkpi_dev_printk(struct device *dev, ulog_level_t level, const char *fmt, ...)
{
	char line[256];
	va_list ap;
	int n;

	if (lkpi_sink == NULL)
		return;

	n = snprintf(line, sizeof(line), "%s: ", dev_name(dev));
	if (n < 0)
		return;

	va_start(ap, fmt);
	(void)vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
	va_end(ap);

	lkpi_sink(level, line);
}

/*
 * A fatal condition: say what happened and stop the image. The stop is abort(): on the
 * board that is the freestanding libc's park (a print and a loop), on a host build the
 * C library's abort, and either way the caller's contract - "this cannot continue" -
 * holds without the component knowing which world it runs in.
 */
void lkpi_fatal(const char *what)
{
	lkpi_printf(ULOG_INFO_LEVEL, ULOG_CRITICAL_LEVEL, "lkpi: fatal: %s\n", what != NULL ? what : "(unspecified)");
	abort();
}

/* ------------------------------------------------------------------ */
/* allocation                                                          */
/* ------------------------------------------------------------------ */

/*
 * Every block carries a header with its size and a magic. The size is what the heap may
 * want back on free and what a diagnostic wants to report; the magic turns a free of a
 * foreign or already-released pointer into a named refusal instead of heap corruption.
 */
struct lkpi_mem_hdr {
	uint32_t	magic;
	uint32_t	size;
};

#define	LKPI_MEM_MAGIC	0x49504b4cu	/* "LKPI" */

void *lkpi_kmem_alloc(size_t size, int flags, bool zero)
{
	struct lkpi_mem_hdr *hdr;
	void *p;

	/* The flags select wait-or-fail upstream; this heap never waits, so they are
	 * accepted and ignored rather than silently changing the size. */
	(void)flags;

	if (size == 0)
		return NULL;

	hdr = osMemoryAlloc(sizeof(*hdr) + size);
	if (hdr == NULL)
		return NULL;

	hdr->magic = LKPI_MEM_MAGIC;
	hdr->size = (uint32_t)size;

	p = (void *)(hdr + 1);
	if (zero)
		memset(p, 0, size);
	return p;
}

void lkpi_kmem_free(const void *ptr)
{
	struct lkpi_mem_hdr *hdr;

	if (ptr == NULL)
		return;

	hdr = ((struct lkpi_mem_hdr *)ptr) - 1;
	if (hdr->magic != LKPI_MEM_MAGIC) {
		lkpi_printf(ULOG_INFO_LEVEL, "lkpi: free of a pointer this allocator never handed out\n");
		return;
	}
	hdr->magic = 0;
	osMemoryFree(hdr);
}

/* ------------------------------------------------------------------ */
/* time and delays                                                     */
/* ------------------------------------------------------------------ */

/* The tick is the time base (1 kHz on the board: HZ in the contract header). */
unsigned long lkpi_jiffies(void)
{
	return (unsigned long)osKernelGetTickCount();
}

ktime_t lkpi_ktime_get(void)
{
	return (ktime_t)osKernelGetTickCount() * 1000000LL;
}

/*
 * Delays. The kernel's only wait primitive is the tick, so every delay here rounds up
 * to whole ticks: a sub-millisecond delay therefore costs one millisecond on a 1 kHz
 * tick. That is the safe direction (a delay never returns early) and it is recorded:
 * a poll loop written for microseconds runs slower, not wrong.
 */
void lkpi_udelay(unsigned int us)
{
	if (us == 0)
		return;
	(void)osDelay(1);
}

void lkpi_mdelay(unsigned int ms)
{
	if (ms == 0)
		return;
	(void)osDelay(ms);
}

void lkpi_usleep_range(unsigned int min_us, unsigned int max_us)
{
	unsigned int ms;

	(void)max_us;
	if (min_us == 0)
		return;
	ms = (min_us + 999u) / 1000u;
	(void)osDelay(ms == 0 ? 1u : ms);
}

void lkpi_msleep(unsigned int ms)
{
	if (ms == 0)
		return;
	(void)osDelay(ms);
}

/* ------------------------------------------------------------------ */
/* atomics, random and string helpers                                  */
/* ------------------------------------------------------------------ */

void lkpi_atomic_inc(atomic_t *v)
{
	(void)__atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

void lkpi_atomic_dec(atomic_t *v)
{
	(void)__atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

void lkpi_atomic_add(int i, atomic_t *v)
{
	(void)__atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

void lkpi_atomic_sub(int i, atomic_t *v)
{
	(void)__atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

int lkpi_atomic_inc_return(atomic_t *v)
{
	return __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

int lkpi_atomic_dec_return(atomic_t *v)
{
	return __atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

int lkpi_atomic_dec_and_test(atomic_t *v)
{
	return __atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST) == 0;
}

/*
 * A xorshift stream seeded from the tick. Good enough for the identifiers and jitter
 * this component asks for; not a cryptographic source, and nothing here claims to be.
 */
unsigned int lkpi_random_u32(void)
{
	static uint32_t state;

	if (state == 0)
		state = (uint32_t)osKernelGetTickCount() ^ 0x9e3779b9u;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

unsigned int get_random_int(void)
{
	return lkpi_random_u32();
}

int lkpi_strcasecmp(const char *a, const char *b)
{
	int ca, cb;

	for (;; a++, b++) {
		ca = (unsigned char)*a;
		cb = (unsigned char)*b;
		if (ca >= 'A' && ca <= 'Z')
			ca += 'a' - 'A';
		if (cb >= 'A' && cb <= 'Z')
			cb += 'a' - 'A';
		if (ca != cb)
			return ca - cb;
		if (ca == 0)
			return 0;
	}
}

char *lkpi_strnstr(const char *haystack, const char *needle, size_t len)
{
	size_t nlen = strlen(needle);

	if (nlen == 0)
		return (char *)haystack;

	for (; len >= nlen && *haystack != '\0'; haystack++, len--) {
		if (*haystack == *needle && memcmp(haystack, needle, nlen) == 0)
			return (char *)haystack;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* mutexes                                                             */
/* ------------------------------------------------------------------ */

static struct lkpi_mutex_shell *lkpi_mutex_of(struct mutex *m)
{
	return (struct lkpi_mutex_shell *)(void *)m->_storage;
}

void lkpi_mutex_init(struct mutex *m)
{
	struct lkpi_mutex_shell *s = lkpi_mutex_of(m);

	memset(m->_storage, 0, sizeof(m->_storage));
	/* A non-recursive mutex, as the interface's mutex is: taking it twice from
	 * one context is a bug in the caller, and it stays visible as one. */
	s->id = osMutexNew(NULL);
	s->inited = (s->id != NULL) ? 1u : 0u;
}

void lkpi_mutex_destroy(struct mutex *m)
{
	struct lkpi_mutex_shell *s = lkpi_mutex_of(m);

	if (s->inited && s->id != NULL)
		(void)osMutexDelete(s->id);
	s->id = NULL;
	s->inited = 0;
	s->locked = 0;
}

void lkpi_mutex_lock(struct mutex *m)
{
	struct lkpi_mutex_shell *s = lkpi_mutex_of(m);

	if (s->id == NULL) {
		/* A mutex used before init_completion-style initialisation: create it
		 * here rather than corrupt the slot, and say so once. */
		s->id = osMutexNew(NULL);
		s->inited = (s->id != NULL) ? 1u : 0u;
	}
	(void)osMutexAcquire(s->id, osWaitForever);
	s->locked = 1;
}

void lkpi_mutex_unlock(struct mutex *m)
{
	struct lkpi_mutex_shell *s = lkpi_mutex_of(m);

	s->locked = 0;
	(void)osMutexRelease(s->id);
}

int lkpi_mutex_trylock(struct mutex *m)
{
	struct lkpi_mutex_shell *s = lkpi_mutex_of(m);

	if (s->id == NULL)
		return 0;
	if (osMutexAcquire(s->id, 0) != osOK)
		return 0;
	s->locked = 1;
	return 1;
}

int lkpi_mutex_is_locked(struct mutex *m)
{
	return lkpi_mutex_of(m)->locked != 0;
}

/* ------------------------------------------------------------------ */
/* spinlocks: interrupt-masking sections                               */
/* ------------------------------------------------------------------ */

/*
 * The contract's spinlocks are the port's critical section, not a mutex: the section
 * excludes every other context on this single-core target, which is what a completion
 * context needs and what a blocking lock could never give it. The no-flags pair parks
 * the saved state in the lock; the irqsave pair carries it in the caller's flags.
 */
void lkpi_spin_lock(spinlock_t *l)
{
	l->_daif = (unsigned long long)osCriticalEnter();
}

void lkpi_spin_unlock(spinlock_t *l)
{
	os_critical_state_t state = (os_critical_state_t)l->_daif;

	l->_daif = 0;
	osCriticalExit(state);
}

unsigned long long lkpi_spin_lock_irqsave(spinlock_t *l)
{
	(void)l;
	return (unsigned long long)osCriticalEnter();
}

void lkpi_spin_unlock_irqrestore(spinlock_t *l, unsigned long long flags)
{
	(void)l;
	osCriticalExit((os_critical_state_t)flags);
}

int lkpi_spin_trylock(spinlock_t *l)
{
	lkpi_spin_lock(l);
	return 1;
}

/* ------------------------------------------------------------------ */
/* completion                                                          */
/* ------------------------------------------------------------------ */

static struct lkpi_mutex_shell *lkpi_c_mtx(struct completion *c)
{
	return (struct lkpi_mutex_shell *)(void *)c->_mtx;
}

static struct lkpi_cv_shell *lkpi_c_cv(struct completion *c)
{
	return (struct lkpi_cv_shell *)(void *)c->_cv;
}

void lkpi_completion_init(struct completion *c)
{
	struct lkpi_cv_shell *cv = lkpi_c_cv(c);

	memset(c->_mtx, 0, sizeof(c->_mtx));
	memset(c->_cv, 0, sizeof(c->_cv));
	c->done = 0;

	/* The mutex goes through the shared shell implementation; the condition is a
	 * counting semaphore with one permit, which is what makes a completion both
	 * consumable once and idempotent when it was already signalled. */
	lkpi_mutex_init((struct mutex *)c->_mtx);
	cv->sem = osSemaphoreNew(1u, 0u, NULL);
	cv->inited = (cv->sem != NULL) ? 1u : 0u;
}

void lkpi_completion_destroy(struct completion *c)
{
	struct lkpi_cv_shell *cv = lkpi_c_cv(c);

	if (cv->inited && cv->sem != NULL)
		(void)osSemaphoreDelete(cv->sem);
	cv->sem = NULL;
	cv->inited = 0;
	lkpi_mutex_destroy((struct mutex *)c->_mtx);
}

void lkpi_completion_reinit(struct completion *c)
{
	struct lkpi_cv_shell *cv = lkpi_c_cv(c);
	struct lkpi_mutex_shell *m = lkpi_c_mtx(c);

	/* Drain a permit left over from the previous round, then start over. */
	while (osSemaphoreAcquire(cv->sem, 0) == osOK)
		;
	(void)osMutexAcquire(m->id, osWaitForever);
	c->done = 0;
	(void)osMutexRelease(m->id);
}

void lkpi_complete(struct completion *c)
{
	struct lkpi_mutex_shell *m = lkpi_c_mtx(c);

	(void)osMutexAcquire(m->id, osWaitForever);
	if (c->done == 0) {
		c->done = 1;
		(void)osSemaphoreRelease(lkpi_c_cv(c)->sem);
	}
	(void)osMutexRelease(m->id);
}

/*
 * complete_all(): the marker is sticky, so every waiter - the ones already asleep and
 * every one that comes later - passes, and none of them consumes it. Only reinit clears
 * it, which is exactly the interface's semantics.
 */
void lkpi_complete_all(struct completion *c)
{
	struct lkpi_mutex_shell *m = lkpi_c_mtx(c);

	(void)osMutexAcquire(m->id, osWaitForever);
	c->done = LKPI_COMPLETION_STICKY;
	(void)osSemaphoreRelease(lkpi_c_cv(c)->sem);
	(void)osMutexRelease(m->id);
}

void complete(struct completion *c)
{
	lkpi_complete(c);
}

void complete_all(struct completion *c)
{
	lkpi_complete_all(c);
}

static int lkpi_completion_consume(struct completion *c)
{
	struct lkpi_mutex_shell *m = lkpi_c_mtx(c);
	int got = 0;

	(void)osMutexAcquire(m->id, osWaitForever);
	if (c->done != 0) {
		if (c->done != LKPI_COMPLETION_STICKY)
			c->done = 0;
		got = 1;
	}
	(void)osMutexRelease(m->id);
	return got;
}

void lkpi_wait_for_completion(struct completion *c)
{
	struct lkpi_cv_shell *cv = lkpi_c_cv(c);

	for (;;) {
		if (lkpi_completion_consume(c))
			return;
		(void)osSemaphoreAcquire(cv->sem, osWaitForever);
	}
}

unsigned long lkpi_wait_for_completion_timeout(struct completion *c,
					       unsigned long timeout)
{
	struct lkpi_cv_shell *cv = lkpi_c_cv(c);
	unsigned long deadline = lkpi_jiffies() + timeout;

	for (;;) {
		unsigned long now;

		if (lkpi_completion_consume(c))
			return (deadline > lkpi_jiffies()) ?
			    (deadline - lkpi_jiffies()) : 1ul;

		now = lkpi_jiffies();
		if (time_after_eq(now, deadline))
			return 0;
		(void)osSemaphoreAcquire(cv->sem, (uint32_t)(deadline - now));
	}
}

int lkpi_try_wait_for_completion(struct completion *c)
{
	return lkpi_completion_consume(c);
}

int lkpi_completion_done(const struct completion *c)
{
	return c->done != 0;
}

/* ------------------------------------------------------------------ */
/* workqueues                                                          */
/* ------------------------------------------------------------------ */

struct lkpi_wq_impl {
	osThreadId_t		thread;
	osMessageQueueId_t	mq;
	char			name[24];
};

struct workqueue_struct *lkpi_system_wq;

#define	LKPI_WQ_DEPTH		32
#define	LKPI_WQ_STACK_SIZE	8192

static struct lkpi_wq_impl *lkpi_wq_of(struct workqueue_struct *wq)
{
	return (struct lkpi_wq_impl *)wq->_wq;
}

void lkpi_work_init(struct work_struct *w, void (*func)(struct work_struct *))
{
	memset(w, 0, sizeof(*w));
	w->func = func;
}

void lkpi_delayed_work_init(struct delayed_work *dw,
			    void (*func)(struct work_struct *))
{
	memset(dw, 0, sizeof(*dw));
	lkpi_work_init(&dw->work, func);
}

/*
 * The worker: one thread per queue, a FIFO of work pointers. The queued flag is cleared
 * with the queue's lock held and before the callback runs, so a queue_work() racing with
 * the callback is either rejected (still queued) or accepted (already cleared) - never
 * lost.
 */
static void lkpi_worker_entry(void *arg)
{
	struct lkpi_wq_impl *impl = arg;
	struct work_struct *w;

	for (;;) {
		if (osMessageQueueGet(impl->mq, &w, NULL, osWaitForever) != osOK)
			continue;
		if (w == NULL)
			return;		/* shutdown sentinel */

		os_critical_state_t state = osCriticalEnter();

		w->wk_queued = 0;
		w->wk_running = 1;
		osCriticalExit(state);

		if (w->func != NULL)
			w->func(w);
		w->wk_running = 0;
	}
}

struct workqueue_struct *lkpi_alloc_workqueue(const char *fmt, int flags,
					      int max_active)
{
	struct workqueue_struct *wq;
	struct lkpi_wq_impl *impl;
	osThreadAttr_t attr;

	(void)flags;
	(void)max_active;

	wq = kmalloc(sizeof(*wq), GFP_KERNEL);
	impl = kzalloc(sizeof(*impl), GFP_KERNEL);
	if (wq == NULL || impl == NULL) {
		kfree(wq);
		kfree(impl);
		return NULL;
	}

	impl->mq = osMessageQueueNew(LKPI_WQ_DEPTH, sizeof(struct work_struct *),
				     NULL);
	if (impl->mq == NULL) {
		kfree(impl);
		kfree(wq);
		return NULL;
	}

	if (fmt != NULL)
		(void)snprintf(impl->name, sizeof(impl->name), "%s", fmt);
	else
		(void)snprintf(impl->name, sizeof(impl->name), "lkpi-wq");

	memset(&attr, 0, sizeof(attr));
	attr.name = impl->name;
	attr.stack_size = LKPI_WQ_STACK_SIZE;
	/* Below the submitter: a driver callback must not be able to starve the
	 * context that queued it (the same rule the USB engine's drain thread
	 * follows). */
	attr.priority = osPriorityNormal;
	impl->thread = osThreadNew(lkpi_worker_entry, impl, &attr);
	if (impl->thread == NULL) {
		(void)osMessageQueueDelete(impl->mq);
		kfree(impl);
		kfree(wq);
		return NULL;
	}

	wq->_wq = impl;
	return wq;
}

void lkpi_destroy_workqueue(struct workqueue_struct *wq)
{
	struct lkpi_wq_impl *impl;

	if (wq == NULL)
		return;
	impl = lkpi_wq_of(wq);
	if (impl != NULL) {
		if (impl->thread != NULL)
			(void)osThreadTerminate(impl->thread);
		if (impl->mq != NULL)
			(void)osMessageQueueDelete(impl->mq);
		kfree(impl);
	}
	kfree(wq);
}

bool lkpi_queue_work(struct workqueue_struct *wq, struct work_struct *w)
{
	struct lkpi_wq_impl *impl;
	os_critical_state_t state;
	int queued = 0;

	if (wq == NULL || w == NULL)
		return false;
	impl = lkpi_wq_of(wq);
	if (impl == NULL)
		return false;

	state = osCriticalEnter();
	if (w->wk_queued == 0) {
		w->wk_queued = 1;
		w->_wq = impl;
		queued = 1;
	}
	osCriticalExit(state);

	if (!queued)
		return false;

	/* The queueing itself is outside the section: a full queue with no wait is
	 * a refusal, and the flag goes back so the caller may retry. */
	if (osMessageQueuePut(impl->mq, &w, 0u, 0u) != osOK) {
		state = osCriticalEnter();
		w->wk_queued = 0;
		osCriticalExit(state);
		return false;
	}
	return true;
}

/*
 * The delayed work's callout: the timer only flags and queues, never runs the body -
 * the timer context may not sleep and the body may (the execution model the net80211
 * seam states for this project).
 */
static void lkpi_delayed_timer_cb(void *arg)
{
	struct delayed_work *dw = arg;
	struct lkpi_callout_shell *shell =
	    (struct lkpi_callout_shell *)(void *)dw->_callout;
	struct workqueue_struct *wq = dw->work._wq;

	shell->pending = 0;
	dw->dw_scheduled = false;

	if (wq != NULL && !dw->dw_cancel) {
		dw->work._wq = NULL;
		(void)lkpi_queue_work(wq, &dw->work);
	}
}

bool lkpi_queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dw,
			     unsigned long delay)
{
	struct lkpi_callout_shell *shell =
	    (struct lkpi_callout_shell *)(void *)dw->_callout;
	uint32_t ms;

	if (wq == NULL || dw == NULL)
		return false;

	if (!shell->inited) {
		shell->owner = dw;
		shell->timer = osTimerNew(lkpi_delayed_timer_cb, osTimerOnce, dw,
					  NULL);
		shell->inited = (shell->timer != NULL) ? 1u : 0u;
		if (!shell->inited)
			return false;
	}

	/* Re-arming an already scheduled delayed work overwrites the pending expiry,
	 * which is the interface's mod_delayed_work semantics. */
	dw->work._wq = wq;
	dw->dw_cancel = false;
	ms = (delay == 0) ? 1u : (uint32_t)jiffies_to_msecs(delay);
	shell->pending = 1;
	dw->dw_scheduled = true;
	return osTimerStart(shell->timer, ms) == osOK;
}

bool lkpi_cancel_delayed_work(struct delayed_work *dw)
{
	struct lkpi_callout_shell *shell =
	    (struct lkpi_callout_shell *)(void *)dw->_callout;
	bool was;

	if (!shell->inited)
		return false;

	was = shell->pending != 0;
	dw->dw_cancel = true;
	(void)osTimerStop(shell->timer);
	shell->pending = 0;
	dw->dw_scheduled = false;
	return was;
}

void lkpi_work_flush(struct work_struct *w)
{
	/* The worker clears wk_queued before running the body and wk_running after
	 * it, so both clear means "not pending and not executing". Polling the tick
	 * is enough here and keeps the queue free of a second wake-up path. */
	while (w->wk_queued != 0 || w->wk_running != 0)
		(void)osDelay(1);
}

void lkpi_flush_delayed_work(struct delayed_work *dw)
{
	(void)lkpi_cancel_delayed_work(dw);
	lkpi_work_flush(&dw->work);
}

bool lkpi_cancel_delayed_work_sync(struct delayed_work *dw)
{
	bool was = lkpi_cancel_delayed_work(dw);

	lkpi_work_flush(&dw->work);
	return was;
}

void *lkpi_worker_thread_id(void)
{
	if (lkpi_system_wq == NULL)
		return NULL;
	return (void *)lkpi_wq_of(lkpi_system_wq)->thread;
}

/* ------------------------------------------------------------------ */
/* timers                                                              */
/* ------------------------------------------------------------------ */

static void lkpi_timer_cb(void *arg)
{
	struct timer_list *t = arg;

	t->tl_pending = false;
	if (t->tl_func != NULL)
		t->tl_func(t);
}

void lkpi_timer_setup(struct timer_list *t, void (*func)(struct timer_list *))
{
	struct lkpi_callout_shell *shell =
	    (struct lkpi_callout_shell *)(void *)t->_callout;

	/*
	 * timer_setup is called once per timer object in Linux, but this shim's
	 * callers are not all that careful: wiphy_delayed_work_queue calls it on
	 * every queue (linuxkpi_wiphy_work.c:254).  Creating an OS timer each time
	 * would leak one CMSIS2 slot per call until the table is exhausted and
	 * every delayed work stops - which is what the rtw89 board round hit.  So
	 * the setup is idempotent for the object it is handed: an already
	 * initialised timer keeps its OS object and only takes the new callback
	 * (its pending flag is the scheduler's, not the setup's).
	 */
	if (shell->inited) {
		t->tl_func = func;
		return;
	}
	memset(t->_callout, 0, sizeof(t->_callout));
	t->tl_func = func;
	t->tl_pending = false;

	shell->owner = t;
	shell->timer = osTimerNew(lkpi_timer_cb, osTimerOnce, t, NULL);
	shell->inited = (shell->timer != NULL) ? 1u : 0u;
}

int lkpi_mod_timer(struct timer_list *t, unsigned long expires)
{
	struct lkpi_callout_shell *shell =
	    (struct lkpi_callout_shell *)(void *)t->_callout;
	long delta = (long)(expires - lkpi_jiffies());
	int was;

	if (!shell->inited)
		return 0;

	was = shell->pending ? 1 : 0;
	if (delta < 1)
		delta = 1;

	t->tl_pending = true;
	shell->pending = 1;
	(void)osTimerStart(shell->timer, (uint32_t)delta);
	return was;
}

int lkpi_del_timer(struct timer_list *t)
{
	struct lkpi_callout_shell *shell =
	    (struct lkpi_callout_shell *)(void *)t->_callout;
	int was;

	if (!shell->inited)
		return 0;
	was = t->tl_pending ? 1 : 0;
	(void)osTimerStop(shell->timer);
	shell->pending = 0;
	t->tl_pending = false;
	return was;
}

int lkpi_del_timer_sync(struct timer_list *t)
{
	int was = lkpi_del_timer(t);

	/* The callback clears the pending flag as its first act, so waiting for the
	 * flag is waiting for a callback that was already in flight. */
	while (t->tl_pending)
		(void)osDelay(1);
	return was;
}

int lkpi_timer_pending(const struct timer_list *t)
{
	return t->tl_pending ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* wait queues                                                         */
/* ------------------------------------------------------------------ */

static struct lkpi_cv_shell *lkpi_wq_cv(struct wait_queue_head *wq)
{
	return (struct lkpi_cv_shell *)(void *)wq->_cv;
}

void lkpi_waitqueue_init(struct wait_queue_head *wq)
{
	struct lkpi_cv_shell *cv = lkpi_wq_cv(wq);

	memset(wq->_mtx, 0, sizeof(wq->_mtx));
	memset(wq->_cv, 0, sizeof(wq->_cv));

	lkpi_mutex_init((struct mutex *)wq->_mtx);
	/* Permits up to the number of waiters that can ever queue here; the count is
	 * bounded by the callers' structure, and a release without a waiter leaves a
	 * permit that the next wait consumes immediately - the same wake-up that
	 * arrived "early" is honoured, which is what the wait_event macros expect. */
	cv->sem = osSemaphoreNew(64u, 0u, NULL);
	cv->inited = (cv->sem != NULL) ? 1u : 0u;
}

void lkpi_waitqueue_wake(struct wait_queue_head *wq)
{
	struct lkpi_cv_shell *cv = lkpi_wq_cv(wq);
	uint32_t n = cv->waiters;

	/* Wake everyone: this queue's users re-check their condition under the lock,
	 * so a spurious wake costs one loop and never a missed one. */
	while (n-- > 0)
		(void)osSemaphoreRelease(cv->sem);
}

void lkpi_waitq_lock(struct wait_queue_head *wq)
{
	lkpi_mutex_lock((struct mutex *)wq->_mtx);
}

void lkpi_waitq_unlock(struct wait_queue_head *wq)
{
	lkpi_mutex_unlock((struct mutex *)wq->_mtx);
}

/*
 * Wait for a wake-up. Returns 1 when woken before the timeout and 0 on timeout - the
 * convention the contract header's macro comment states. The waiter count is what lets
 * a wake release exactly as many permits as there are sleepers.
 */
int lkpi_waitq_timedwait(struct wait_queue_head *wq, int ms)
{
	struct lkpi_cv_shell *cv = lkpi_wq_cv(wq);
	osStatus_t st;

	cv->waiters++;
	if (ms <= 0)
		st = osSemaphoreAcquire(cv->sem, osWaitForever);
	else
		st = osSemaphoreAcquire(cv->sem, (uint32_t)ms);
	if (cv->waiters > 0)
		cv->waiters--;

	return (st == osOK) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* module init entries                                                 */
/* ------------------------------------------------------------------ */

/*
 * The interface's module_init() lands each entry point in the .lkpi_init
 * section; this is the walker that runs them, in section order, once. The
 * section is bracketed by its own two symbols and kept apart from .lkpi_module
 * (the usb_driver registry, whose walk is lkpi_usb_init()), so the two walks
 * cannot disturb each other's byte layout. The bracket symbols are weak for the
 * same reason the module walker's are: a build that carries no entries says so
 * instead of failing to link.
 */
extern lkpi_init_fn_t __lkpi_init_start[] __attribute__((weak));
extern lkpi_init_fn_t __lkpi_init_end[] __attribute__((weak));

void lkpi_module_init_run(void)
{
	static bool walked;
	lkpi_init_fn_t *it;
	unsigned int n = 0;

	if (walked)
		return;
	walked = true;

	if (__lkpi_init_start == NULL || __lkpi_init_end == NULL ||
	    (uintptr_t)__lkpi_init_end <= (uintptr_t)__lkpi_init_start) {
		lkpi_printf(ULOG_INFO_LEVEL,
		    "lkpi: no .lkpi_init entries in this link\n");
		return;
	}
	for (it = __lkpi_init_start; it < __lkpi_init_end; it++) {
		if (*it == NULL)
			continue;
		(void)(*it)();
		n++;
	}
	lkpi_printf(ULOG_INFO_LEVEL, "lkpi: %u module init entr%s run\n", n,
	    n == 1u ? "y" : "ies");
}

/* ------------------------------------------------------------------ */
/* the calling thread's task object                                    */
/* ------------------------------------------------------------------ */

/*
 * The interface resolves `current` to the running task's record. On this port a
 * task IS a thread, so the body hands back one stable object per thread, keyed
 * on the identity the kernel gave it. The table is fixed and small, and a
 * thread that arrives when it is full is counted and refused - a NULL, never a
 * shared object: two threads sharing one identity would look alike to every
 * caller that compares `current`, and a driver's own "am I the interrupt
 * thread?" test compares exactly that. The table is sized for the images this
 * repository builds; an overflow is a bring-up bug and prints once.
 */
#define	LKPI_TASK_SLOTS		16

struct lkpi_task_slot {
	osThreadId_t		id;
	struct task_struct	task;
};

static struct lkpi_task_slot lkpi_task_slots[LKPI_TASK_SLOTS];
static unsigned int lkpi_task_count;
static unsigned int lkpi_task_overflow;

/*
 * The thread name is a nicety of the task record, and a minimal CMSIS backend
 * (the ones some host suites link) does not carry it. The reference is weak for
 * exactly that reason: a backend without the call leaves comm empty instead of
 * failing the link, and every other field of the record is still the port's own.
 */
extern const char *osThreadGetName(osThreadId_t thread_id) __attribute__((weak));

struct task_struct *lkpi_current(void)
{
	osThreadId_t id = osThreadGetId();
	unsigned int i;

	if (id == NULL)
		return NULL;
	for (i = 0; i < LKPI_TASK_SLOTS; i++) {
		if (lkpi_task_slots[i].id == id)
			return &lkpi_task_slots[i].task;
	}
	if (lkpi_task_count >= LKPI_TASK_SLOTS) {
		lkpi_task_overflow++;
		if (lkpi_task_overflow == 1u)
			lkpi_printf(ULOG_ERROR_LEVEL,
			    "lkpi: task identity table full (%u slots); a thread that "
			    "needs current must get a slot\n",
			    (unsigned int)LKPI_TASK_SLOTS);
		return NULL;
	}
	i = lkpi_task_count++;
	lkpi_task_slots[i].id = id;
	(void)memset(&lkpi_task_slots[i].task, 0, sizeof(lkpi_task_slots[i].task));
	lkpi_task_slots[i].task.pid = (pid_t)(i + 1u);
	if (osThreadGetName != NULL) {
		const char *name = osThreadGetName(id);

		if (name != NULL)
			(void)snprintf(lkpi_task_slots[i].task.comm,
			    sizeof(lkpi_task_slots[i].task.comm), "%s", name);
	}
	return &lkpi_task_slots[i].task;
}

/* ------------------------------------------------------------------ */
/* start-up                                                            */
/* ------------------------------------------------------------------ */

void lkpi_core_init(void)
{
	if (lkpi_system_wq != NULL)
		return;

	lkpi_system_wq = lkpi_alloc_workqueue("lkpi-system", 0, 1);
	if (lkpi_system_wq == NULL)
		lkpi_fatal("the system workqueue could not be created");
}