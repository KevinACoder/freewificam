/* SPDX-License-Identifier: GPL-2.0 */
/*
 * metal_cmsis2.c - CMSIS-RTOS2 API over the cooperative baremetal kernel.
 *
 * The kernel (metal_kernel.c) supplies threads, wait lists, timeouts and
 * software timers. This file supplies the object types the CMSIS API promises:
 * semaphores, mutexes, message queues, event flags, thread flags and timers.
 *
 * Rules that follow from the cooperative model:
 *   - every blocking call is a single metal_block_when() on the object's wait
 *     list, so the check and the enqueue happen with IRQs masked and a wakeup
 *     cannot fall into the gap between them;
 *   - priorities only decide who is picked next at a switch point, never who
 *     is preempted, and a woken thread is simply made READY;
 *   - the scheduler loop runs timers, so an osTimer callback is not in a
 *     thread: blocking calls there return osErrorResource instead of hanging.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cmsis2.h"
#include "metal_kernel.h"

#define METAL_OBJ_MAGIC 0x4d455441u

struct metal_sem {
	uint32_t magic;
	uint32_t count;
	uint32_t max;
	void *wait;
};

struct metal_mutex {
	uint32_t magic;
	struct metal_thread *owner;
	uint32_t count;
	uint8_t recursive;
	void *wait;
};

struct metal_queue {
	uint32_t magic;
	uint8_t *buf;
	uint32_t cap;
	uint32_t size;
	uint32_t count;
	uint32_t head;
	uint32_t tail;
	void *get_wait;
	void *put_wait;
};

struct metal_evt {
	uint32_t magic;
	uint32_t bits;
	void *wait;
};

static int s_kernel_state;
static int s_lock_nest;
static uint32_t s_lock_state;

static int obj_ok(const void *p)
{
	return p != NULL && *(const uint32_t *)p == METAL_OBJ_MAGIC;
}

static struct metal_thread *self_thread(void)
{
	return metal_self();
}

static uint64_t timeout_deadline(uint32_t ticks)
{
	if (ticks == osWaitForever)
		return 0ull;
	return metal_now_us() + (uint64_t)ticks * 1000ull;
}

/* ------------------------------------------------------------------ */
/* kernel                                                              */
/* ------------------------------------------------------------------ */

osStatus_t osKernelInitialize(void)
{
	if (s_kernel_state == osKernelRunning)
		return osOK;
	if (s_kernel_state != osKernelInactive) {
		s_kernel_state = osKernelInactive;
		return osOK;
	}
	metal_kernel_init();
	s_kernel_state = osKernelReady;
	return osOK;
}

osStatus_t osKernelGetInfo(osVersion_t *version, char *id_buf, uint32_t id_size)
{
	if (version != NULL) {
		version->api = 20010000u;
		version->kernel = 1000000u;
	}
	if (id_buf != NULL && id_size > 0u) {
		id_buf[0] = '\0';
	}
	return osOK;
}

osKernelState_t osKernelGetState(void)
{
	switch (s_kernel_state) {
	case osKernelRunning:
		return osKernelRunning;
	case osKernelReady:
		return osKernelReady;
	default:
		return osKernelInactive;
	}
}

osStatus_t osKernelStart(void)
{
	s_kernel_state = osKernelRunning;
	metal_kernel_start();
}

int32_t osKernelLock(void)
{
	int32_t prev = s_lock_nest;

	if (s_lock_nest == 0)
		s_lock_state = metal_irq_save();
	s_lock_nest++;
	return prev;
}

int32_t osKernelUnlock(void)
{
	int32_t prev = s_lock_nest;

	if (s_lock_nest > 0) {
		s_lock_nest--;
		if (s_lock_nest == 0)
			metal_irq_restore(s_lock_state);
	}
	return prev;
}

