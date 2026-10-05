/*
 * @file   usb_osal_threadx.c
 * @brief  CherryUSB's OSAL over ThreadX, with the three integration
 *         adaptations the net_80211 line measured on this board.
 *
 * Upstream ships osal/usb_osal_threadx.c; this file is the adapter's copy
 * (upstream's beamed through patches/ would work too, but the pool and the
 * reaper ordering below are board lessons, not taste):
 *
 * 1. The byte pool is ours. Upstream externs an integrator-created
 *    usb_byte_pool; here it is a static .bss region and usb_osal_init()
 *    creates the pool over it. Same shape, no board header plumbing.
 *
 * 2. Priorities pass through raw. CherryUSB numbers 0 as most urgent and
 *    ThreadX agrees (0 = highest urgency), so unlike the FreeRTOS osal no
 *    inversion is needed. The PSC hub threads run at priority 0.
 *
 * 3. TX_TIMER_TICKS_PER_SECOND must say 1000: the tick glue arms the 1 kHz
 *    timer and usb_osal_msleep refuses to compile against any other claim
 *    (upstream added that #error; tx_user.h defines it).
 *
 * The reaper ordering is a board fix, not an upstream bug left open: the
 * reaper's first action is mq_recv on usb_osal_mq, and creating the thread
 * hands it the CPU immediately (priority 10 outranks nothing, but the
 * queue must exist before the thread runs at all). Upstream creates the
 * thread first; on this kernel that is a latent TX_PTR_ERROR.
 *
 * Allocation is the CherryUSB byte pool (usb_osal_malloc), NOT the system
 * heap: the USB stack's buffers are DMA targets and the pool's fixed
 * arena keeps them inside the DMA window the linker script defines.
 *
 * @date   27.09.2026
 * @author zhugengyu
 */

#include "usb_osal.h"
#include "usb_errno.h"
#include "usb_config.h"
#include "usb_util.h"
#include "usb_log.h"
#include "tx_api.h"

/* Sized for two hub threads (TCB + 64 KiB stack each), the reaper, the
 * EHCI descriptor pools and the core's per-device allocations. The
 * urtwn line adds its own worker threads on top (32 KiB stacks) - keep
 * the margin. */
#define USB_OSAL_POOL_BYTES	(1024u * 1024u)

