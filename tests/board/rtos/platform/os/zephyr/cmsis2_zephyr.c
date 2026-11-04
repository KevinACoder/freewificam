/*
 * @file   cmsis2_zephyr.c
 * @brief  The CMSIS-RTOS2 surface served directly on Zephyr's kernel.
 *
 * Third backend beside os/freertos/ (the upstream CMSIS-FreeRTOS adapter) and
 * os/threadx/ (a written adapter over ThreadX).  Nothing above this layer
 * changes: linuxkpi, net80211, the vendor driver, CherryUSB's osal and the cases
 * keep calling osThreadNew()/osSemaphoreAcquire()/osTimerStart(), and those
 * names land here on k_thread/k_sem/k_mutex/k_msgq/k_event/k_timer.
 *
 * Why a backend and not Zephyr's own subsys/portability/cmsis_rtos_v2: that
 * subsystem is missing osKernelInitialize/osKernelStart/osKernelGetState,
 * osMemoryAlloc/osMemoryFree/osMemoryGetStats, osCriticalEnter/osCriticalExit
 * and osExecProfileGet, it ignores the stack size the caller asks for (every
 * dynamic stack is one fixed size, thread.c), and it needs at least 56 preempt
 * priorities.  A backend is a few hundred lines and keeps every existing
 * semantic.
 *
 * The two rules that matter, both learned from the board-proven backends:
 *
 *  - A timer callback is deferred to a service thread.  Zephyr runs k_timer
 *    expiry inside the system clock ISR; this project's consumers time out
 *    into code that submits a USB transfer and therefore sleeps, which an ISR
 *    context may not do.  FreeRTOS (timer service task) and ThreadX (timer
 *    thread) both answer from a thread, so this backend does too: the k_timer
 *    expiry only queues the slot, and timer_service_thread() runs the callback.
 *
 *  - The kernel's "ticks" are milliseconds.  The port hardcodes hz = 1000
 *    (components/net80211/ports/osal/cmsis2/osal_cmsis2.c) and the whole image
 *    is written against osDelay(ms).  The build therefore sets
 *    CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000, and the check below refuses any other
 *    rate instead of silently scaling every timeout.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/arch/arm64/lib_helpers.h>

#include "os/cmsis2/cmsis2.h"

#if !defined(CONFIG_SYS_CLOCK_TICKS_PER_SEC) || \
	(CONFIG_SYS_CLOCK_TICKS_PER_SEC != 1000)
#error "the CMSIS-RTOS2 tick is the millisecond: set CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000"
#endif

#if !defined(CONFIG_DYNAMIC_THREAD) || !defined(CONFIG_DYNAMIC_THREAD_ALLOC)
#error "the zephyr backend allocates thread stacks: enable CONFIG_DYNAMIC_THREAD and CONFIG_DYNAMIC_THREAD_ALLOC"
#endif

#ifndef CMSIS2_MAX_THREADS
#define CMSIS2_MAX_THREADS 24
#endif
#ifndef CMSIS2_MAX_TIMERS
#define CMSIS2_MAX_TIMERS 32
#endif

/* Thread records: one per thread this backend created, plus one per foreign
 * thread that asked for its own identity (Zephyr's main thread, the system
 * work queue, the deferred-timer service thread).  A record is the handle the
 * rest of the image sees, so osThreadGetId() can answer without allocating. */
#define FWCAM_THREAD_SLOTS (CMSIS2_MAX_THREADS + 16)
#define FWCAM_THREAD_NAME  24

struct z_thread_rec {
	k_tid_t tid;			/* NULL: not running */
	k_thread_stack_t *stack;	/* NULL: not ours to free */
	size_t stack_size;
	uint8_t owned;			/* created here, so terminate frees it */
	uint8_t reserved;		/* claimed by a create in progress */
	char name[FWCAM_THREAD_NAME];
};

static struct z_thread_rec thread_recs[FWCAM_THREAD_SLOTS];
static struct k_spinlock thread_recs_lock;