int32_t osKernelRestoreLock(int32_t lock)
{
	if (lock < 0)
		lock = 0;
	if (lock == 0) {
		if (s_lock_nest == 0)
			return 0;
		s_lock_nest = 0;
		metal_irq_restore(s_lock_state);
	} else {
		if (s_lock_nest == 0)
			s_lock_state = metal_irq_save();
		s_lock_nest = lock;
	}
	return lock;
}

uint32_t osKernelSuspend(void)
{
	return 0u;
}

void osKernelResume(uint32_t tick)
{
	(void)tick;
}

uint32_t osKernelGetTickCount(void)
{
	return metal_now_ms();
}

uint32_t osKernelGetTickFreq(void)
{
	return 1000u;
}

uint32_t osKernelGetSysTimerFreq(void)
{
	return 24000000u;
}

uint32_t osKernelGetSysTimerCount(void)
{
	uint64_t cnt;

	__asm__ volatile("mrs %0, cntvct_el0" : "=r"(cnt));
	return (uint32_t)cnt;
}

/* ------------------------------------------------------------------ */
/* critical sections                                                   */
/* ------------------------------------------------------------------ */

os_critical_state_t osCriticalEnter(void)
{
	return (os_critical_state_t)metal_irq_save();
}

void osCriticalExit(os_critical_state_t state)
{
	metal_irq_restore((uint32_t)state);
}

/* ------------------------------------------------------------------ */
/* threads                                                             */
/* ------------------------------------------------------------------ */

osThreadId_t osThreadNew(osThreadFunc_t func, void *argument,
			 const osThreadAttr_t *attr)
{
	const char *name = NULL;
	uint32_t stack = 8192u;
	int prio = (int)osPriorityNormal;
	struct metal_thread *t;

	if (func == NULL)
		return NULL;
	if (attr != NULL) {
		if (attr->name != NULL)
			name = attr->name;
		if (attr->stack_size != 0u)
			stack = attr->stack_size;
		if (attr->priority != osPriorityNone)
			prio = (int)attr->priority;
	}
	t = metal_thread_create((metal_fn_t)func, argument, name, prio, stack);
	return (osThreadId_t)t;
}

const char *osThreadGetName(osThreadId_t thread_id)
{
	struct metal_thread *t = (struct metal_thread *)thread_id;

	if (t == NULL)
		return NULL;
	return t->name;
}

osThreadId_t osThreadGetId(void)
{
	return (osThreadId_t)self_thread();
}

osThreadState_t osThreadGetState(osThreadId_t thread_id)
{
	struct metal_thread *t = (struct metal_thread *)thread_id;

	if (t == NULL)
		return osThreadError;
	if (t == self_thread())
		return osThreadRunning;
	switch (t->state) {
	case METAL_READY:
		return osThreadReady;
	case METAL_RUNNING:
		return osThreadRunning;
	case METAL_BLOCKED:
		return osThreadBlocked;
	case METAL_TERMINATED:
		return osThreadTerminated;
	default:
		return osThreadInactive;
	}
}

uint32_t osThreadGetStackSize(osThreadId_t thread_id)
{
	struct metal_thread *t = (struct metal_thread *)thread_id;

	return t != NULL ? t->stack_size : 0u;
}

uint32_t osThreadGetStackSpace(osThreadId_t thread_id)
{
	(void)thread_id;
	return 0u;
}

osStatus_t osThreadSetPriority(osThreadId_t thread_id, osPriority_t priority)
{
	struct metal_thread *t = (struct metal_thread *)thread_id;

	if (t == NULL || priority == osPriorityNone)
		return osErrorParameter;
	t->prio = (uint8_t)priority;
	return osOK;
}

osPriority_t osThreadGetPriority(osThreadId_t thread_id)
{
	struct metal_thread *t = (struct metal_thread *)thread_id;

	if (t == NULL)
		return osPriorityError;
	return (osPriority_t)t->prio;
}

osStatus_t osThreadYield(void)
{
	metal_yield();
	return osOK;
}

