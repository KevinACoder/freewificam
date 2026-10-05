/*
 * @file   cmsis2_threadx.c
 * @brief  CMSIS-RTOS2 shim over the ThreadX UP kernel (SPEC S3
 *         threadx-up-boot §3.5.2) - the kernel replacement seam.
 *
 * The app layer sees cmsis_os2.h only; this file is the ThreadX-side
 * implementation (the baremetal timebase build compiles cmsis2_shim.c
 * instead, never both - Makefile CONFIG_OS switch). Interface invariants
 * are S1 §3.7.3: tick semantics via the kernel (tx_time_get), printing
 * through board_log, no direct GIC/timer register touches.
 *
 * API face this round (宁少勿多, spec §3.5.2): kernel state/tick/systimer,
 * lock depth stack, thread create (slot pool + deferred creation) /
 * terminate / id / yield, delay, thread flags (incl. the ISR-set entry
 * cherrysh S4 builds on). Semaphores/mutexes/queues/timers/memory pools
 * are deliberately NOT here - first consumer brings them (see the spec's
 * 扩面义务 note).
 *
 * Priority map: CMSIS 1..56 (larger = more urgent) -> ThreadX 0..31
 * (smaller = more urgent) via (TX_MAX_PRIORITIES-1) - band*4 with
 * band = priority/8 (cap 7). osPriorityLow/Normal/High land on 27/19/11
 * (board-measured via the map log line at first use, spec §3.5.2).
 */

#include <stddef.h>
#include <stdint.h>

#include "tx_api.h"
#include "tx_thread.h"
#include "tx_event_flags.h"
#include "tx_mutex.h"
#include "tx_semaphore.h"
#include "tx_queue.h"

#include "cmsis_os2.h"
#include "generic_timer.h"
#include "arch_asm.h"

#include "cmsis2_diag.h"
#include "heap.h"
#include "log.h"

#ifndef CONFIG_TX_MAX_THREADS
#define CONFIG_TX_MAX_THREADS	16
#endif
#ifndef CONFIG_TX_TICKS_PER_SECOND
#define CONFIG_TX_TICKS_PER_SECOND	1000
#endif
#ifndef CONFIG_CMSIS2_VERSION
#define CONFIG_CMSIS2_VERSION	"threadx"
#endif

/* Default thread stack: must hold an ISR frame too (spec §3.4.2). */
#define CMSIS_DEFAULT_STACK	4096U

/* --- thread slot pool -------------------------------------------------------- */

typedef enum {
	SLOT_FREE = 0,
	SLOT_PENDING,	/* claimed before the kernel ran, created at define */
	SLOT_LIVE,	/* tx_thread_create done */
} slot_state_t;

typedef struct {
	TX_THREAD thread;
	TX_EVENT_FLAGS_GROUP flags;
	osThreadFunc_t func;
	void *arg;
	char name[16];
	void *stack;
	uint32_t stack_size;
	UINT tx_prio;
	slot_state_t state;
} thread_slot_t;

static thread_slot_t slots[CONFIG_TX_MAX_THREADS];

/* Kernel lifecycle (spec §3.5.2: Inactive -> Ready at Initialize -> Running
 * at Start, which never returns). */
static enum {
	K_INACTIVE = 0,
	K_READY,
	K_RUNNING,
} kernel_state;

/* --- priority mapping ---------------------------------------------------------- */

/* One logged line per mapped priority, so the board log pins the formula's
 * actual values (the prior project's comment/arithmetic mismatch, spec
 * §3.5.2). Ringed: the smoke's three values cover the log once. */
static unsigned int map_priority(osPriority_t prio)
{
	unsigned int band;
	unsigned int mapped;

	if ((unsigned int)prio < 8U) {
		band = 0U;
	} else {
		band = ((unsigned int)prio / 8U);
		if (band > 7U) {
			band = 7U;
		}
	}
	mapped = (TX_MAX_PRIORITIES - 1U) - band * 4U;
	board_log("cmsis2: prio %d -> tx %u\n", (int)prio, mapped);
	return mapped;
}

/* --- thread creation ------------------------------------------------------------ */