/* Timer records.  The callback runs on the service thread, never in the
 * clock ISR, so a slot is only ever touched under the message-queue hand-off
 * and the small spinlock below. */
struct z_timer_rec {
	struct k_timer timer;
	osTimerFunc_t func;
	void *arg;
	uint8_t in_use;
	uint8_t periodic;
};

static struct z_timer_rec timer_recs[CMSIS2_MAX_TIMERS];

/* Service thread for timer callbacks.  It is created on the first timer, from
 * a thread context (osTimerNew is never called from an ISR here). */
K_THREAD_STACK_DEFINE(timer_service_stack, 2048);
static struct k_thread timer_service_tcb;
static struct k_msgq timer_queue;
static void *timer_queue_buf[16];
static struct k_spinlock timer_lock;
static uint8_t timer_service_started;

/* Event flag records: k_event plus the "last value" the CMSIS call returns. */
#ifndef CMSIS2_MAX_EVENT_FLAGS
#define CMSIS2_MAX_EVENT_FLAGS 8
#endif

struct z_event_rec {
	struct k_event event;
	uint8_t in_use;
};

static struct z_event_rec event_recs[CMSIS2_MAX_EVENT_FLAGS];

/* Message queues allocate their own buffer (the CMSIS attribute normally
 * names one, and none of this project's callers pass one). */
struct z_msgq_rec {
	struct k_msgq queue;
	void *buf;
};

/* --- helpers -------------------------------------------------------------- */

/* CMSIS priority band -> Zephyr preemptive priority.  osPriorityISR (56) is
 * the highest band and osPriorityLow (8) the lowest; every band maps into the
 * preemptible range so a thread never accidentally becomes cooperative (a
 * cooperative thread would starve the rest of the image).  A priority of 0 or
 * osPriorityNone means "not set": that is the CMSIS default, osPriorityNormal. */
static int prio_of(osPriority_t cmsis)
{
	int level;

	if ((int)cmsis <= (int)osPriorityNone) {
		cmsis = osPriorityNormal;
	}
	if ((int)cmsis > (int)osPriorityISR) {
		cmsis = osPriorityISR;
	}

	level = ((int)osPriorityISR - (int)cmsis) / 2;
	if (level < 0) {
		level = 0;
	}
	if (level > (CONFIG_NUM_PREEMPT_PRIORITIES - 1)) {
		level = CONFIG_NUM_PREEMPT_PRIORITIES - 1;
	}
	return K_PRIO_PREEMPT(level);
}

/* A CMSIS timeout is either "forever" or a tick count; zero means "do not
 * wait", which is the only form legal from an ISR. */
static k_timeout_t timeout_of(uint32_t ticks)
{
	if (ticks == osWaitForever) {
		return K_FOREVER;
	}
	return K_TICKS((int32_t)ticks);
}

static struct z_thread_rec *rec_of(osThreadId_t id)
{
	struct z_thread_rec *rec = (struct z_thread_rec *)id;
	uintptr_t lo = (uintptr_t)&thread_recs[0];
	uintptr_t hi = (uintptr_t)&thread_recs[FWCAM_THREAD_SLOTS];

	if ((uintptr_t)rec < lo || (uintptr_t)rec >= hi) {
		return NULL;
	}
	return rec;
}

/* Find the record for a live thread, or claim a free slot for it.  Called with
 * the spinlock held (or from a context that cannot race, see the callers). */
static struct z_thread_rec *rec_find_locked(k_tid_t tid)
{
	struct z_thread_rec *free_rec = NULL;
	int i;

	for (i = 0; i < FWCAM_THREAD_SLOTS; i++) {
		if (thread_recs[i].tid == tid) {
			return &thread_recs[i];
		}
		if (thread_recs[i].tid == NULL && thread_recs[i].reserved == 0u &&
		    free_rec == NULL) {
			free_rec = &thread_recs[i];
		}
	}
	return free_rec;
}

/* Claim a slot for a create in progress, so two concurrent creates cannot pick
 * the same one.  Returns the slot with reserved set, or NULL. */