osStatus_t osThreadSuspend(osThreadId_t thread_id)
{
	(void)thread_id;
	return osError;
}

osStatus_t osThreadResume(osThreadId_t thread_id)
{
	(void)thread_id;
	return osError;
}

osStatus_t osThreadDetach(osThreadId_t thread_id)
{
	(void)thread_id;
	return osOK;
}

osStatus_t osThreadJoin(osThreadId_t thread_id)
{
	(void)thread_id;
	return osError;
}

void osThreadExit(void)
{
	metal_thread_terminate_self();
}

osStatus_t osThreadTerminate(osThreadId_t thread_id)
{
	struct metal_thread *t = (struct metal_thread *)thread_id;

	if (t == NULL)
		return osErrorParameter;
	if (t == self_thread()) {
		metal_thread_terminate_self();
		return osOK;
	}
	return metal_thread_terminate_id(t) == METAL_OK ? osOK : osError;
}

uint32_t osThreadGetCount(void)
{
	return metal_thread_count();
}

uint32_t osThreadEnumerate(osThreadId_t *thread_array, uint32_t array_items)
{
	(void)thread_array;
	(void)array_items;
	return 0u;
}

/* ------------------------------------------------------------------ */
/* thread flags                                                        */
/* ------------------------------------------------------------------ */

static uint32_t flags_match(uint32_t bits, uint32_t flags, uint32_t options)
{
	if ((options & osFlagsWaitAll) != 0u)
		return (bits & flags) == flags;
	return bits & flags;
}

uint32_t osThreadFlagsSet(osThreadId_t thread_id, uint32_t flags)
{
	struct metal_thread *t = (struct metal_thread *)thread_id;
	uint32_t state;

	if (t == NULL)
		return osFlagsErrorParameter;
	state = metal_irq_save();
	t->flags |= flags;
	if (t->state == METAL_BLOCKED && t->flags_wait != 0u) {
		uint32_t hit = flags_match(t->flags, t->flags_wait,
					   t->flags_opt);

		if (hit != 0u) {
			uint32_t clear = (t->flags_opt & osFlagsWaitAll) ?
				t->flags_wait : hit;

			if ((t->flags_opt & osFlagsNoClear) == 0u)
				t->flags &= ~clear;
			t->flag_result = hit;
			t->timeout_us = 0ull;
			t->wake_status = METAL_OK;
			t->state = METAL_READY;
		}
	}
	{
		uint32_t all = t->flags;

		metal_irq_restore(state);
		return all;
	}
}

uint32_t osThreadFlagsClear(uint32_t flags)
{
	struct metal_thread *t = self_thread();
	uint32_t state;

	if (t == NULL)
		return osFlagsErrorUnknown;
	state = metal_irq_save();
	t->flags &= ~flags;
	{
		uint32_t all = t->flags;

		metal_irq_restore(state);
		return all;
	}
}

uint32_t osThreadFlagsGet(void)
{
	struct metal_thread *t = self_thread();

	if (t == NULL)
		return 0u;
	return t->flags;
}

static int thread_flags_ready(void *arg)
{
	struct metal_thread *t = (struct metal_thread *)arg;

	return flags_match(t->flags, t->flags_wait, t->flags_opt) != 0u;
}

uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout)
{
	struct metal_thread *t = self_thread();
	uint64_t deadline;
	uint32_t hit;

	if (t == NULL)
		return osFlagsErrorUnknown;
	if (flags == 0u)
		return osFlagsErrorParameter;

	deadline = timeout_deadline(timeout);
	t->flags_wait = flags;
	t->flags_opt = options;
	for (;;) {
		int st = metal_block_when(NULL, deadline, thread_flags_ready, t);

		if (st == METAL_TIMEOUT) {
			t->flags_wait = 0u;
			return osFlagsErrorTimeout;
		}
		if (st != METAL_OK) {
			t->flags_wait = 0u;
			return osFlagsErrorResource;
		}
		hit = flags_match(t->flags, flags, options);
		if (hit != 0u) {
			break;
		}
	}
	t->flags_wait = 0u;
	if ((options & osFlagsNoClear) == 0u)
		t->flags &= ~(options & osFlagsWaitAll ? flags : hit);
	return hit;
}

