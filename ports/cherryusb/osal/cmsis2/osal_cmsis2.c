/*
 * @file
 * @brief The single CMSIS-RTOS2 osal of the CherryUSB host port.
 *
 * One adapter implements the kernel-service face the imported USB host stack expects
 * (third-party/cherryusb/common/usb_osal.h:43-75) on the CMSIS-RTOS2 API, and serves
 * every backend alike: ThreadX and FreeRTOS. This file names no kernel header - the
 * backend is selected by the build (os/cmsis2/cmsis2.h) and linked in.
 *
 * Service list: threads, semaphores (capped and counting), mutexes, message queues of
 * one pointer each, software timers, the critical-section pair, millisecond sleeps and
 * the allocation pair. Return values keep the stack's contract: zero on success,
 * -USB_ERR_TIMEOUT on a failed take, receive or send.
 *
 * Time unit: the interface speaks milliseconds, CMSIS-RTOS2 speaks ticks. Every
 * timeout crosses usb_osal_ticks_of(), which converts at the kernel's tick frequency,
 * rounds a nonzero millisecond value up, and keeps a finite timeout finite.
 *
 * ISR rule: the completion side of the host controller runs in the interrupt handler,
 * so give, send and the critical section are the ISR-safe CMSIS primitives. CMSIS-RTOS2
 * has no "am I in an interrupt" query, so a take or receive issued from interrupt
 * context must pass timeout 0: the conversion then produces the non-blocking form of
 * the acquire, which is the only form the interrupt path may use.
 *
 * @design ports & adapters: the adapter boundary is CMSIS-RTOS2, so the port does not
 * multiply per-kernel implementations. Operation semantics re-derived from the
 * component's own FreeRTOS osal (the same face it implemented):
 * third-party/cherryusb/osal/usb_osal_freertos.c:15-268 - counting shapes at :43-52,
 * the ISR split at :58-99, the timer wrapper at :167-231; file and annotation shape
 * from the single-CMSIS precedent: ports/net80211/osal/cmsis2/osal_cmsis2.c:1-26.
 * Bytes are ours, the API surface is the one the imported code was written against.
 */

#include "usb_osal.h"
#include "usb_errno.h"

#include "os/cmsis2/cmsis2.h"

/*
 * The project's allocation seam: the board heap provides pvPortMalloc/vPortFree on the
 * ThreadX line (os/threadx/heap.h) and the kernel's own heap on the FreeRTOS line.
 * usb_osal_malloc/free are the imported stack's only allocation point, so the
 * memory-subsystem milestone replaces the allocator behind this seam and nothing here
 * moves. Declared locally because the FreeRTOS-side declaration lives in a kernel
 * header this file may not include.
 */
void *pvPortMalloc(size_t size);
void vPortFree(void *ptr);

/* ------------------------------------------------------------------ */
/* Unit conversions                                                    */
/* ------------------------------------------------------------------ */

/* The CMSIS-RTOS2 thread priorities are bands from osPriorityLow (8) to osPriorityISR
 * (56), growing with the priority. The interface keeps the FreeRTOS ordering - a
 * smaller value means a higher priority (usb_osal.h:41-42) - and the reference inverts
 * its parameters into its kernel's range (usb_osal_freertos.c:19:
 * configMAX_PRIORITIES - 1 - prio). The same inversion against the top CMSIS band:
 *
 *     prio 0  -> osPriorityISR (highest)   prio 1 -> 55   ...   prio 55 -> osPriorityIdle
 *
 * Everything deeper clamps at osPriorityIdle, the lowest value both backends accept
 * (the ThreadX adapter rejects osPriorityNone; the FreeRTOS one rejects below Idle).
 */
static osPriority_t usb_osal_priority_of(uint32_t prio)
{
	uint32_t top = (uint32_t) osPriorityISR;

	if (prio >= top - (uint32_t) osPriorityIdle) {
		return osPriorityIdle;
	}
	return (osPriority_t) (top - prio);
}

/* Milliseconds of the interface to ticks of the kernel. A nonzero millisecond value
 * rounds up and can never become a zero-tick (non-blocking) call; osWaitForever stays
 * the CMSIS marker for "no timeout" and a finite timeout stays below it. */