static struct z_thread_rec *rec_reserve(void)
{
	struct z_thread_rec *rec;
	k_spinlock_key_t key = k_spin_lock(&thread_recs_lock);

	rec = rec_find_locked(NULL);
	if (rec != NULL) {
		rec->reserved = 1u;
		rec->tid = NULL;
		rec->stack = NULL;
		rec->stack_size = 0u;
		rec->owned = 0u;
		rec->name[0] = '\0';
	}
	k_spin_unlock(&thread_recs_lock, key);
	return rec;
}

static void rec_release(struct z_thread_rec *rec)
{
	k_spinlock_key_t key = k_spin_lock(&thread_recs_lock);

	rec->reserved = 0u;
	k_spin_unlock(&thread_recs_lock, key);
}

static struct z_thread_rec *rec_lookup(k_tid_t tid)
{
	struct z_thread_rec *rec;
	k_spinlock_key_t key = k_spin_lock(&thread_recs_lock);

	rec = rec_find_locked(tid);
	if (rec != NULL && rec->tid == NULL) {
		rec->tid = tid;
		rec->stack = NULL;
		rec->stack_size = 0u;
		rec->owned = 0u;
		rec->name[0] = '\0';
	}
	k_spin_unlock(&thread_recs_lock, key);
	return rec;
}

/* --- kernel --------------------------------------------------------------- */

osStatus_t osKernelInitialize(void)
{
	/* Zephyr's kernel is already up when main() runs; this is the CMSIS
	 * "before the scheduler" call and has nothing left to do. */
	return osOK;
}

osStatus_t osKernelStart(void)
{
	/* CMSIS: a successful osKernelStart() does not return - control passes
	 * to the threads.  On Zephyr the kernel is already scheduling, so the
	 * equivalent is "the caller's thread never runs again": park it forever.
	 * Returning osOK here would let every caller's post-start code (and
	 * main.c's fatal("scheduler returned")) execute on the highest-priority
	 * thread in the system, which would starve the runner it just created. */
	for (;;) {
		(void)k_sleep(K_FOREVER);
	}
}

osKernelState_t osKernelGetState(void)
{
	return k_is_pre_kernel() ? osKernelReady : osKernelRunning;
}

uint32_t osKernelGetTickCount(void)
{
	return (uint32_t)k_uptime_ticks();
}

uint32_t osKernelGetTickFreq(void)
{
	return (uint32_t)CONFIG_SYS_CLOCK_TICKS_PER_SEC;
}

uint32_t osKernelGetSysTimerCount(void)
{
	return k_cycle_get_32();
}

