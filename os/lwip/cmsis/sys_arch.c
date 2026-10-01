/*
 * @file   sys_arch.c
 * @brief  lwIP's OS interface on CMSIS-RTOS2.
 *
 * The FreeRTOS build uses the contrib port verbatim
 * (third-party/lwip/contrib/ports/freertos/sys_arch.c); this file is the
 * ThreadX build's counterpart, written to the same contract over the os*
 * API instead - which both CMSIS twins implement. Nothing else in the lwIP
 * line changes between kernels: the core, ethernetif and the adapter are
 * kernel-free or already CMSIS-based.
 *
 * Contract notes (lwip/src/include/lwip/sys.h):
 *   - waits take milliseconds, 0 = wait forever; they return the time spent
 *     waiting, or SYS_ARCH_TIMEOUT;
 *   - tryfetch reports SYS_MBOX_EMPTY rather than a timeout;
 *   - messages are single pointers, so the queue's message size is one
 *     pointer and no caller-visible padding question arises.
 *
 * Priority translation: lwipopts.h numbers threads in CMSIS BANDS - the
 * FreeRTOS twin consumed them band-identically (band = prio, since the
 * FreeRTOS CMSIS layer maps osPriority to bands by /8). osPriority values
 * are band*8, so the mapping here is the same bands expressed natively:
 * 1 -> osPriorityLow, 3 -> osPriorityNormal, 4 -> osPriorityAboveNormal,
 * keeping "tcpip outranks its feeders, the shell outranks tcpip".
 *
 * sys_arch_protect nests as a scheduler-level lock on both kernels
 * (vTaskSuspendAll nesting there, the kernel-lock counter here) - the
 * returned token is the nesting depth in both cases.
 *
 * Port note (specs/lwip-adapt §4/P7-2a): carried over line-for-line from the
 * prior workspace's lwip line; the cmsis2_threadx shim gained osSemaphore
 * and osMessageQueue for this consumer (the S3 §3.5.2 扩面义务).
 *
 * @author zhugengyu
 * @date   22.09.2026 (prior-work) / 02.10.2026 (port)
 */

#include <stddef.h>

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"

#include "cmsis_os2.h"

/* --- time ------------------------------------------------------------------- */

void sys_init(void)
{
	/* Tick is armed before the kernel starts (tx_glue); there is
	 * nothing to seed. */
}

u32_t sys_now(void)
{
	return osKernelGetTickCount();
}

u32_t sys_jiffies(void)
{
	return osKernelGetTickCount();
}

void sys_arch_msleep(u32_t delay_ms)
{
	(void)osDelay(delay_ms);
}

/* --- mutexes ---------------------------------------------------------------- */

#if !LWIP_COMPAT_MUTEX

err_t sys_mutex_new(sys_mutex_t *mutex)
{
	osMutexAttr_t attr = { 0 };

	attr.attr_bits = osMutexPrioInherit;
	mutex->mut = osMutexNew(&attr);

	return (mutex->mut != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
	(void)osMutexAcquire(mutex->mut, osWaitForever);
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
	(void)osMutexRelease(mutex->mut);
}

void sys_mutex_free(sys_mutex_t *mutex)
{
	(void)osMutexDelete(mutex->mut);
	mutex->mut = NULL;
}

#endif /* !LWIP_COMPAT_MUTEX */

/* --- semaphores --------------------------------------------------------------- */

err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
	/* Unbounded ceiling: lwIP's post/take pairing makes overflow a
	 * programming error elsewhere, and a dropped signal here would stall
	 * a thread with no diagnostic. */
	sem->sem = osSemaphoreNew(0xFFFFU, (uint32_t)count, NULL);

	return (sem->sem != NULL) ? ERR_OK : ERR_MEM;
}

void sys_sem_signal(sys_sem_t *sem)
{
	(void)osSemaphoreRelease(sem->sem);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
	uint32_t start = osKernelGetTickCount();
	osStatus_t status;

	/* 0 = wait forever; a nonzero timeout must still report a waited time
	 * of at least... 0 is fine per contract ("with or without waiting"). */
	status = osSemaphoreAcquire(sem->sem,
				    (timeout == 0U) ? osWaitForever : timeout);
	if (status != osOK) {
		return SYS_ARCH_TIMEOUT;
	}
	return osKernelGetTickCount() - start;
}

void sys_sem_free(sys_sem_t *sem)
{
	(void)osSemaphoreDelete(sem->sem);
	sem->sem = NULL;
}

/* --- mailboxes (queues of one pointer) ---------------------------------------- */

err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
	if (size <= 0) {
		size = 1;
	}
	mbox->mbx = osMessageQueueNew((uint32_t)size, sizeof(void *), NULL);

	return (mbox->mbx != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
	while (osMessageQueuePut(mbox->mbx, &msg, 0U, osWaitForever) != osOK) {
		/* The queue is deleted only with the stack quiesced, so a
		 * failure here would be a programming error - but looping
		 * beats silently dropping mail. */
	}
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
	return (osMessageQueuePut(mbox->mbx, &msg, 0U, 0U) == osOK)
	       ? ERR_OK : ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
	/* osMessageQueuePut is ISR-callable on both kernels (tx_queue_send
	 * from ISR; the FreeRTOS twin's FromISR path), and the IRQ-exit
	 * reschedule makes ERR_NEED_SCHED unnecessary. */
	return sys_mbox_trypost(mbox, msg);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
	uint32_t start = osKernelGetTickCount();
	void *got = NULL;
	osStatus_t status;

	status = osMessageQueueGet(mbox->mbx, &got, NULL,
				   (timeout == 0U) ? osWaitForever : timeout);
	if (status != osOK) {
		return SYS_ARCH_TIMEOUT;
	}
	if (msg != NULL) {
		*msg = got;
	}
	return osKernelGetTickCount() - start;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
	void *got = NULL;

	/* Genuinely non-blocking: a 1 ms timeout here would sleep the tcpip
	 * thread every poll. */
	if (osMessageQueueGet(mbox->mbx, &got, NULL, 0U) != osOK) {
		return SYS_MBOX_EMPTY;
	}
	if (msg != NULL) {
		*msg = got;
	}
	return 0U;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
	(void)osMessageQueueDelete(mbox->mbx);
	mbox->mbx = NULL;
}

/* --- threads -------------------------------------------------------------------- */

static osPriority_t band_to_cmsis(int prio)
{
	int band = prio;

	if (band < 1) {
		band = 1;
	}
	if (band > 7) {
		band = 7;
	}
	/* osPriorityLow=8 .. osPriorityHigh=40: band*8, the numeric layout
	 * CMSIS-RTOS2 fixes for us. */
	return (osPriority_t)(band * 8);
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
			    int stacksize, int prio)
{
	osThreadAttr_t attr = { 0 };
	sys_thread_t t = { NULL };

	(void)name;
	attr.priority = band_to_cmsis(prio);
	/* lwIP sizes are advisory and generous stacks cost .bss on this
	 * image only through the heap - honor them, floored at 2 KiB. */
	attr.stack_size = (stacksize > 0) ? (uint32_t)stacksize : 2048U;
	attr.name = name;
	t.thread_handle = osThreadNew((osThreadFunc_t)thread, arg, &attr);

	return t;
}

/* --- critical sections ------------------------------------------------------------ */

#if SYS_LIGHTWEIGHT_PROT

sys_prot_t sys_arch_protect(void)
{
	return (sys_prot_t)osKernelLock();
}

void sys_arch_unprotect(sys_prot_t p)
{
	(void)osKernelRestoreLock((int32_t)p);
}

#endif /* SYS_LIGHTWEIGHT_PROT */