static uint32_t usb_osal_ticks_of(uint32_t timeout_ms)
{
	uint64_t ticks;
	uint32_t freq;

	if (timeout_ms == USB_OSAL_WAITING_FOREVER) {
		return osWaitForever;
	}
	if (timeout_ms == 0u) {
		return 0u;
	}
	freq = osKernelGetTickFreq();
	if (freq == 0u) {
		/* The backend has not settled its tick base: the millisecond value is
		 * also the tick count, which is this project's 1 kHz default. */
		return timeout_ms;
	}
	ticks = ((uint64_t) timeout_ms * (uint64_t) freq + 999u) / 1000u;
	if (ticks > (uint64_t) osWaitForever - 1u) {
		ticks = (uint64_t) osWaitForever - 1u;
	}
	return (uint32_t) ticks;
}

/* ------------------------------------------------------------------ */
/* Threads                                                             */
/* ------------------------------------------------------------------ */

usb_osal_thread_t usb_osal_thread_create(const char *name, uint32_t stack_size,
					 uint32_t prio, usb_thread_entry_t entry, void *args)
{
	osThreadAttr_t attr;

	memset(&attr, 0, sizeof(attr));
	attr.name = name;
	attr.attr_bits = osThreadDetached;
	/* stack_size stays in bytes: CMSIS-RTOS2 takes bytes, so unlike the reference
	 * (which divides by sizeof(StackType_t) for its word-counting kernel) there is
	 * nothing to divide here. */
	attr.stack_size = stack_size;
	attr.priority = usb_osal_priority_of(prio);
	return (usb_osal_thread_t) osThreadNew((osThreadFunc_t) entry, args, &attr);
}

void usb_osal_thread_delete(usb_osal_thread_t thread)
{
	osThreadId_t id = (osThreadId_t) thread;

	/* A NULL handle means the calling thread deletes itself - the hub thread exits
	 * that way (third-party/cherryusb/class/hub/usbh_hub.c:703) - and both CMSIS
	 * backends reject a NULL id, so the caller is resolved through osThreadGetId(). */
	if (id == NULL) {
		id = osThreadGetId();
	}
	if (id != NULL) {
		(void) osThreadTerminate(id);
	}
}

void usb_osal_thread_schedule_other(void)
{
	(void) osThreadYield();
}

/* ------------------------------------------------------------------ */
/* Semaphores                                                          */
/* ------------------------------------------------------------------ */

usb_osal_sem_t usb_osal_sem_create(uint32_t initial_count)
{
	/* Counting semaphore capped at one, the reference's shape. A larger initial
	 * value cannot be represented by a one-token semaphore (the reference's kernel
	 * asserts on it, the CMSIS-FreeRTOS backend returns NULL), so it clamps. */
	if (initial_count > 1u) {
		initial_count = 1u;
	}
	return (usb_osal_sem_t) osSemaphoreNew(1u, initial_count, NULL);
}

usb_osal_sem_t usb_osal_sem_create_counting(uint32_t max_count)
{
	return (usb_osal_sem_t) osSemaphoreNew(max_count, 0u, NULL);
}

void usb_osal_sem_delete(usb_osal_sem_t sem)
{
	if (sem != NULL) {
		(void) osSemaphoreDelete((osSemaphoreId_t) sem);
	}
}

int usb_osal_sem_take(usb_osal_sem_t sem, uint32_t timeout)
{
	osStatus_t st;

	st = osSemaphoreAcquire((osSemaphoreId_t) sem, usb_osal_ticks_of(timeout));
	return (st == osOK) ? 0 : -USB_ERR_TIMEOUT;
}

int usb_osal_sem_give(usb_osal_sem_t sem)
{
	/* Interrupt-safe on both backends: the completion path of the stack gives the
	 * semaphore that releases a waiting transfer, and nothing here spins. */
	osStatus_t st = osSemaphoreRelease((osSemaphoreId_t) sem);

	return (st == osOK) ? 0 : -USB_ERR_TIMEOUT;
}

void usb_osal_sem_reset(usb_osal_sem_t sem)
{
	/* No reset call in CMSIS-RTOS2: drain with non-blocking acquires, which is the
	 * shape the port's mapping asks for. */
	if (sem != NULL) {
		while (osSemaphoreAcquire((osSemaphoreId_t) sem, 0u) == osOK) {
		}
	}
}

/* ------------------------------------------------------------------ */
/* Mutexes                                                             */
/* ------------------------------------------------------------------ */