uint32_t osKernelGetSysTimerFreq(void)
{
	return (uint32_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
}

osStatus_t osDelay(uint32_t ticks)
{
	if (ticks == 0u) {
		k_yield();
		return osOK;
	}
	return (k_sleep(K_TICKS((int32_t)ticks)) == 0) ? osOK : osError;
}

osStatus_t osDelayUntil(uint32_t ticks)
{
	int64_t now = k_uptime_ticks();

	if ((int32_t)(ticks - (uint32_t)now) <= 0) {
		return osOK;
	}
	return (k_sleep(K_TICKS((int32_t)(ticks - (uint32_t)now))) == 0)
		       ? osOK
		       : osError;
}

/* --- threads -------------------------------------------------------------- */

static void thread_trampoline(void *p1, void *p2, void *p3)
{
	osThreadFunc_t func = (osThreadFunc_t)p1;

	ARG_UNUSED(p3);
	func(p2);
}

osThreadId_t osThreadNew(osThreadFunc_t func, void *argument,
			 const osThreadAttr_t *attr)
{
	struct z_thread_rec *rec;
	k_thread_stack_t *stack;
	struct k_thread *tcb;
	size_t stack_size;
	const char *name = NULL;
	osPriority_t prio = osPriorityNormal;

	if (func == NULL) {
		return NULL;
	}
	if (attr != NULL) {
		name = attr->name;
		prio = attr->priority;
		stack_size = attr->stack_size;
	} else {
		stack_size = 0u;
	}
	if (stack_size == 0u) {
		/* CMSIS leaves the size to the implementation; Zephyr's main
		 * stack is the image's own default and is the honest answer. */
		stack_size = CONFIG_MAIN_STACK_SIZE;
	}

	rec = rec_reserve();
	if (rec == NULL) {
		return NULL;
	}

	stack = k_thread_stack_alloc(stack_size, 0);
	if (stack == NULL) {
		rec_release(rec);
		return NULL;
	}
	tcb = k_malloc(sizeof(struct k_thread));
	if (tcb == NULL) {
		k_thread_stack_free(stack);
		rec_release(rec);
		return NULL;
	}

	rec->stack = stack;
	rec->stack_size = stack_size;
	rec->owned = 1u;
	if (name != NULL) {
		strncpy(rec->name, name, sizeof(rec->name) - 1u);
		rec->name[sizeof(rec->name) - 1u] = '\0';
	}

	rec->tid = k_thread_create(tcb, stack, stack_size, thread_trampoline,
				   (void *)func, argument, NULL, prio_of(prio),
				   0, K_NO_WAIT);
	if (rec->tid == NULL) {
		rec->stack = NULL;
		rec->stack_size = 0u;
		rec->owned = 0u;
		rec->name[0] = '\0';
		k_free(tcb);
		k_thread_stack_free(stack);
		rec_release(rec);
		return NULL;
	}
	rec_release(rec);
	return (osThreadId_t)rec;
}

osThreadId_t osThreadGetId(void)
{
	k_tid_t tid = k_current_get();
	struct z_thread_rec *rec = rec_lookup(tid);

	if (rec == NULL || rec->tid != tid) {
		return NULL;
	}
	return (osThreadId_t)rec;
}

const char *osThreadGetName(osThreadId_t thread_id)
{
	struct z_thread_rec *rec = rec_of(thread_id);

	if (rec == NULL) {
		return NULL;
	}
	if (rec->name[0] != '\0') {
		return rec->name;
	}
	if (rec->tid != NULL) {
		const char *name = k_thread_name_get(rec->tid);

		if (name != NULL && name[0] != '\0') {
			return name;
		}
	}
	return NULL;
}

osThreadState_t osThreadGetState(osThreadId_t thread_id)
{
	struct z_thread_rec *rec = rec_of(thread_id);
	uint8_t state;

	if (rec == NULL || rec->tid == NULL) {
		return osThreadInactive;
	}
	/* Read through the thread's own state word rather than the state
	 * *name*: the ledger reports these five values, and a name-based
	 * classification would fold "sleeping" and "pending" together. */
	state = rec->tid->base.thread_state;
	if ((state & _THREAD_DEAD) != 0u) {
		return osThreadTerminated;
	}
	if (rec->tid == k_current_get()) {
		return osThreadRunning;
	}
	if ((state & (_THREAD_PENDING | _THREAD_SUSPENDED)) != 0u) {
		return osThreadBlocked;
	}
	return osThreadReady;
}

osStatus_t osThreadSetPriority(osThreadId_t thread_id, osPriority_t priority)
{
	struct z_thread_rec *rec = rec_of(thread_id);

	if (rec == NULL || rec->tid == NULL) {
		return osErrorParameter;
	}
	k_thread_priority_set(rec->tid, prio_of(priority));
	return osOK;
}

osStatus_t osThreadYield(void)
{
	k_yield();
	return osOK;
}

osStatus_t osThreadTerminate(osThreadId_t thread_id)
{
	struct z_thread_rec *rec = rec_of(thread_id);
	k_thread_stack_t *stack;
	struct k_thread *tcb;
	int owned;

	if (rec == NULL || rec->tid == NULL) {
		return osErrorParameter;
	}

	tcb = (struct k_thread *)rec->tid;
	stack = rec->stack;
	owned = rec->owned;

	k_thread_abort(rec->tid);

	rec->tid = NULL;
	rec->stack = NULL;
	rec->owned = 0u;
	rec->name[0] = '\0';

	if (owned != 0) {
		/* The thread's own object and stack came from the heap in
		 * osThreadNew(); release them here so a terminate/create
		 * cycle does not leak a stack per iteration. */
		k_thread_stack_free(stack);
		k_free(tcb);
	}
	return osOK;
}

/* --- semaphores ----------------------------------------------------------- */

static osSemaphoreId_t sem_new(uint32_t max_count, uint32_t initial)
{
	struct k_sem *sem;

	if (k_is_in_isr()) {
		return NULL;
	}
	if (max_count == 0u) {
		max_count = 1u;
	}
	if (initial > max_count) {
		initial = max_count;
	}
	sem = k_malloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}
	if (k_sem_init(sem, initial, max_count) != 0) {
		k_free(sem);
		return NULL;
	}
	return (osSemaphoreId_t)sem;
}