static void slot_entry(ULONG input)
{
	thread_slot_t *slot = &slots[input];

	slot->func(slot->arg);
	/* CMSIS thread functions returning is equivalent to osThreadTerminate
	 * on self (os_threadlib semantics); the wrapper parks instead so the
	 * slot stays LIVE and its stack stays owned. The parking MUST go
	 * through the kernel: the original plain `wfe` spin never yielded,
	 * so a returned thread stayed the running thread at its priority and
	 * starved every lower-priority thread (board-proven 2026-10-02: the
	 * first-ever returning thread, autorun@tx15, froze gmacrx/shell/
	 * heartbeat@tx19 while the tick kept ticking - network pings died
	 * too). Self-suspend parks it out of the scheduler; the wfe loop
	 * below only matters if something later resumes the thread. */
	(void)tx_thread_suspend(&slot->thread);
	for (;;) {
		ARCH_WFE();
	}
}

static UINT slot_tx_create(thread_slot_t *slot)
{
	return tx_thread_create(&slot->thread, slot->name, slot_entry,
				(ULONG)(slot - slots), slot->stack,
				slot->stack_size, slot->tx_prio,
				slot->tx_prio, TX_NO_TIME_SLICE,
				TX_AUTO_START);
}

/* Event-flags created-list forensics (2026-10-02 console-outage round):
 * txe_event_flags_create reports TX_GROUP_ERROR when its walk over
 * _tx_event_flags_created_ptr/created_count ends on the caller's own
 * pointer - i.e. when the kernel's list state itself is wrong. One
 * summary line per create records the head/count growth, and on failure
 * a bounded walk dumps the first nodes, so the boot log shows both when
 * the list goes bad and what it contains. */
static void ef_trace(thread_slot_t *slot, int detail)
{
	TX_EVENT_FLAGS_GROUP *g = _tx_event_flags_created_ptr;
	unsigned int i;

	board_log("cmsis2: ef '%s' idx=%u ptr=%p count=%lu head=%p\n",
		  slot->name, (unsigned)(slot - slots), (void *)&slot->flags,
		  (unsigned long)_tx_event_flags_created_count, (void *)g);

	if (!detail) {
		return;
	}
	for (i = 0U; i < 8U; i++) {
		TX_EVENT_FLAGS_GROUP *next;

		if (g == NULL) {
			break;
		}
		/* Deref only inside the image+heap window; a wild pointer must
		 * not fault the system that is being diagnosed. */
		if ((uintptr_t)g < 0x0a000000UL ||
		    (uintptr_t)g >= 0x0a800000UL) {
			board_log("cmsis2: ef list[%u] ptr=%p OUT-OF-RANGE\n",
				  i, (void *)g);
			break;
		}
		next = g->tx_event_flags_group_created_next;
		board_log("cmsis2: ef list[%u] ptr=%p name=%s id=%lx next=%p\n",
			  i, (void *)g,
			  (g->tx_event_flags_group_name != NULL) ?
				  g->tx_event_flags_group_name : "?",
			  (unsigned long)g->tx_event_flags_group_id,
			  (void *)next);
		if (next == g || next == _tx_event_flags_created_ptr) {
			break;
		}
		g = next;
	}
}