/* ------------------------------------------------------------------ */
/* delays                                                              */
/* ------------------------------------------------------------------ */

osStatus_t osDelay(uint32_t ticks)
{
	if (ticks == 0u) {
		metal_yield();
		return osOK;
	}
	metal_block_on(NULL, (uint64_t)ticks * 1000ull);
	return osOK;
}

osStatus_t osDelayUntil(uint32_t ticks)
{
	uint32_t now = metal_now_ms();
	uint32_t delta = ticks - now;

	if (ticks <= now)
		return osOK;
	return osDelay(delta);
}

/* ------------------------------------------------------------------ */
/* timers                                                              */
/* ------------------------------------------------------------------ */

osTimerId_t osTimerNew(osTimerFunc_t func, osTimerType_t type, void *argument,
		       const osTimerAttr_t *attr)
{
	struct metal_timer *t;

	(void)attr;
	if (func == NULL)
		return NULL;
	t = metal_timer_alloc();
	if (t == NULL)
		return NULL;
	t->fn = (metal_fn_t)func;
	t->arg = argument;
	t->periodic = (type == osTimerPeriodic) ? 1u : 0u;
	return (osTimerId_t)t;
}

const char *osTimerGetName(osTimerId_t timer_id)
{
	(void)timer_id;
	return NULL;
}

osStatus_t osTimerStart(osTimerId_t timer_id, uint32_t ticks)
{
	struct metal_timer *t = (struct metal_timer *)timer_id;

	if (t == NULL || ticks == 0u)
		return osErrorParameter;
	return metal_timer_start(t, ticks) == 0 ? osOK : osError;
}

osStatus_t osTimerStop(osTimerId_t timer_id)
{
	struct metal_timer *t = (struct metal_timer *)timer_id;

	if (t == NULL)
		return osErrorParameter;
	return metal_timer_stop(t) == 0 ? osOK : osError;
}

uint32_t osTimerIsRunning(osTimerId_t timer_id)
{
	struct metal_timer *t = (struct metal_timer *)timer_id;

	return t != NULL && t->active ? 1u : 0u;
}

osStatus_t osTimerDelete(osTimerId_t timer_id)
{
	struct metal_timer *t = (struct metal_timer *)timer_id;

	if (t == NULL)
		return osErrorParameter;
	metal_timer_release(t);
	return osOK;
}

/* ------------------------------------------------------------------ */
/* event flags                                                         */
/* ------------------------------------------------------------------ */

static void *evt_find_waiter(struct metal_evt *e)
{
	struct metal_thread *w = *(struct metal_thread **)&e->wait;
	struct metal_thread *best = NULL;

	while (w != NULL) {
		if (flags_match(e->bits, w->flags_wait, w->flags_opt) != 0u) {
			if (best == NULL || w->prio > best->prio)
				best = w;
		}
		w = w->wait_next;
	}
	return best;
}

osEventFlagsId_t osEventFlagsNew(const osEventFlagsAttr_t *attr)
{
	struct metal_evt *e;

	(void)attr;
	e = (struct metal_evt *)metal_heap_alloc(sizeof(*e));
	if (e == NULL)
		return NULL;
	memset(e, 0, sizeof(*e));
	e->magic = METAL_OBJ_MAGIC;
	return (osEventFlagsId_t)e;
}