/* ThreadX on this port types a thread entry as void (*)(ULONG) and a timer
 * handler as void (*)(ULONG), with ULONG = unsigned int (tx_port.h); the
 * CherryUSB contract is void (*)(void *) / void (*)(void *).  The casts
 * below are this kernel port's convention, not a shortcut: the value
 * crosses as the 32-bit entry-parameter field, and every argument this
 * OSAL passes is a pointer into the image's DMA-visible RAM (below
 * 0xc0000000 - the linker script's window), so it fits that field.  A
 * future heap or pool above 4 GiB would break it, which is why the
 * constraint is written down here rather than left to the cast. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"

static uint8_t usb_osal_pool_mem[USB_OSAL_POOL_BYTES]
	__attribute__((aligned(16)));
static TX_BYTE_POOL usb_byte_pool;
static uint8_t usb_osal_started;

usb_osal_mq_t usb_osal_mq;

usb_osal_thread_t usb_osal_thread_create(const char *name, uint32_t stack_size,
					 uint32_t prio, usb_thread_entry_t entry,
					 void *args)
{
	TX_THREAD *thread_ptr = TX_NULL;

	tx_byte_allocate(&usb_byte_pool, (VOID **)&thread_ptr,
			 USB_ALIGN_UP(sizeof(TX_THREAD), 4) + stack_size, TX_NO_WAIT);
	if (thread_ptr == TX_NULL) {
		USB_LOG_ERR("Create thread %s failed\r\n", name);
		return NULL;
	}

	tx_thread_create(thread_ptr, (CHAR *)name, (VOID(*)(ULONG))entry,
			 (uintptr_t)args,
			 ((CHAR *)thread_ptr + USB_ALIGN_UP(sizeof(TX_THREAD), 4)),
			 stack_size, prio, prio, TX_NO_TIME_SLICE, TX_AUTO_START);

	return (usb_osal_thread_t)thread_ptr;
}

void usb_osal_thread_delete(usb_osal_thread_t thread)
{
	if (thread == NULL) {
		thread = tx_thread_identify();

		usb_osal_mq_send(usb_osal_mq, (uintptr_t)thread);

		tx_thread_terminate(thread);
		return;
	}

	tx_thread_terminate(thread);
	tx_thread_delete(thread);
	tx_byte_release(thread);
}

void usb_osal_thread_schedule_other(void)
{
	TX_THREAD *current_thread = tx_thread_identify();
	UINT old_priority;

	tx_thread_priority_change(current_thread, TX_MAX_PRIORITIES - 1,
				  &old_priority);

	tx_thread_relinquish();

	tx_thread_priority_change(current_thread, old_priority, &old_priority);
}

usb_osal_sem_t usb_osal_sem_create(uint32_t initial_count)
{
	TX_SEMAPHORE *sem_ptr = TX_NULL;

	tx_byte_allocate(&usb_byte_pool, (VOID **)&sem_ptr,
			 sizeof(TX_SEMAPHORE), TX_NO_WAIT);
	if (sem_ptr == TX_NULL) {
		USB_LOG_ERR("Create semaphore failed\r\n");
		return NULL;
	}

	tx_semaphore_create(sem_ptr, "usbh_sem", initial_count);
	return (usb_osal_sem_t)sem_ptr;
}

void usb_osal_sem_delete(usb_osal_sem_t sem)
{
	tx_semaphore_delete((TX_SEMAPHORE *)sem);
	tx_byte_release(sem);
}

int usb_osal_sem_take(usb_osal_sem_t sem, uint32_t timeout)
{
	UINT ret;

	ret = tx_semaphore_get((TX_SEMAPHORE *)sem, timeout);
	if (ret == TX_SUCCESS) {
		return 0;
	}
	if ((ret == TX_WAIT_ABORTED) || (ret == TX_NO_INSTANCE)) {
		return -USB_ERR_TIMEOUT;
	}
	return -USB_ERR_INVAL;
}

int usb_osal_sem_give(usb_osal_sem_t sem)
{
	return (tx_semaphore_put((TX_SEMAPHORE *)sem) == TX_SUCCESS) ?
	       0 : -USB_ERR_INVAL;
}

void usb_osal_sem_reset(usb_osal_sem_t sem)
{
	(void)tx_semaphore_get((TX_SEMAPHORE *)sem, 0);
}

usb_osal_mutex_t usb_osal_mutex_create(void)
{
	TX_MUTEX *mutex_ptr = TX_NULL;

	tx_byte_allocate(&usb_byte_pool, (VOID **)&mutex_ptr,
			 sizeof(TX_MUTEX), TX_NO_WAIT);
	if (mutex_ptr == TX_NULL) {
		return NULL;
	}

	tx_mutex_create(mutex_ptr, "usbh_mutex", TX_INHERIT);
	return (usb_osal_mutex_t)mutex_ptr;
}

void usb_osal_mutex_delete(usb_osal_mutex_t mutex)
{
	tx_mutex_delete((TX_MUTEX *)mutex);
	tx_byte_release(mutex);
}

int usb_osal_mutex_take(usb_osal_mutex_t mutex)
{
	UINT ret;

	ret = tx_mutex_get((TX_MUTEX *)mutex, TX_WAIT_FOREVER);
	if (ret == TX_SUCCESS) {
		return 0;
	}
	if ((ret == TX_WAIT_ABORTED) || (ret == TX_NO_INSTANCE)) {
		return -USB_ERR_TIMEOUT;
	}
	return -USB_ERR_INVAL;
}

int usb_osal_mutex_give(usb_osal_mutex_t mutex)
{
	return (tx_mutex_put((TX_MUTEX *)mutex) == TX_SUCCESS) ?
	       0 : -USB_ERR_INVAL;
}

usb_osal_mq_t usb_osal_mq_create(uint32_t max_msgs)
{
	TX_QUEUE *queue_ptr = TX_NULL;

	tx_byte_allocate(&usb_byte_pool, (VOID **)&queue_ptr,
			 USB_ALIGN_UP(sizeof(TX_QUEUE), 4) +
			 sizeof(uintptr_t) * max_msgs, TX_NO_WAIT);
	if (queue_ptr == TX_NULL) {
		return NULL;
	}

	tx_queue_create(queue_ptr, "usbh_mq", sizeof(uintptr_t) / 4,
			(CHAR *)queue_ptr + USB_ALIGN_UP(sizeof(TX_QUEUE), 4),
			sizeof(uintptr_t) * max_msgs);
	return (usb_osal_mq_t)queue_ptr;
}

void usb_osal_mq_delete(usb_osal_mq_t mq)
{
	tx_queue_delete((TX_QUEUE *)mq);
	tx_byte_release(mq);
}

int usb_osal_mq_send(usb_osal_mq_t mq, uintptr_t addr)
{
	return (tx_queue_send((TX_QUEUE *)mq, &addr, TX_NO_WAIT) == TX_SUCCESS) ?
	       0 : -USB_ERR_INVAL;
}

int usb_osal_mq_recv(usb_osal_mq_t mq, uintptr_t *addr, uint32_t timeout)
{
	UINT ret;

	ret = tx_queue_receive((TX_QUEUE *)mq, addr, timeout);
	if (ret == TX_SUCCESS) {
		return 0;
	}
	if (ret == TX_QUEUE_EMPTY) {
		return -USB_ERR_TIMEOUT;
	}
	return -USB_ERR_INVAL;
}

/* ThreadX forbids suspension from its system timer thread - every blocking
 * call there fails fast with TX_WAIT_ERROR (0x4) - and the hub class runs
 * its interrupt-in submission from a timer callback, which on xHCI starts
 * with a blocking Configure Endpoint command (measured 2026-09-28: the
 * command "timed out" in 4 ms, ret=-USB_ERR_INVAL, st=0x4).  Defer the
 * callbacks onto this service thread, whose context may block; the
 * RT-Thread shape of this osal runs timers in a thread too. */