osSemaphoreId_t osSemaphoreNew(uint32_t max_count, uint32_t initial_count,
			       const osSemaphoreAttr_t *attr)
{
	ARG_UNUSED(attr);
	return sem_new(max_count, initial_count);
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout)
{
	struct k_sem *sem = (struct k_sem *)semaphore_id;

	if (sem == NULL) {
		return osErrorParameter;
	}
	/* The interrupt path may not wait: a nonzero timeout from an ISR is a
	 * caller bug, and reporting it is better than blocking the system
	 * clock ISR. */
	if (k_is_in_isr() && timeout != 0u) {
		return osErrorParameter;
	}
	return (k_sem_take(sem, timeout_of(timeout)) == 0) ? osOK
		: (timeout == 0u) ? osErrorResource : osErrorTimeout;
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id)
{
	struct k_sem *sem = (struct k_sem *)semaphore_id;
	unsigned int key;
	osStatus_t status = osOK;

	if (sem == NULL) {
		return osErrorParameter;
	}
	/* A full semaphore reports osErrorResource, which is what the USB osal's
	 * usb_osal_sem_give() turns into -USB_ERR_TIMEOUT; k_sem_give() on its
	 * own would drop the extra token silently.  The check and the give have
	 * to be one step, and a spinlock is not the lock to hold across
	 * k_sem_give() (it reschedules); masking interrupts is, exactly as the
	 * critical-section pair above does. */
	key = irq_lock();
	if (k_sem_count_get(sem) >= sem->limit) {
		status = osErrorResource;
	} else {
		k_sem_give(sem);
	}
	irq_unlock(key);
	return status;
}

uint32_t osSemaphoreGetCount(osSemaphoreId_t semaphore_id)
{
	struct k_sem *sem = (struct k_sem *)semaphore_id;

	return (sem == NULL) ? 0u : k_sem_count_get(sem);
}

osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id)
{
	if (semaphore_id == NULL) {
		return osErrorParameter;
	}
	k_free(semaphore_id);
	return osOK;
}

/* --- mutexes -------------------------------------------------------------- */

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
	struct k_mutex *mutex;

	if (k_is_in_isr()) {
		return NULL;
	}
	ARG_UNUSED(attr);
	mutex = k_malloc(sizeof(*mutex));
	if (mutex == NULL) {
		return NULL;
	}
	/* Zephyr's mutex is recursive and reports its owner and lock count,
	 * which covers both CMSIS attribute bits the project uses
	 * (osMutexRecursive and osMutexPrioInherit). */
	k_mutex_init(mutex);
	return (osMutexId_t)mutex;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout)
{
	struct k_mutex *mutex = (struct k_mutex *)mutex_id;

	if (mutex == NULL) {
		return osErrorParameter;
	}
	if (k_is_in_isr()) {
		return osErrorISR;
	}
	return (k_mutex_lock(mutex, timeout_of(timeout)) == 0) ? osOK
		: (timeout == 0u) ? osErrorResource : osErrorTimeout;
}

osStatus_t osMutexRelease(osMutexId_t mutex_id)
{
	struct k_mutex *mutex = (struct k_mutex *)mutex_id;

	if (mutex == NULL) {
		return osErrorParameter;
	}
	return (k_mutex_unlock(mutex) == 0) ? osOK : osError;
}