uint32_t osEventFlagsSet(osEventFlagsId_t ef_id, uint32_t flags)
{
	struct metal_evt *e = (struct metal_evt *)ef_id;
	uint32_t state;

	if (!obj_ok(e))
		return osFlagsErrorParameter;
	state = metal_irq_save();
	e->bits |= flags;
	for (;;) {
		struct metal_thread *w = evt_find_waiter(e);
		struct metal_thread **pp;
		uint32_t hit, clear;

		if (w == NULL)
			break;
		hit = flags_match(e->bits, w->flags_wait, w->flags_opt);
		clear = (w->flags_opt & osFlagsWaitAll) ? w->flags_wait : hit;
		if ((w->flags_opt & osFlagsNoClear) == 0u)
			e->bits &= ~clear;

		pp = (struct metal_thread **)&e->wait;
		while (*pp != NULL && *pp != w)
			pp = &(*pp)->wait_next;
		if (*pp == w)
			*pp = w->wait_next;
		w->wait_next = NULL;
		w->wait_head = NULL;
		w->flag_result = hit;
		w->timeout_us = 0ull;
		w->wake_status = METAL_OK;
		w->state = METAL_READY;
	}
	{
		uint32_t bits = e->bits;

		metal_irq_restore(state);
		return bits;
	}
}

uint32_t osEventFlagsClear(osEventFlagsId_t ef_id, uint32_t flags)
{
	struct metal_evt *e = (struct metal_evt *)ef_id;
	uint32_t state, old;

	if (!obj_ok(e))
		return osFlagsErrorParameter;
	state = metal_irq_save();
	old = e->bits;
	e->bits &= ~flags;
	metal_irq_restore(state);
	return old;
}

uint32_t osEventFlagsGet(osEventFlagsId_t ef_id)
{
	struct metal_evt *e = (struct metal_evt *)ef_id;
	uint32_t state, bits;

	if (!obj_ok(e))
		return 0u;
	state = metal_irq_save();
	bits = e->bits;
	metal_irq_restore(state);
	return bits;
}

static int evt_ready(void *arg)
{
	struct metal_evt *e = (struct metal_evt *)arg;

	return evt_find_waiter(e) != NULL;
}

uint32_t osEventFlagsWait(osEventFlagsId_t ef_id, uint32_t flags,
			  uint32_t options, uint32_t timeout)
{
	struct metal_evt *e = (struct metal_evt *)ef_id;
	struct metal_thread *t = self_thread();
	uint64_t deadline;
	uint32_t hit;

	if (!obj_ok(e) || t == NULL || flags == 0u)
		return osFlagsErrorParameter;

	t->flags_wait = flags;
	t->flags_opt = options;
	deadline = timeout_deadline(timeout);
	for (;;) {
		int st = metal_block_when(&e->wait, deadline, evt_ready, e);

		if (st == METAL_TIMEOUT) {
			t->flags_wait = 0u;
			return osFlagsErrorTimeout;
		}
		if (st != METAL_OK) {
			t->flags_wait = 0u;
			return osFlagsErrorResource;
		}
		hit = flags_match(e->bits, flags, options);
		if (hit != 0u)
			break;
	}
	t->flags_wait = 0u;
	return hit;
}

osStatus_t osEventFlagsDelete(osEventFlagsId_t ef_id)
{
	struct metal_evt *e = (struct metal_evt *)ef_id;

	if (!obj_ok(e))
		return osErrorParameter;
	e->magic = 0u;
	metal_heap_free(e);
	return osOK;
}

/* ------------------------------------------------------------------ */
/* mutexes                                                             */
/* ------------------------------------------------------------------ */

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
	struct metal_mutex *m;

	m = (struct metal_mutex *)metal_heap_alloc(sizeof(*m));
	if (m == NULL)
		return NULL;
	memset(m, 0, sizeof(*m));
	m->magic = METAL_OBJ_MAGIC;
	m->recursive = (attr != NULL &&
			(attr->attr_bits & osMutexRecursive) != 0u) ? 1u : 0u;
	return (osMutexId_t)m;
}