#define USB_TIMER_QUEUE_SLOTS	16U

static TX_QUEUE usb_timer_queue;
static ULONG usb_timer_queue_mem[USB_TIMER_QUEUE_SLOTS];

struct usb_timer_defer {
	usb_timer_handler_t handler;
	void *argument;
};

static void usb_timer_trampoline(ULONG arg)
{
	struct usb_timer_defer *defer = (struct usb_timer_defer *)(uintptr_t)arg;

	/* never block: a drop costs one poll cycle and the next expiry
	 * re-arms */
	(void)tx_queue_send(&usb_timer_queue, (VOID *)&defer, TX_NO_WAIT);
}

static void usb_timer_service(void *argument)
{
	struct usb_timer_defer *defer;

	(void)argument;
	for (;;) {
		if (tx_queue_receive(&usb_timer_queue, (VOID *)&defer,
				     TX_WAIT_FOREVER) == TX_SUCCESS) {
			defer->handler(defer->argument);
		}
	}
}

struct usb_osal_timer *usb_osal_timer_create(const char *name,
					     uint32_t timeout_ms,
					     usb_timer_handler_t handler,
					     void *argument, bool is_period)
{
	TX_TIMER *timer_ptr = TX_NULL;
	struct usb_osal_timer *timer;
	struct usb_timer_defer *defer;

	tx_byte_allocate(&usb_byte_pool, (VOID **)&timer,
			 sizeof(struct usb_osal_timer), TX_NO_WAIT);
	if (timer == NULL) {
		return NULL;
	}
	memset(timer, 0, sizeof(struct usb_osal_timer));

	tx_byte_allocate(&usb_byte_pool, (VOID **)&timer_ptr, sizeof(TX_TIMER),
			 TX_NO_WAIT);
	if (timer_ptr == TX_NULL) {
		tx_byte_release(timer);
		return NULL;
	}

	tx_byte_allocate(&usb_byte_pool, (VOID **)&defer, sizeof(*defer),
			 TX_NO_WAIT);
	if (defer == TX_NULL) {
		tx_byte_release(timer_ptr);
		tx_byte_release(timer);
		return NULL;
	}
	defer->handler = handler;
	defer->argument = argument;

	timer->timer = timer_ptr;
	timer->timeout_ms = timeout_ms;
	timer->is_period = is_period;
	if (tx_timer_create(timer_ptr, (CHAR *)name, usb_timer_trampoline,
			    (uintptr_t)defer,
			    1, is_period ? 1 : 0, TX_NO_ACTIVATE) != TX_SUCCESS) {
		tx_byte_release(defer);
		tx_byte_release(timer_ptr);
		tx_byte_release(timer);
		return NULL;
	}
	return timer;
}