osThreadId_t osMutexGetOwner(osMutexId_t mutex_id)
{
	struct k_mutex *mutex = (struct k_mutex *)mutex_id;

	if (mutex == NULL || mutex->owner == NULL) {
		return NULL;
	}
	return (osThreadId_t)rec_lookup(mutex->owner);
}

osStatus_t osMutexDelete(osMutexId_t mutex_id)
{
	if (mutex_id == NULL) {
		return osErrorParameter;
	}
	k_free(mutex_id);
	return osOK;
}

/* --- timers --------------------------------------------------------------- */

static void timer_service_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		struct z_timer_rec *rec = NULL;

		if (k_msgq_get(&timer_queue, &rec, K_FOREVER) != 0) {
			continue;
		}
		if (rec != NULL && rec->in_use != 0u && rec->func != NULL) {
			rec->func(rec->arg);
		}
	}
}

static void timer_expiry(struct k_timer *timer)
{
	struct z_timer_rec *rec = k_timer_user_data_get(timer);

	/* Runs in the system clock ISR: only hand the slot over.  A full queue
	 * drops this expiry; a periodic timer re-arms and the next expiry
	 * retries, and a one-shot has already done its work. */
	(void)k_msgq_put(&timer_queue, &rec, K_NO_WAIT);
}

static void timer_service_start(void)
{
	if (timer_service_started != 0u) {
		return;
	}
	k_msgq_init(&timer_queue, (char *)timer_queue_buf,
		    sizeof(timer_queue_buf[0]), ARRAY_SIZE(timer_queue_buf));
	k_thread_create(&timer_service_tcb, timer_service_stack,
			K_THREAD_STACK_SIZEOF(timer_service_stack),
			timer_service_thread, NULL, NULL, NULL,
			prio_of(osPriorityAboveNormal), 0, K_NO_WAIT);
	k_thread_name_set(&timer_service_tcb, "cmsis-timer");
	timer_service_started = 1u;
}

osTimerId_t osTimerNew(osTimerFunc_t func, osTimerType_t type, void *argument,
		       const osTimerAttr_t *attr)
{
	struct z_timer_rec *rec = NULL;
	k_spinlock_key_t key;
	int i;

	ARG_UNUSED(attr);
	if (func == NULL || k_is_in_isr()) {
		return NULL;
	}

	key = k_spin_lock(&timer_lock);
	for (i = 0; i < CMSIS2_MAX_TIMERS; i++) {
		if (timer_recs[i].in_use == 0u) {
			rec = &timer_recs[i];
			rec->in_use = 1u;
			break;
		}
	}
	k_spin_unlock(&timer_lock, key);

	if (rec == NULL) {
		return NULL;
	}

	rec->func = func;
	rec->arg = argument;
	rec->periodic = (type == osTimerPeriodic) ? 1u : 0u;

	k_timer_init(&rec->timer, timer_expiry, NULL);
	k_timer_user_data_set(&rec->timer, rec);

	timer_service_start();
	return (osTimerId_t)rec;
}

osStatus_t osTimerStart(osTimerId_t timer_id, uint32_t ticks)
{
	struct z_timer_rec *rec = (struct z_timer_rec *)timer_id;

	if (rec == NULL || rec->in_use == 0u) {
		return osErrorParameter;
	}
	if (ticks == 0u) {
		/* CMSIS has no "expire now"; every backend in this project
		 * refuses it rather than guessing. */
		return osErrorParameter;
	}
	/* Start or restart, which is what k_timer_start() does. */
	k_timer_start(&rec->timer, K_TICKS((int32_t)ticks),
		      (rec->periodic != 0u) ? K_TICKS((int32_t)ticks) : K_NO_WAIT);
	return osOK;
}

osStatus_t osTimerStop(osTimerId_t timer_id)
{
	struct z_timer_rec *rec = (struct z_timer_rec *)timer_id;

	if (rec == NULL || rec->in_use == 0u) {
		return osErrorParameter;
	}
	k_timer_stop(&rec->timer);
	return osOK;
}