static int mutex_free(void *arg)
{
	struct metal_mutex *m = (struct metal_mutex *)arg;

	return m->owner == NULL;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout)
{
	struct metal_mutex *m = (struct metal_mutex *)mutex_id;
	struct metal_thread *t = self_thread();
	uint64_t deadline;

	if (!obj_ok(m) || t == NULL)
		return osErrorParameter;

	if (timeout == 0u) {
		uint32_t state = metal_irq_save();

		if (m->owner == NULL) {
			m->owner = t;
			m->count = 1u;
			metal_irq_restore(state);
			return osOK;
		}
		if (m->owner == t && m->recursive) {
			m->count++;
			metal_irq_restore(state);
			return osOK;
		}
		metal_irq_restore(state);
		return osErrorResource;
	}

	deadline = timeout_deadline(timeout);
	for (;;) {
		int st = metal_block_when(&m->wait, deadline, mutex_free, m);
		uint32_t state;

		if (st == METAL_TIMEOUT)
			return osErrorTimeout;
		if (st != METAL_OK)
			return osErrorResource;
		state = metal_irq_save();
		if (m->owner == NULL) {
			m->owner = t;
			m->count = 1u;
			metal_irq_restore(state);
			return osOK;
		}
		if (m->owner == t && m->recursive) {
			m->count++;
			metal_irq_restore(state);
			return osOK;
		}
		metal_irq_restore(state);
	}
}

osStatus_t osMutexRelease(osMutexId_t mutex_id)
{
	struct metal_mutex *m = (struct metal_mutex *)mutex_id;
	struct metal_thread *t = self_thread();
	uint32_t state;

	if (!obj_ok(m))
		return osErrorParameter;
	state = metal_irq_save();
	if (m->owner != t) {
		metal_irq_restore(state);
		return osErrorResource;
	}
	m->count--;
	if (m->count == 0u) {
		m->owner = NULL;
		metal_wake_one(&m->wait, METAL_OK);
	}
	metal_irq_restore(state);
	return osOK;
}

osThreadId_t osMutexGetOwner(osMutexId_t mutex_id)
{
	struct metal_mutex *m = (struct metal_mutex *)mutex_id;

	if (!obj_ok(m))
		return NULL;
	return (osThreadId_t)m->owner;
}

osStatus_t osMutexDelete(osMutexId_t mutex_id)
{
	struct metal_mutex *m = (struct metal_mutex *)mutex_id;

	if (!obj_ok(m))
		return osErrorParameter;
	m->magic = 0u;
	metal_heap_free(m);
	return osOK;
}

/* ------------------------------------------------------------------ */
/* semaphores                                                          */
/* ------------------------------------------------------------------ */

osSemaphoreId_t osSemaphoreNew(uint32_t max_count, uint32_t initial_count,
			       const osSemaphoreAttr_t *attr)
{
	struct metal_sem *s;

	(void)attr;
	if (max_count == 0u || initial_count > max_count)
		return NULL;
	s = (struct metal_sem *)metal_heap_alloc(sizeof(*s));
	if (s == NULL)
		return NULL;
	memset(s, 0, sizeof(*s));
	s->magic = METAL_OBJ_MAGIC;
	s->max = max_count;
	s->count = initial_count;
	return (osSemaphoreId_t)s;
}

static int sem_ready(void *arg)
{
	struct metal_sem *s = (struct metal_sem *)arg;

	return s->count > 0u;
}

static int sem_try_take(struct metal_sem *s)
{
	int ok = 0;
	uint32_t state = metal_irq_save();

	if (s->count > 0u) {
		s->count--;
		ok = 1;
	}
	metal_irq_restore(state);
	return ok;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout)
{
	struct metal_sem *s = (struct metal_sem *)semaphore_id;
	uint64_t deadline;

	if (!obj_ok(s))
		return osErrorParameter;
	if (timeout == 0u)
		return sem_try_take(s) ? osOK : osErrorResource;

	deadline = timeout_deadline(timeout);
	for (;;) {
		int st = metal_block_when(&s->wait, deadline, sem_ready, s);

		if (st == METAL_TIMEOUT)
			return osErrorTimeout;
		if (st != METAL_OK)
			return osErrorResource;
		if (sem_try_take(s))
			return osOK;
	}
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id)
{
	struct metal_sem *s = (struct metal_sem *)semaphore_id;
	uint32_t state;

	if (!obj_ok(s))
		return osErrorParameter;
	state = metal_irq_save();
	if (s->count < s->max) {
		s->count++;
		metal_wake_one(&s->wait, METAL_OK);
	}
	metal_irq_restore(state);
	return osOK;
}

