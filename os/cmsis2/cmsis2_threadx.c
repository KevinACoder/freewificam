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

#include "cmsis_os2.h"

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
	 * on self (os_threadlib semantics); the wrapper parks instead - the
	 * slot stays LIVE and its stack stays owned. */
	for (;;) {
		__asm__ __volatile__("wfe");
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

	if (tx_event_flags_create(&slot->flags, slot->name) != TX_SUCCESS) {
		vPortFree(slot->stack);
		slot->stack = NULL;
		return NULL;
	}

	if (kernel_state == K_READY) {
		/* Inside the Initialize..Start window: claim now, create in
		 * the define tail (ThreadX only allows object creation from
		 * tx_application_define or later). */
		slot->state = SLOT_PENDING;
	} else {
		if (slot_tx_create(slot) != TX_SUCCESS) {
			tx_event_flags_delete(&slot->flags);
			vPortFree(slot->stack);
			slot->stack = NULL;
			slot->state = SLOT_FREE;
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

	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

uint32_t osKernelGetSysTimerFreq(void)
{
	uint32_t v;

	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
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