osThreadId_t osThreadNew(osThreadFunc_t func, void *argument,
			 const osThreadAttr_t *attr)
{
	thread_slot_t *slot = NULL;
	unsigned int i;
	uint32_t stack_size;
	osPriority_t prio = (attr != NULL) ? attr->priority : osPriorityNormal;

	/* Legal CMSIS band is 1..osPriorityISR(56); this vendored V2.3 header
	 * marks illegal priority as osPriorityError = -1 (NOT 64 - the
	 * guard below must not use it as an upper bound). */
	if (func == NULL || prio <= osPriorityNone || prio > osPriorityISR) {
		return NULL;
	}

	for (i = 0U; i < (unsigned)CONFIG_TX_MAX_THREADS; i++) {
		if (slots[i].state == SLOT_FREE) {
			slot = &slots[i];
			break;
		}
	}
	if (slot == NULL) {
		board_log("cmsis2: slot pool exhausted (%u)\n",
			  (unsigned)CONFIG_TX_MAX_THREADS);
		return NULL;
	}

	/* Claim the slot before anything else. tx_thread_create(TX_AUTO_START)
	 * can switch into the new thread before returning, and that thread may
	 * itself call osThreadNew - an unclaimed slot gets re-selected and the
	 * event-flags duplicate check fires (TX_GROUP_ERROR 0x6, board-proven
	 * 2026-10-02: tcpip@tx11 preempted mid-create of itself; tcpip_ready's
	 * osThreadNew('autorun') re-claimed slots[4] and found its own group
	 * already in the created list). */
	slot->state = SLOT_PENDING;

	stack_size = (attr != NULL && attr->stack_size != 0U)
			     ? attr->stack_size
			     : CMSIS_DEFAULT_STACK;
	stack_size = (stack_size + 15U) & ~15U;
	slot->stack = pvPortMalloc(stack_size);
	if (slot->stack == NULL) {
		return NULL;
	}

	slot->func = func;
	slot->arg = argument;
	slot->tx_prio = (UINT)map_priority(prio);
	{
		const char *src = (attr != NULL && attr->name != NULL) ?
					  attr->name :
					  "cmsis";
		unsigned int k;

		for (k = 0U; k < sizeof(slot->name) - 1U && src[k] != '\0';
		     k++) {
			slot->name[k] = src[k];
		}
		slot->name[k] = '\0';
	}
	slot->stack_size = stack_size;

	{
		UINT rc;

		ef_trace(slot, 0);
		rc = tx_event_flags_create(&slot->flags, slot->name);

		if (rc != TX_SUCCESS) {
			vPortFree(slot->stack);
			slot->stack = NULL;
			slot->state = SLOT_FREE;
			board_log("cmsis2: event_flags_create '%s' FAIL 0x%x "
				  "(sz=%u ptr=%p)\n",
				  slot->name, rc,
				  (unsigned)sizeof(TX_EVENT_FLAGS_GROUP),
				  (void *)&slot->flags);
			ef_trace(slot, 1);
			return NULL;
		}
	}

	if (kernel_state == K_READY) {
		/* Inside the Initialize..Start window: claim now, create in
		 * the define tail (ThreadX only allows object creation from
		 * tx_application_define or later). */
		slot->state = SLOT_PENDING;
	} else {
		UINT rc = slot_tx_create(slot);

		if (rc != TX_SUCCESS) {
			tx_event_flags_delete(&slot->flags);
			vPortFree(slot->stack);
			slot->stack = NULL;
			slot->state = SLOT_FREE;
			board_log("cmsis2: thread_create '%s' prio %u FAIL 0x%x\n",
				  slot->name, slot->tx_prio, rc);
			return NULL;
		}
		slot->thread.tx_thread_cmsis_slot = slot;
		slot->state = SLOT_LIVE;
	}

	return (osThreadId_t)slot;
}

/* Deferred creation replay: called from tx_application_define's tail. */
void cmsis2_tx_application_define(void)
{
	unsigned int i;

	for (i = 0U; i < (unsigned)CONFIG_TX_MAX_THREADS; i++) {
		if (slots[i].state == SLOT_PENDING) {
			if (slot_tx_create(&slots[i]) == TX_SUCCESS) {
				slots[i].thread.tx_thread_cmsis_slot =
					&slots[i];
				slots[i].state = SLOT_LIVE;
			} else {
				board_log("cmsis2: deferred create FAIL '%s'\n",
					  slots[i].name);
				slots[i].state = SLOT_FREE;
			}
		}
	}
}

osStatus_t osThreadTerminate(osThreadId_t thread_id)
{
	thread_slot_t *slot = (thread_slot_t *)thread_id;
	UINT rc;

	if (slot == NULL || slot->state != SLOT_LIVE) {
		return osErrorParameter;
	}

	rc = tx_thread_terminate(&slot->thread);
	if (rc != TX_SUCCESS) {
		return osErrorResource;
	}
	if (tx_thread_identify() == &slot->thread) {
		/* Self-terminate: never returns to the caller's stack, so it
		 * cannot be freed here. The wrapper's park loop owns the
		 * corpse; the slot stays LIVE. */
		return osOK;
	}
	tx_event_flags_delete(&slot->flags);
	vPortFree(slot->stack);
	slot->stack = NULL;
	slot->state = SLOT_FREE;
	return osOK;
}

osThreadId_t osThreadGetId(void)
{
	TX_THREAD *current = tx_thread_identify();

	if (current == NULL) {
		return NULL;
	}
	return (osThreadId_t)current->tx_thread_cmsis_slot;
}

/* --- read-only snapshot for the N7-stab fast probe (cmsis2_diag.h) -------- */