uint32_t osSemaphoreGetCount(osSemaphoreId_t semaphore_id)
{
	struct metal_sem *s = (struct metal_sem *)semaphore_id;
	uint32_t state, n;

	if (!obj_ok(s))
		return 0u;
	state = metal_irq_save();
	n = s->count;
	metal_irq_restore(state);
	return n;
}

osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id)
{
	struct metal_sem *s = (struct metal_sem *)semaphore_id;

	if (!obj_ok(s))
		return osErrorParameter;
	s->magic = 0u;
	metal_heap_free(s);
	return osOK;
}

/* ------------------------------------------------------------------ */
/* message queues                                                      */
/* ------------------------------------------------------------------ */

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size,
				     const osMessageQueueAttr_t *attr)
{
	struct metal_queue *q;

	(void)attr;
	if (msg_count == 0u || msg_size == 0u)
		return NULL;
	if (msg_count > (UINT32_MAX / 2u) / msg_size)
		return NULL;
	q = (struct metal_queue *)metal_heap_alloc(sizeof(*q));
	if (q == NULL)
		return NULL;
	memset(q, 0, sizeof(*q));
	q->buf = (uint8_t *)metal_heap_alloc(msg_count * msg_size);
	if (q->buf == NULL) {
		metal_heap_free(q);
		return NULL;
	}
	q->magic = METAL_OBJ_MAGIC;
	q->cap = msg_count;
	q->size = msg_size;
	return (osMessageQueueId_t)q;
}

static int queue_space(void *arg)
{
	struct metal_queue *q = (struct metal_queue *)arg;

	return q->count < q->cap;
}

static int queue_data(void *arg)
{
	struct metal_queue *q = (struct metal_queue *)arg;

	return q->count > 0u;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr,
			     uint8_t msg_prio, uint32_t timeout)
{
	struct metal_queue *q = (struct metal_queue *)mq_id;
	uint64_t deadline;

	(void)msg_prio;
	if (!obj_ok(q) || msg_ptr == NULL)
		return osErrorParameter;

	if (timeout == 0u) {
		uint32_t state = metal_irq_save();

		if (q->count >= q->cap) {
			metal_irq_restore(state);
			return osErrorResource;
		}
		memcpy(q->buf + q->tail * q->size, msg_ptr, q->size);
		q->tail = (q->tail + 1u) % q->cap;
		q->count++;
		metal_wake_one(&q->get_wait, METAL_OK);
		metal_irq_restore(state);
		return osOK;
	}

	deadline = timeout_deadline(timeout);
	for (;;) {
		int st = metal_block_when(&q->put_wait, deadline, queue_space, q);
		uint32_t state;

		if (st == METAL_TIMEOUT)
			return osErrorTimeout;
		if (st != METAL_OK)
			return osErrorResource;
		state = metal_irq_save();
		if (q->count < q->cap) {
			memcpy(q->buf + q->tail * q->size, msg_ptr, q->size);
			q->tail = (q->tail + 1u) % q->cap;
			q->count++;
			metal_wake_one(&q->get_wait, METAL_OK);
			metal_irq_restore(state);
			return osOK;
		}
		metal_irq_restore(state);
	}
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr,
			     uint8_t *msg_prio, uint32_t timeout)
{
	struct metal_queue *q = (struct metal_queue *)mq_id;
	uint64_t deadline;

	(void)msg_prio;
	if (!obj_ok(q) || msg_ptr == NULL)
		return osErrorParameter;

	if (timeout == 0u) {
		uint32_t state = metal_irq_save();

		if (q->count == 0u) {
			metal_irq_restore(state);
			return osErrorResource;
		}
		memcpy(msg_ptr, q->buf + q->head * q->size, q->size);
		q->head = (q->head + 1u) % q->cap;
		q->count--;
		metal_wake_one(&q->put_wait, METAL_OK);
		metal_irq_restore(state);
		return osOK;
	}

	deadline = timeout_deadline(timeout);
	for (;;) {
		int st = metal_block_when(&q->get_wait, deadline, queue_data, q);
		uint32_t state;

		if (st == METAL_TIMEOUT)
			return osErrorTimeout;
		if (st != METAL_OK)
			return osErrorResource;
		state = metal_irq_save();
		if (q->count > 0u) {
			memcpy(msg_ptr, q->buf + q->head * q->size, q->size);
			q->head = (q->head + 1u) % q->cap;
			q->count--;
			metal_wake_one(&q->put_wait, METAL_OK);
			metal_irq_restore(state);
			return osOK;
		}
		metal_irq_restore(state);
	}
}