/* Priority inheritance is on, which is what a bus-wide lock wants, and the CMSIS
 * attribute states it. The mutex stays non-recursive, mirroring the reference (which
 * asks its kernel for a plain mutex): a consumer that nests a take is a bug there too,
 * and the ThreadX backend's native mutex is recursive by nature, so the recursive flag
 * would not bind there anyway. */
static const osMutexAttr_t usb_osal_mutex_attr = {
	.name = "usbh_mutex",
	.attr_bits = osMutexPrioInherit,
};

usb_osal_mutex_t usb_osal_mutex_create(void)
{
	return (usb_osal_mutex_t) osMutexNew(&usb_osal_mutex_attr);
}

void usb_osal_mutex_delete(usb_osal_mutex_t mutex)
{
	if (mutex != NULL) {
		(void) osMutexDelete((osMutexId_t) mutex);
	}
}

int usb_osal_mutex_take(usb_osal_mutex_t mutex)
{
	osStatus_t st;

	st = osMutexAcquire((osMutexId_t) mutex, osWaitForever);
	return (st == osOK) ? 0 : -USB_ERR_TIMEOUT;
}

int usb_osal_mutex_give(usb_osal_mutex_t mutex)
{
	osStatus_t st = osMutexRelease((osMutexId_t) mutex);

	return (st == osOK) ? 0 : -USB_ERR_TIMEOUT;
}

/* ------------------------------------------------------------------ */
/* Message queues                                                      */
/* ------------------------------------------------------------------ */

usb_osal_mq_t usb_osal_mq_create(uint32_t max_msgs)
{
	return (usb_osal_mq_t) osMessageQueueNew(max_msgs, sizeof(uintptr_t), NULL);
}

void usb_osal_mq_delete(usb_osal_mq_t mq)
{
	if (mq != NULL) {
		(void) osMessageQueueDelete((osMessageQueueId_t) mq);
	}
}

int usb_osal_mq_send(usb_osal_mq_t mq, uintptr_t addr)
{
	/* Non-blocking send: this same call is the hub wakeup raised from the host
	 * controller's interrupt handler (third-party/cherryusb/class/hub/usbh_hub.c:708,
	 * reached from the port-change callback), where only a zero pause is legal, and
	 * CMSIS-RTOS2 offers no interrupt query to branch on. A full queue drops the
	 * event and reports a timeout, exactly as the reference's interrupt branch does;
	 * the waking side re-reads the port state, so a dropped wakeup coalesces. */
	osStatus_t st = osMessageQueuePut((osMessageQueueId_t) mq, &addr, 0u, 0u);

	return (st == osOK) ? 0 : -USB_ERR_TIMEOUT;
}

int usb_osal_mq_recv(usb_osal_mq_t mq, uintptr_t *addr, uint32_t timeout)
{
	osStatus_t st;

	st = osMessageQueueGet((osMessageQueueId_t) mq, addr, NULL, usb_osal_ticks_of(timeout));
	return (st == osOK) ? 0 : -USB_ERR_TIMEOUT;
}

/* ------------------------------------------------------------------ */
/* Software timers                                                     */
/* ------------------------------------------------------------------ */

/* Timer callbacks are deferred onto a service thread, never run on the kernel's
 * timer context: the consumer here is the hub class, whose expiry callback submits
 * a control transfer and therefore suspends, and a callback running on the timer
 * context cannot suspend (the kernel rejects the whole submit). The service thread
 * is an ordinary thread, so it can. A full queue drops one expiry - the timer
 * re-arms and the next expiry retries - so the trampoline never blocks.
 *
 * Shape and rationale from the carrier's port of the same stack
 * (its usb_osal_threadx.c deferral, which records the measured symptom: a
 * Configure Endpoint command that "timed out" in milliseconds with the
 * kernel's wait-error status). */
#define USB_OSAL_TIMER_QSLOTS 16U

struct usb_osal_timer_defer {
	struct usb_osal_timer *timer;
};

static osMessageQueueId_t usb_osal_timer_q;
static struct usb_osal_timer_defer usb_osal_timer_slots[USB_OSAL_TIMER_QSLOTS];
static uint32_t usb_osal_timer_slot_next;
static osThreadId_t usb_osal_timer_thread;
osThreadId_t usb_osal_timer_service_id(void);
static void usb_osal_timer_expired(void *argument);