unsigned int cmsis2_thread_snapshot(cmsis2_thread_snap_t *out, unsigned int max)
{
	unsigned int i;
	unsigned int n = 0U;

	if (out == NULL) {
		return 0U;
	}
	for (i = 0U; i < (unsigned)CONFIG_TX_MAX_THREADS && n < max; i++) {
		thread_slot_t *slot = &slots[i];

		if (slot->state == SLOT_FREE) {
			continue;
		}
		out[n].name = slot->name;
		out[n].tx_prio = (unsigned int)slot->tx_prio;
		out[n].state = (unsigned int)slot->state;
		out[n].runs = (unsigned long)slot->thread.tx_thread_run_count;
		if (slot->state == SLOT_LIVE && slot->stack != NULL &&
		    slot->thread.tx_thread_stack_highest_ptr != NULL) {
			/* Stack area: start = LOW address, end = HIGH address;
			 * the stack grows down from end, so the high-water mark
			 * (lowest address touched) is measured from end. */
			out[n].stack_used = (unsigned long)
				((uintptr_t)slot->thread.tx_thread_stack_end -
				 (uintptr_t)slot->thread.tx_thread_stack_highest_ptr);
		} else {
			out[n].stack_used = 0UL;
		}
		out[n].stack_size = (unsigned long)slot->stack_size;
		n++;
	}
	return n;
}

const char *cmsis2_current_thread_name(void)
{
	TX_THREAD *t = tx_thread_identify();

	if (t == NULL) {
		return "none";
	}
	return (t->tx_thread_name != NULL) ? t->tx_thread_name : "?";
}

osStatus_t osThreadYield(void)
{
	tx_thread_relinquish();
	return osOK;
}

/* --- thread flags (cherrysh S4 RX 前置) ------------------------------------------ */

static TX_EVENT_FLAGS_GROUP *flags_of(osThreadId_t thread_id)
{
	thread_slot_t *slot = (thread_slot_t *)thread_id;

	if (slot == NULL || slot->state != SLOT_LIVE) {
		return NULL;
	}
	return &slot->flags;
}

uint32_t osThreadFlagsSet(osThreadId_t thread_id, uint32_t flags)
{
	TX_EVENT_FLAGS_GROUP *group = flags_of(thread_id);
	UINT rc;

	if (group == NULL) {
		return (uint32_t)osFlagsErrorParameter;
	}
	rc = tx_event_flags_set(group, (ULONG)flags, TX_OR);
	return rc == TX_SUCCESS ? flags : (uint32_t)osFlagsErrorUnknown;
}

uint32_t osThreadFlagsSetFromISR(osThreadId_t thread_id, uint32_t flags)
{
	/* tx_event_flags_set with TX_NO_WAIT is ISR-safe: no blocking path. */
	return osThreadFlagsSet(thread_id, flags);
}

uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout)
{
	TX_EVENT_FLAGS_GROUP *group = flags_of(osThreadGetId());
	ULONG actual = 0U;
	UINT get_option;
	UINT rc;

	if (group == NULL) {
		return (uint32_t)osFlagsErrorParameter;
	}
	if ((options & osFlagsNoClear) != 0U) {
		get_option = ((options & osFlagsWaitAll) != 0U) ? TX_AND :
								  TX_OR;
	} else {
		get_option = ((options & osFlagsWaitAll) != 0U) ?
				     TX_AND_CLEAR :
				     TX_OR_CLEAR;
	}

	rc = tx_event_flags_get(group, (ULONG)flags, get_option, &actual,
				(ULONG)timeout);
	if (rc == TX_SUCCESS) {
		return (uint32_t)actual;
	}
	if (rc == TX_NO_EVENTS) {
		return (uint32_t)osFlagsErrorTimeout;
	}
	return (uint32_t)osFlagsErrorResource;
}

/* --- kernel lifecycle ------------------------------------------------------------ */

osStatus_t osKernelInitialize(void)
{
	if (kernel_state != K_INACTIVE) {
		return osError;
	}
	kernel_state = K_READY;
	return osOK;
}

osStatus_t osKernelStart(void)
{
	extern void tx_kernel_enter_spsel0(void);

	if (kernel_state != K_READY) {
		return osError;
	}
	/* Never returns: the SPSel hand-over stub enters the kernel
	 * (spec §3.2.3). */
	kernel_state = K_RUNNING;
	tx_kernel_enter_spsel0();
	return osOK;
}