uint32_t osMessageQueueGetCapacity(osMessageQueueId_t mq_id)
{
	struct metal_queue *q = (struct metal_queue *)mq_id;

	return obj_ok(q) ? q->cap : 0u;
}

uint32_t osMessageQueueGetMsgSize(osMessageQueueId_t mq_id)
{
	struct metal_queue *q = (struct metal_queue *)mq_id;

	return obj_ok(q) ? q->size : 0u;
}

uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id)
{
	struct metal_queue *q = (struct metal_queue *)mq_id;
	uint32_t state, n;

	if (!obj_ok(q))
		return 0u;
	state = metal_irq_save();
	n = q->count;
	metal_irq_restore(state);
	return n;
}

uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id)
{
	struct metal_queue *q = (struct metal_queue *)mq_id;
	uint32_t state, n;

	if (!obj_ok(q))
		return 0u;
	state = metal_irq_save();
	n = q->cap - q->count;
	metal_irq_restore(state);
	return n;
}

osStatus_t osMessageQueueReset(osMessageQueueId_t mq_id)
{
	struct metal_queue *q = (struct metal_queue *)mq_id;
	uint32_t state;

	if (!obj_ok(q))
		return osErrorParameter;
	state = metal_irq_save();
	q->head = 0u;
	q->tail = 0u;
	q->count = 0u;
	metal_wake_all(&q->put_wait, METAL_OK);
	metal_irq_restore(state);
	return osOK;
}

osStatus_t osMessageQueueDelete(osMessageQueueId_t mq_id)
{
	struct metal_queue *q = (struct metal_queue *)mq_id;

	if (!obj_ok(q))
		return osErrorParameter;
	q->magic = 0u;
	metal_heap_free(q->buf);
	q->buf = NULL;
	metal_heap_free(q);
	return osOK;
}

/* ------------------------------------------------------------------ */
/* memory                                                              */
/* ------------------------------------------------------------------ */

void *osMemoryAlloc(size_t size)
{
	if (size == 0u)
		return NULL;
	return metal_heap_alloc(size);
}

void osMemoryFree(void *ptr)
{
	if (ptr != NULL)
		metal_heap_free(ptr);
}

void osMemoryGetStats(struct osMemoryStats *stats)
{
	size_t free_bytes = 0u, min_free = 0u, total = 0u;
	uint32_t fails = 0u;

	if (stats == NULL)
		return;
	metal_heap_stats(&free_bytes, &min_free, &total, &fails);
	stats->free_bytes = free_bytes;
	stats->min_free_bytes = min_free;
	stats->total_bytes = total;
	stats->alloc_fail = fails;
}