uint32_t osTimerIsRunning(osTimerId_t timer_id)
{
	struct z_timer_rec *rec = (struct z_timer_rec *)timer_id;

	if (rec == NULL || rec->in_use == 0u) {
		return 0u;
	}
	return (k_timer_remaining_get(&rec->timer) != 0u) ? 1u : 0u;
}

osStatus_t osTimerDelete(osTimerId_t timer_id)
{
	struct z_timer_rec *rec = (struct z_timer_rec *)timer_id;
	k_spinlock_key_t key;

	if (rec == NULL || rec->in_use == 0u) {
		return osErrorParameter;
	}
	k_timer_stop(&rec->timer);

	key = k_spin_lock(&timer_lock);
	rec->in_use = 0u;
	rec->func = NULL;
	rec->arg = NULL;
	k_spin_unlock(&timer_lock, key);
	return osOK;
}

/* --- event flags ---------------------------------------------------------- */

osEventFlagsId_t osEventFlagsNew(const osEventFlagsAttr_t *attr)
{
	struct z_event_rec *rec = NULL;
	k_spinlock_key_t key;
	int i;

	ARG_UNUSED(attr);
	if (k_is_in_isr()) {
		return NULL;
	}

	key = k_spin_lock(&timer_lock);
	for (i = 0; i < CMSIS2_MAX_EVENT_FLAGS; i++) {
		if (event_recs[i].in_use == 0u) {
			rec = &event_recs[i];
			rec->in_use = 1u;
			break;
		}
	}
	k_spin_unlock(&timer_lock, key);

	if (rec == NULL) {
		return NULL;
	}
	k_event_init(&rec->event);
	return (osEventFlagsId_t)rec;
}

uint32_t osEventFlagsSet(osEventFlagsId_t ef_id, uint32_t flags)
{
	struct z_event_rec *rec = (struct z_event_rec *)ef_id;

	if (rec == NULL || rec->in_use == 0u) {
		return osFlagsErrorParameter;
	}
	return k_event_post(&rec->event, flags);
}

uint32_t osEventFlagsClear(osEventFlagsId_t ef_id, uint32_t flags)
{
	struct z_event_rec *rec = (struct z_event_rec *)ef_id;

	if (rec == NULL || rec->in_use == 0u) {
		return osFlagsErrorParameter;
	}
	return k_event_clear(&rec->event, flags);
}

uint32_t osEventFlagsGet(osEventFlagsId_t ef_id)
{
	struct z_event_rec *rec = (struct z_event_rec *)ef_id;

	if (rec == NULL || rec->in_use == 0u) {
		return osFlagsErrorParameter;
	}
	return k_event_wait(&rec->event, 0xFFFFFFFFu, false, K_NO_WAIT);
}

uint32_t osEventFlagsWait(osEventFlagsId_t ef_id, uint32_t flags,
			  uint32_t options, uint32_t timeout)
{
	struct z_event_rec *rec = (struct z_event_rec *)ef_id;
	bool reset;
	uint32_t ret;

	if (rec == NULL || rec->in_use == 0u) {
		return osFlagsErrorParameter;
	}

	reset = ((options & osFlagsNoClear) == 0u) ? true : false;

	if ((options & osFlagsWaitAll) != 0u) {
		ret = k_event_wait_all(&rec->event, flags, reset,
				       timeout_of(timeout));
	} else {
		ret = k_event_wait(&rec->event, flags, reset, timeout_of(timeout));
	}

	if (ret != 0u) {
		return ret;
	}
	return (timeout == 0u) ? osFlagsErrorResource : osFlagsErrorTimeout;
}

osStatus_t osEventFlagsDelete(osEventFlagsId_t ef_id)
{
	struct z_event_rec *rec = (struct z_event_rec *)ef_id;
	k_spinlock_key_t key;

	if (rec == NULL || rec->in_use == 0u) {
		return osErrorParameter;
	}
	key = k_spin_lock(&timer_lock);
	rec->in_use = 0u;
	k_spin_unlock(&timer_lock, key);
	return osOK;
}