osKernelState_t osKernelGetState(void)
{
	switch (kernel_state) {
	case K_READY:
		return osKernelReady;
	case K_RUNNING:
		return osKernelRunning;
	default:
		return osKernelInactive;
	}
}

osStatus_t osKernelGetInfo(osVersion_t *version, char *id_buf,
			   uint32_t id_buf_len)
{
	static const char id[] = CONFIG_CMSIS2_VERSION;
	uint32_t i;

	if (version != NULL) {
		version->api = 0x00020003U;	/* CMSIS-RTOS2 V2.3 */
		version->kernel = 0x06050102U;	/* ThreadX 6.5.1 */
	}
	if (id_buf != NULL && id_buf_len != 0U) {
		for (i = 0U; i < id_buf_len - 1U && id[i] != '\0'; i++) {
			id_buf[i] = id[i];
		}
		id_buf[i] = '\0';
	}
	return osOK;
}

/* --- time ------------------------------------------------------------------------ */

uint32_t osKernelGetTickCount(void)
{
	return (uint32_t)tx_time_get();
}

uint32_t osKernelGetTickFreq(void)
{
	return (uint32_t)CONFIG_TX_TICKS_PER_SECOND;
}

uint32_t osKernelGetSysTimerCount(void)
{
	uint32_t v;

	v = gtimer_count_get();
	return v;
}

uint32_t osKernelGetSysTimerFreq(void)
{
	return gtimer_freq_get();
}

/* --- locking (UP 语义 = 关中断临界区, spec §3.5.2) ---------------------------------- */

#define LOCK_NEST_MAX	8U

static uint32_t lock_depth;
static unsigned int lock_posture[LOCK_NEST_MAX];

/* Lock-state contract (cmsis_os2.h V2.3): return the PREVIOUS lock state,
 * 1 = locked / 0 = not locked, negative = error. The depth stack is what
 * makes nesting honest: only the outermost transition touches DAIF. */
int32_t osKernelLock(void)
{
	int32_t prev;

	if (lock_depth >= LOCK_NEST_MAX) {
		return (int32_t)osErrorResource;
	}
	prev = (lock_depth == 0U) ? 0 : 1;
	lock_posture[lock_depth] = _tx_thread_smp_protect();
	lock_depth++;
	return prev;
}

int32_t osKernelUnlock(void)
{
	int32_t prev;

	if (lock_depth == 0U) {
		return 0;
	}
	prev = (lock_depth == 1U) ? 0 : 1;
	lock_depth--;
	_tx_thread_smp_unprotect(lock_posture[lock_depth]);
	return prev;
}

int32_t osKernelRestoreLock(int32_t lock)
{
	while (lock_depth != 0U) {
		lock_depth--;
		_tx_thread_smp_unprotect(lock_posture[lock_depth]);
	}
	if (lock == 1) {
		lock_posture[lock_depth] = _tx_thread_smp_protect();
		lock_depth = 1U;
	}
	return (lock_depth != 0U) ? 1 : 0;
}

/* --- delay ------------------------------------------------------------------------ */

osStatus_t osDelay(uint32_t ticks)
{
	if (ticks == 0U) {
		return osOK;
	}
	return tx_thread_sleep((ULONG)ticks) == TX_SUCCESS ? osOK :
							     osError;
}

/* --- mutex (S6/linuxkpi 扩面, 2026-10-01: 首个消费方 linuxkpi 扩入; ------------------
 * [S7a] §3.3.3 同族先例 - "IPC 类本轮不提供"按消费者逐件扩入). The control
 * block comes off the tlsf heap (the thread-slot pool's allocator); priority
 * inheritance is ThreadX TX_INHERIT, matching what a passive-level Linux
 * mutex wants. */

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
	TX_MUTEX *mutex = pvPortMalloc(sizeof(TX_MUTEX));

	if (mutex == NULL) {
		return NULL;
	}
	/* Priority inheritance always on (TX_INHERIT) - what a passive-level
	 * Linux mutex wants; recursive acquisition is ThreadX-native (the
	 * osMutexRecursive attr bit needs no switch here). */
	if (tx_mutex_create(mutex, (CHAR *)(uintptr_t)((attr != NULL &&
				    attr->name != NULL) ?
					   attr->name :
					   "cmsis_mutex"),
			    TX_INHERIT) != TX_SUCCESS) {
		vPortFree(mutex);
		return NULL;
	}
	return (osMutexId_t)mutex;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout)
{
	TX_MUTEX *mutex = (TX_MUTEX *)mutex_id;

	if ((mutex == NULL) || (mutex->tx_mutex_id != TX_MUTEX_ID)) {
		return osErrorParameter;
	}
	if (tx_thread_identify() == NULL) {
		/* no thread context: a non-blocking acquire is the only
		 * legal shape */
		if (timeout != 0U) {
			return osErrorParameter;
		}
		return tx_mutex_get(mutex, TX_NO_WAIT) == TX_SUCCESS ?
				       osOK :
				       osErrorResource;
	}
	return tx_mutex_get(mutex, (ULONG)timeout) == TX_SUCCESS ? osOK :
								   osErrorResource;
}