static void usb_osal_timer_service(void *argument)
{
	struct usb_osal_timer_defer *slot;
	(void) argument;

	for (;;) {
		if (osMessageQueueGet(usb_osal_timer_q, &slot, NULL, osWaitForever) == osOK) {
			struct usb_osal_timer *timer = slot->timer;

			if (timer->handler != NULL) {
				timer->handler(timer->argument);
			}
		}
	}
}

static void usb_osal_timer_expired(void *argument)
{
	struct usb_osal_timer *timer = (struct usb_osal_timer *) argument;
	struct usb_osal_timer_defer *slot;
	uint32_t idx;

	if (usb_osal_timer_q == NULL) {
		return;
	}
	idx = usb_osal_timer_slot_next++ % USB_OSAL_TIMER_QSLOTS;
	slot = &usb_osal_timer_slots[idx];
	slot->timer = timer;
	(void) osMessageQueuePut(usb_osal_timer_q, &slot, 0, 0);
}

/* Called once by the board glue before the stack starts; idempotent. */
void usb_osal_timer_service_start(void)
{
	if (usb_osal_timer_q != NULL) {
		return;
	}
	usb_osal_timer_q = osMessageQueueNew(USB_OSAL_TIMER_QSLOTS, sizeof(void *), NULL);
	if (usb_osal_timer_q == NULL) {
		return;
	}
	usb_osal_timer_thread = osThreadNew(usb_osal_timer_service, NULL,
					    &(osThreadAttr_t){
						    .name = "usb_tmr",
						    .priority = osPriorityNormal,
						    .stack_size = 4096U,
					    });
	(void) usb_osal_timer_thread;
}

struct usb_osal_timer *usb_osal_timer_create(const char *name, uint32_t timeout_ms,
					     usb_timer_handler_t handler, void *argument,
					     bool is_period)
{
	struct usb_osal_timer *timer;
	osTimerId_t id;

	(void) name; /* the CMSIS timer needs no name; the interface still passes one */
	timer = (struct usb_osal_timer *) pvPortMalloc(sizeof(struct usb_osal_timer));
	if (timer == NULL) {
		return NULL;
	}
	memset(timer, 0, sizeof(struct usb_osal_timer));
	timer->handler = handler;
	timer->argument = argument;
	timer->is_period = is_period;
	timer->timeout_ms = timeout_ms;

	id = osTimerNew(usb_osal_timer_expired,
			is_period ? osTimerPeriodic : osTimerOnce,
			timer, NULL);
	if (id == NULL) {
		vPortFree(timer);
		return NULL;
	}
	timer->timer = (void *) id;
	return timer;
}

void usb_osal_timer_delete(struct usb_osal_timer *timer)
{
	if (timer == NULL) {
		return;
	}
	if (timer->timer != NULL) {
		(void) osTimerStop((osTimerId_t) timer->timer);
		(void) osTimerDelete((osTimerId_t) timer->timer);
	}
	vPortFree(timer);
}

void usb_osal_timer_start(struct usb_osal_timer *timer)
{
	/* The period becomes ticks here and not at create time: starting is the moment
	 * the kernel's tick base is meaningful, and the wrapper keeps the millisecond
	 * value of the interface. */
	(void) osTimerStart((osTimerId_t) timer->timer, usb_osal_ticks_of(timer->timeout_ms));
}

void usb_osal_timer_stop(struct usb_osal_timer *timer)
{
	/* A timer that is not running is stopped already: osErrorResource is the
	 * backend's answer then, and the interface has no return value for it. */
	(void) osTimerStop((osTimerId_t) timer->timer);
}

/* ------------------------------------------------------------------ */
/* Critical section                                                    */
/* ------------------------------------------------------------------ */

/* The opaque state of the CMSIS extension is widened into the size_t flag the
 * interface passes back; it is the same saved state, restored unchanged, and the pair
 * is legal from a completion context (cmsis_os2_ext.h:16-30). */
size_t usb_osal_enter_critical_section(void)
{
	return (size_t) osCriticalEnter();
}

void usb_osal_leave_critical_section(size_t flag)
{
	osCriticalExit((os_critical_state_t) flag);
}

/* ------------------------------------------------------------------ */
/* Sleep and allocation                                                */
/* ------------------------------------------------------------------ */

void usb_osal_msleep(uint32_t delay)
{
	(void) osDelay(usb_osal_ticks_of(delay));
}

void *usb_osal_malloc(size_t size)
{
	return pvPortMalloc(size);
}

void usb_osal_free(void *ptr)
{
	vPortFree(ptr);
}