void usb_osal_timer_delete(struct usb_osal_timer *timer)
{
	tx_timer_deactivate((TX_TIMER *)timer->timer);
	tx_timer_delete((TX_TIMER *)timer->timer);
	tx_byte_release(timer->timer);
	tx_byte_release(timer);
	/* the defer block is deliberately kept: a callback already queued
	 * behind a delete would run on freed memory, and one 8-byte block
	 * per hub across the line is noise next to that race */
}

void usb_osal_timer_start(struct usb_osal_timer *timer)
{
	if (tx_timer_change((TX_TIMER *)timer->timer, timer->timeout_ms,
			    timer->is_period ? timer->timeout_ms : 0) == TX_SUCCESS) {
		(void)tx_timer_activate((TX_TIMER *)timer->timer);
	}
}

void usb_osal_timer_stop(struct usb_osal_timer *timer)
{
	tx_timer_deactivate((TX_TIMER *)timer->timer);
}

size_t usb_osal_enter_critical_section(void)
{
	TX_INTERRUPT_SAVE_AREA

	TX_DISABLE

	return interrupt_save;
}

void usb_osal_leave_critical_section(size_t flag)
{
	TX_INTERRUPT_SAVE_AREA

	interrupt_save = flag;
	TX_RESTORE
}

void usb_osal_msleep(uint32_t delay)
{
#if TX_TIMER_TICKS_PER_SECOND != 1000
#error "TX_TIMER_TICKS_PER_SECOND must be 1000"
#endif
	tx_thread_sleep(delay);
}

void *usb_osal_malloc(size_t size)
{
	CHAR *pointer = TX_NULL;

	tx_byte_allocate(&usb_byte_pool, (VOID **)&pointer, size,
			 TX_WAIT_FOREVER);

	return pointer;
}

void usb_osal_free(void *ptr)
{
	tx_byte_release(ptr);
}

static void usb_osal_thread(CONFIG_USB_OSAL_THREAD_SET_ARGV)
{
	int ret;
	usb_osal_thread_t thread;

	(void)argument;

	while (1) {
		ret = usb_osal_mq_recv(usb_osal_mq, (uintptr_t *)&thread,
				       TX_WAIT_FOREVER);
		if (ret < 0) {
			continue;
		}
		tx_thread_delete(thread);
		tx_byte_release(thread);
	}
}

/* Creates the byte pool - over the caller's region when given one, over the
 * static region otherwise - then the self-delete reaper thread and its
 * request queue. Idempotent so a second caller is harmless.
 *
 * Order note: the queue before the thread (upstream has it the other way).
 * usb_osal_thread_create() starts the thread immediately, and the reaper's
 * first act is mq_recv on a queue that would still be NULL. */
void usb_osal_init(uint8_t *mem, uint32_t mem_size)
{
	usb_osal_thread_t thread;

	if (usb_osal_started != 0U) {
		return;
	}
	if (mem == NULL) {
		mem = usb_osal_pool_mem;
		mem_size = USB_OSAL_POOL_BYTES;
	}
	usb_osal_started = 1U;

	tx_byte_pool_create(&usb_byte_pool, "usb byte pool", mem, mem_size);

	/* the timer service queue before any timer can exist (hub class
	 * timers are created on the bus start path, after this init) */
	if (tx_queue_create(&usb_timer_queue, "usb_tmr_q", 1,
			    usb_timer_queue_mem,
			    USB_TIMER_QUEUE_SLOTS) != TX_SUCCESS) {
		USB_LOG_ERR("Create usb_timer_queue failed\r\n");
		return;
	}

	usb_osal_mq = usb_osal_mq_create(32);
	if (usb_osal_mq == NULL) {
		USB_LOG_ERR("Create usb_osal_mq failed\r\n");
		return;
	}

	thread = usb_osal_thread_create("usb_osal", 2048, 10, usb_osal_thread,
					NULL);
	if (thread == NULL) {
		USB_LOG_ERR("Create usb_osal_thread failed\r\n");
	}

	thread = usb_osal_thread_create("usb_tmr", 2048, 10, usb_timer_service,
					NULL);
	if (thread == NULL) {
		USB_LOG_ERR("Create usb_timer_service failed\r\n");
	}
}

#pragma GCC diagnostic pop