osStatus_t osMutexRelease(osMutexId_t mutex_id)
{
	TX_MUTEX *mutex = (TX_MUTEX *)mutex_id;

	if ((mutex == NULL) || (mutex->tx_mutex_id != TX_MUTEX_ID)) {
		return osErrorParameter;
	}
	return tx_mutex_put(mutex) == TX_SUCCESS ? osOK : osErrorResource;
}

osStatus_t osMutexDelete(osMutexId_t mutex_id)
{
	TX_MUTEX *mutex = (TX_MUTEX *)mutex_id;

	if ((mutex == NULL) || (mutex->tx_mutex_id != TX_MUTEX_ID)) {
		return osErrorParameter;
	}
	if (tx_mutex_delete(mutex) != TX_SUCCESS) {
		return osErrorResource;
	}
	vPortFree(mutex);
	return osOK;
}

/* --- semaphores (S7/P7-2a 扩面：首个消费方 = lwip sys_arch，S3 §3.5.2 义务) ---- */
/* TX semaphore has no max-count: CMSIS max_count is accepted and ignored -
 * lwip's sys_sem_new deliberately asks for an unbounded ceiling, which is
 * exactly what TX gives. tx_semaphore_put is ISR-callable (sys_sem_signal
 * posts from driver ISRs). */

osSemaphoreId_t osSemaphoreNew(uint32_t max_count, uint32_t initial_count,
			       const osSemaphoreAttr_t *attr)
{
	TX_SEMAPHORE *sem = pvPortMalloc(sizeof(TX_SEMAPHORE));

	(void)max_count;
	(void)attr;
	if (sem == NULL) {
		return NULL;
	}
	if (tx_semaphore_create(sem, (CHAR *)"cmsis-sem",
				(ULONG)initial_count) != TX_SUCCESS) {
		vPortFree(sem);
		return NULL;
	}
	return (osSemaphoreId_t)sem;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout)
{
	TX_SEMAPHORE *sem = (TX_SEMAPHORE *)semaphore_id;
	UINT wait;
	UINT ret;

	if ((sem == NULL) || (sem->tx_semaphore_id != TX_SEMAPHORE_ID)) {
		return osErrorParameter;
	}
	/* CMSIS osWaitForever == TX_WAIT_FOREVER == 0xffffffff. */
	wait = (timeout == 0U) ? TX_NO_WAIT : (UINT)timeout;
	ret = tx_semaphore_get(sem, wait);

	return (ret == TX_SUCCESS) ? osOK :
	       (wait == TX_NO_WAIT) ? osErrorResource : osErrorTimeout;
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id)
{
	TX_SEMAPHORE *sem = (TX_SEMAPHORE *)semaphore_id;

	if ((sem == NULL) || (sem->tx_semaphore_id != TX_SEMAPHORE_ID)) {
		return osErrorParameter;
	}
	return (tx_semaphore_put(sem) == TX_SUCCESS) ? osOK : osErrorResource;
}

osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id)
{
	TX_SEMAPHORE *sem = (TX_SEMAPHORE *)semaphore_id;

	if ((sem == NULL) || (sem->tx_semaphore_id != TX_SEMAPHORE_ID)) {
		return osErrorParameter;
	}
	if (tx_semaphore_delete(sem) != TX_SUCCESS) {
		return osErrorResource;
	}
	vPortFree(sem);
	return osOK;
}

uint32_t osSemaphoreGetCount(osSemaphoreId_t semaphore_id)
{
	TX_SEMAPHORE *sem = (TX_SEMAPHORE *)semaphore_id;

	if ((sem == NULL) || (sem->tx_semaphore_id != TX_SEMAPHORE_ID)) {
		return 0U;
	}
	return (uint32_t)sem->tx_semaphore_count;
}