/* --- message queues ------------------------------------------------------- */

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size,
				     const osMessageQueueAttr_t *attr)
{
	struct z_msgq_rec *rec;

	if (k_is_in_isr() || msg_count == 0u || msg_size == 0u) {
		return NULL;
	}
	ARG_UNUSED(attr);

	rec = k_malloc(sizeof(*rec));
	if (rec == NULL) {
		return NULL;
	}
	rec->buf = k_malloc((size_t)msg_count * (size_t)msg_size);
	if (rec->buf == NULL) {
		k_free(rec);
		return NULL;
	}
	k_msgq_init(&rec->queue, (char *)rec->buf, msg_size, msg_count);
	return (osMessageQueueId_t)rec;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr,
			     uint8_t msg_prio, uint32_t timeout)
{
	struct z_msgq_rec *rec = (struct z_msgq_rec *)mq_id;

	ARG_UNUSED(msg_prio);
	if (rec == NULL || msg_ptr == NULL) {
		return osErrorParameter;
	}
	if (k_is_in_isr() && timeout != 0u) {
		return osErrorParameter;
	}
	return (k_msgq_put(&rec->queue, msg_ptr, timeout_of(timeout)) == 0)
		       ? osOK
		       : (timeout == 0u) ? osErrorResource : osErrorTimeout;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr,
			     uint8_t *msg_prio, uint32_t timeout)
{
	struct z_msgq_rec *rec = (struct z_msgq_rec *)mq_id;

	ARG_UNUSED(msg_prio);
	if (rec == NULL || msg_ptr == NULL) {
		return osErrorParameter;
	}
	if (k_is_in_isr() && timeout != 0u) {
		return osErrorParameter;
	}
	return (k_msgq_get(&rec->queue, msg_ptr, timeout_of(timeout)) == 0)
		       ? osOK
		       : (timeout == 0u) ? osErrorResource : osErrorTimeout;
}

uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id)
{
	struct z_msgq_rec *rec = (struct z_msgq_rec *)mq_id;

	return (rec == NULL) ? 0u : k_msgq_num_used_get(&rec->queue);
}

uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id)
{
	struct z_msgq_rec *rec = (struct z_msgq_rec *)mq_id;

	return (rec == NULL) ? 0u : k_msgq_num_free_get(&rec->queue);
}

uint32_t osMessageQueueGetCapacity(osMessageQueueId_t mq_id)
{
	struct z_msgq_rec *rec = (struct z_msgq_rec *)mq_id;

	return (rec == NULL) ? 0u : rec->queue.max_msgs;
}

uint32_t osMessageQueueGetMsgSize(osMessageQueueId_t mq_id)
{
	struct z_msgq_rec *rec = (struct z_msgq_rec *)mq_id;

	return (rec == NULL) ? 0u : rec->queue.msg_size;
}

osStatus_t osMessageQueueReset(osMessageQueueId_t mq_id)
{
	struct z_msgq_rec *rec = (struct z_msgq_rec *)mq_id;

	if (rec == NULL) {
		return osErrorParameter;
	}
	k_msgq_purge(&rec->queue);
	return osOK;
}

osStatus_t osMessageQueueDelete(osMessageQueueId_t mq_id)
{
	struct z_msgq_rec *rec = (struct z_msgq_rec *)mq_id;

	if (rec == NULL) {
		return osErrorParameter;
	}
	k_free(rec->buf);
	k_free(rec);
	return osOK;
}

/* --- the heap this backend hands out -------------------------------------- */

/* The whole image allocates through these two names: the CMSIS-RTOS2 extension
 * (components/net80211's wlan_osal_alloc) and CherryUSB's osal, both of which
 * declare pvPortMalloc/vPortFree because the FreeRTOS line's heap is called
 * that.  On Zephyr they are the kernel heap, so one arena serves the kernel
 * objects and the drivers' buffers alike and a leak shows up in one gauge. */
void *pvPortMalloc(size_t size)
{
	return k_malloc(size);
}

void vPortFree(void *ptr)
{
	k_free(ptr);
}