/* --- message queues (S7/P7-2a 扩面：lwip sys_mbox = 指针邮箱) ------------------ */
/* TX queue message size is fixed at creation and counted in 32-bit words
 * (1..16): bytes are rounded up to words and anything beyond 64 is refused
 * rather than silently truncated. prio is ignored (TX queues are FIFO);
 * tx_queue_send with TX_NO_WAIT is ISR-callable (sys_mbox_trypost_fromisr). */

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size,
				     const osMessageQueueAttr_t *attr)
{
	TX_QUEUE *queue;
	void *storage;
	ULONG words;
	UINT q_bytes;

	(void)attr;
	if ((msg_count == 0U) || (msg_size == 0U)) {
		return NULL;
	}
	words = (msg_size + 3U) / 4U;
	if (words > 16U) {
		return NULL;
	}
	q_bytes = sizeof(TX_QUEUE) + msg_count * words * sizeof(ULONG);
	queue = pvPortMalloc(q_bytes);
	if (queue == NULL) {
		return NULL;
	}
	storage = (void *)((uintptr_t)queue + sizeof(TX_QUEUE));
	if (tx_queue_create(queue, (CHAR *)"cmsis-q", (UINT)words, storage,
			    msg_count * words * sizeof(ULONG)) != TX_SUCCESS) {
		vPortFree(queue);
		return NULL;
	}
	return (osMessageQueueId_t)queue;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr,
			     uint8_t msg_prio, uint32_t timeout)
{
	TX_QUEUE *queue = (TX_QUEUE *)mq_id;
	UINT wait;
	UINT ret;

	(void)msg_prio;
	if ((queue == NULL) || (queue->tx_queue_id != TX_QUEUE_ID) ||
	    (msg_ptr == NULL)) {
		return osErrorParameter;
	}
	wait = (timeout == 0U) ? TX_NO_WAIT : (UINT)timeout;
	ret = tx_queue_send(queue, (void *)msg_ptr, wait);

	return (ret == TX_SUCCESS) ? osOK :
	       (wait == TX_NO_WAIT) ? osErrorResource : osErrorTimeout;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr,
			     uint8_t *msg_prio, uint32_t timeout)
{
	TX_QUEUE *queue = (TX_QUEUE *)mq_id;
	UINT wait;
	UINT ret;

	(void)msg_prio;
	if ((queue == NULL) || (queue->tx_queue_id != TX_QUEUE_ID) ||
	    (msg_ptr == NULL)) {
		return osErrorParameter;
	}
	wait = (timeout == 0U) ? TX_NO_WAIT : (UINT)timeout;
	ret = tx_queue_receive(queue, msg_ptr, wait);

	return (ret == TX_SUCCESS) ? osOK :
	       (wait == TX_NO_WAIT) ? osErrorResource : osErrorTimeout;
}

uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id)
{
	TX_QUEUE *queue = (TX_QUEUE *)mq_id;

	if ((queue == NULL) || (queue->tx_queue_id != TX_QUEUE_ID)) {
		return 0U;
	}
	return (uint32_t)queue->tx_queue_enqueued;
}

uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id)
{
	TX_QUEUE *queue = (TX_QUEUE *)mq_id;

	if ((queue == NULL) || (queue->tx_queue_id != TX_QUEUE_ID)) {
		return 0U;
	}
	return (uint32_t)queue->tx_queue_available_storage;
}

osStatus_t osMessageQueueDelete(osMessageQueueId_t mq_id)
{
	TX_QUEUE *queue = (TX_QUEUE *)mq_id;

	if ((queue == NULL) || (queue->tx_queue_id != TX_QUEUE_ID)) {
		return osErrorParameter;
	}
	if (tx_queue_delete(queue) != TX_SUCCESS) {
		return osErrorResource;
	}
	vPortFree(queue);
	return osOK;
}

/* --- caller-storage primitives (S6 de-ThreadX: linuxkpi completion/callout
 *     shells embed their wake channel / timer by value) ------------------- */

#include "cmsis_os2_ext.h"

/* the ext shells are OS-neutral word arrays; the sizes must hold this
 * port's objects - drift is a compile error here, not a boot corruption */
_Static_assert(sizeof(((os_sem_static_t *)0)->priv) >= sizeof(TX_SEMAPHORE),
    "os_sem_static_t must hold TX_SEMAPHORE");
_Static_assert(sizeof(((os_timer_static_t *)0)->priv) >= sizeof(TX_TIMER),
    "os_timer_static_t priv must hold TX_TIMER");

osStatus_t osSemStaticInit(os_sem_static_t *s, const char *name,
			   uint32_t initial_count)
{
	TX_SEMAPHORE *sem = (TX_SEMAPHORE *)(void *)s->priv;

	return tx_semaphore_create(sem, (CHAR *)name,
				   (ULONG)initial_count) == TX_SUCCESS ?
		       osOK :
		       osError;
}

osStatus_t osSemStaticDestroy(os_sem_static_t *s)
{
	TX_SEMAPHORE *sem = (TX_SEMAPHORE *)(void *)s->priv;

	if ((s == NULL) || (sem->tx_semaphore_id != TX_SEMAPHORE_ID)) {
		return osErrorParameter;
	}
	return tx_semaphore_delete(sem) == TX_SUCCESS ? osOK : osErrorResource;
}

osStatus_t osSemStaticAcquire(os_sem_static_t *s, uint32_t timeout)
{
	TX_SEMAPHORE *sem = (TX_SEMAPHORE *)(void *)s->priv;
	UINT wait;
	UINT ret;

	if ((s == NULL) || (sem->tx_semaphore_id != TX_SEMAPHORE_ID)) {
		return osErrorParameter;
	}
	/* CMSIS osWaitForever == TX_WAIT_FOREVER == 0xffffffff. */
	wait = (timeout == 0U) ? TX_NO_WAIT : (UINT)timeout;
	ret = tx_semaphore_get(sem, wait);

	return (ret == TX_SUCCESS) ? osOK :
	       (wait == TX_NO_WAIT) ? osErrorResource :
				      osErrorTimeout;
}

osStatus_t osSemStaticRelease(os_sem_static_t *s)
{
	TX_SEMAPHORE *sem = (TX_SEMAPHORE *)(void *)s->priv;

	if ((s == NULL) || (sem->tx_semaphore_id != TX_SEMAPHORE_ID)) {
		return osErrorParameter;
	}
	return tx_semaphore_put(sem) == TX_SUCCESS ? osOK : osErrorResource;
}

static void os_timer_static_trampoline(ULONG input)
{
	os_timer_static_t *t = (os_timer_static_t *)(uintptr_t)input;

	t->fn(t->arg);
}

osStatus_t osTimerStaticCreate(os_timer_static_t *t, const char *name,
			       os_timer_static_fn_t fn, void *arg)
{
	TX_TIMER *timer = (TX_TIMER *)(void *)t->priv;

	t->fn = fn;
	t->arg = arg;
	if (tx_timer_create(timer, (CHAR *)name, os_timer_static_trampoline,
			    (ULONG)(uintptr_t)t, 1, 0,
			    TX_NO_ACTIVATE) != TX_SUCCESS) {
		return osError;
	}
	return osOK;
}

osStatus_t osTimerStaticStart(os_timer_static_t *t, uint32_t ticks)
{
	TX_TIMER *timer = (TX_TIMER *)(void *)t->priv;

	(void)tx_timer_deactivate(timer);
	if (tx_timer_change(timer,
			    ticks > 0 ? (ULONG)ticks : 1UL,
			    0) != TX_SUCCESS) {
		return osError;
	}
	return tx_timer_activate(timer) == TX_SUCCESS ? osOK : osError;
}

osStatus_t osTimerStaticStop(os_timer_static_t *t)
{
	TX_TIMER *timer = (TX_TIMER *)(void *)t->priv;

	return tx_timer_deactivate(timer) == TX_SUCCESS ? osOK : osError;
}

/* --- interrupt critical section (S6 de-ThreadX: USB completion paths) -- */
/* the aarch64 UP port's interrupt-disable pair; TX_DISABLE/RESTORE expand
 * to exactly these, the ext type just carries the saved DAIF across the
 * section boundary. */

os_critical_state_t osCriticalEnter(void)
{
	return (os_critical_state_t)_tx_thread_interrupt_disable();
}

void osCriticalExit(os_critical_state_t state)
{
	_tx_thread_interrupt_restore((UINT)state);
}
