/*
 * @file    sys_arch.c
 * @brief   The single CMSIS-RTOS2 sys_arch of the lwIP port.
 *
 * One adapter implements lwIP's OS face (lwip/sys.h) on the CMSIS-RTOS2 API
 * and serves every backend alike: ThreadX and FreeRTOS. This file names no
 * kernel header - the backend is selected by the build (os/cmsis2/cmsis2.h)
 * and linked in. Nothing else in the lwIP port differs between kernels.
 *
 * Contract notes (third-party/lwip/src/include/lwip/sys.h):
 *   - waits take milliseconds; timeout 0 means wait forever; a failed wait
 *     returns SYS_ARCH_TIMEOUT, a successful one returns the time waited;
 *   - tryfetch reports SYS_MBOX_EMPTY rather than a timeout;
 *   - a mailbox message is one pointer, so the queue's message size is one
 *     pointer and no caller-visible padding question arises;
 *   - sys_thread_new must not fail: the CMSIS call is asserted by the
 *     backend's own configuration if its pools run out.
 *
 * Time unit: the interface speaks milliseconds, CMSIS-RTOS2 speaks ticks of
 * osKernelGetTickFreq(). Both directions convert through the tick frequency
 * with a rounded-up millisecond->tick step (a nonzero wait must never turn
 * into a zero-tick poll), and never at a hard-coded 1 kHz.
 *
 * Wait semantics: sys_arch_protect/sys_arch_unprotect are the scheduler-level
 * take/restore pair (osKernelLock/osKernelRestoreLock), which nests by
 * contract and excludes other tasks, not interrupts. Consequence fixed by the
 * port: lwIP core code is touched under the core lock or inside a protect
 * section, and the only ISR-side entry is sys_mbox_trypost_fromisr.
 *
 * @design ports & adapters: one adapter over the CMSIS-RTOS2 facade, not one
 * per kernel. Operation semantics re-derived from the upstream FreeRTOS port
 * (the same face it implemented):
 * third-party/lwip/contrib/ports/freertos/sys_arch.c:47-607 - the mutex,
 * semaphore and mailbox bodies, the wrapper types' validity probes and the
 * thread naming; the CMSIS mapping follows the board-proven shape of
 * ports/cherryusb/osal/cmsis2/osal_cmsis2.c:41-305 (band priorities, timeout
 * conversion, ISR-callable queue put - this project's single-CMSIS precedent).
 * Bytes are ours; the API surface is the one the imported code was written
 * against.
 */

#include <stddef.h>
#include <stdint.h>

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"

#include "sys_mbox_seam.h"

#include "os/cmsis2/cmsis2.h"

/* --- time ------------------------------------------------------------------- */

/* Ticks of the kernel's tick base to milliseconds. The interface consumes
 * these values as milliseconds; a zero frequency means the backend has not
 * settled its tick base yet, in which case the project's 1 kHz default makes
 * the two equal. */
static uint32_t sys_arch_ticks_to_ms(uint32_t ticks)
{
	uint32_t freq = osKernelGetTickFreq();

	if (freq == 0u) {
		return ticks;
	}
	return (uint32_t)(((uint64_t)ticks * 1000u) / (uint64_t)freq);
}

/* Milliseconds to ticks for a nonzero timeout: rounds up so a finite wait
 * never degrades into a poll, and stays strictly below osWaitForever so a
 * finite timeout can never be mistaken for "wait forever". */
static uint32_t sys_arch_ticks_of(uint32_t timeout_ms)
{
	uint32_t freq = osKernelGetTickFreq();
	uint64_t ticks;

	if (freq == 0u) {
		return timeout_ms;
	}
	ticks = ((uint64_t)timeout_ms * (uint64_t)freq + 999u) / 1000u;
	if (ticks == 0u) {
		ticks = 1u;
	}
	if (ticks > (uint64_t)osWaitForever - 1u) {
		ticks = (uint64_t)osWaitForever - 1u;
	}
	return (uint32_t)ticks;
}

void sys_init(void)
{
	/* The tick base is up before the stack starts (the board's OS bring-up
	 * owns it); there is nothing to seed here. */
}

u32_t sys_now(void)
{
	return (u32_t)sys_arch_ticks_to_ms(osKernelGetTickCount());
}

u32_t sys_jiffies(void)
{
	return (u32_t)osKernelGetTickCount();
}

void sys_arch_msleep(u32_t delay_ms)
{
	(void)osDelay(sys_arch_ticks_of(delay_ms));
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
	/* The ceiling is out of lwIP's post/take pairing's way: overflow is a
	 * programming error elsewhere, and a dropped signal here would stall a
	 * thread with no diagnostic. */
	sem->sem = osSemaphoreNew(0xFFFFu, (uint32_t)count, NULL);

	return (sem->sem != NULL) ? ERR_OK : ERR_MEM;
}

void sys_sem_signal(sys_sem_t *sem)
{
	(void)osSemaphoreRelease(sem->sem);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
	uint32_t start = osKernelGetTickCount();
	osStatus_t st;

	st = osSemaphoreAcquire(sem->sem,
				(timeout == 0u) ? osWaitForever
						: sys_arch_ticks_of(timeout));
	if (st != osOK) {
		return SYS_ARCH_TIMEOUT;
	}
	return (u32_t)sys_arch_ticks_to_ms(osKernelGetTickCount() - start);
}

void sys_sem_free(sys_sem_t *sem)
{
	(void)osSemaphoreDelete(sem->sem);
	sem->sem = NULL;
}

/* --- mailboxes (queues of one pointer) ---------------------------------------- */

/*
 * The mailbox traffic account (see sys_mbox_seam.h): the per-frame handoff
 * question - was this frame posted to the TCP/IP thread or run in place - is
 * answered by how many posts happen, and no lwIP stock counter records that
 * (the memp figure is a pool gauge from the caller's alloc, and the sys-stats
 * mbox fields were never wired). Counted at the mailbox entry points so every
 * caller is covered.
 */
static struct sys_mbox_seam sys_mbox_seam_state;

void
sys_mbox_seam_get(struct sys_mbox_seam *out)
{

	*out = sys_mbox_seam_state;
}

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
	/* May not fail: the contract says block until the message is posted. A
	 * queue here is deleted only with the stack quiesced, so a failure
	 * would be a programming error - looping beats silently dropping mail. */
	while (osMessageQueuePut(mbox->mbx, &msg, 0u, osWaitForever) != osOK) {
	}
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
	if (osMessageQueuePut(mbox->mbx, &msg, 0u, 0u) == osOK) {
		sys_mbox_seam_state.post_ok++;
		return ERR_OK;
	}
	sys_mbox_seam_state.post_full++;
	return ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
	/* osMessageQueuePut is callable from interrupt context on both backs;
	 * the IRQ-exit reschedule makes the upstream port's ERR_NEED_SCHED
	 * answer unnecessary, so the post simply succeeds or reports full. */
	return sys_mbox_trypost(mbox, msg);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
	uint32_t start = osKernelGetTickCount();
	void *got = NULL;
	osStatus_t st;

	st = osMessageQueueGet(mbox->mbx, &got, NULL,
			       (timeout == 0u) ? osWaitForever
					       : sys_arch_ticks_of(timeout));
	if (st != osOK) {
		return SYS_ARCH_TIMEOUT;
	}
	sys_mbox_seam_state.fetch_ok++;
	sys_mbox_seam_state.fetch_wait_ms +=
	    (unsigned long long)sys_arch_ticks_to_ms(osKernelGetTickCount() - start);
	if (msg != NULL) {
		*msg = got;
	}
	return (u32_t)sys_arch_ticks_to_ms(osKernelGetTickCount() - start);
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
	void *got = NULL;

	/* Genuinely non-blocking: a nonzero timeout here would sleep the tcpip
	 * thread on every poll. */
	if (osMessageQueueGet(mbox->mbx, &got, NULL, 0u) != osOK) {
		sys_mbox_seam_state.fetch_empty++;
		return SYS_MBOX_EMPTY;
	}
	sys_mbox_seam_state.fetch_ok++;
	if (msg != NULL) {
		*msg = got;
	}
	return 0u;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
	(void)osMessageQueueDelete(mbox->mbx);
	mbox->mbx = NULL;
}

/* --- threads -------------------------------------------------------------------- */

/* lwIP numbers threads in CMSIS bands: the configuration's small integers are
 * the band index, and the CMSIS priority a band maps to is band * 8 (the
 * numeric layout osPriorityLow=8 .. osPriorityHigh=40 fixes for us). The
 * translation keeps "tcpip outranks its feeders, the shell outranks tcpip"
 * a property of the configuration numbers alone. */
static osPriority_t sys_arch_prio_to_cmsis(int prio)
{
	int band = prio;

	if (band < 1) {
		band = 1;
	}
	if (band > 7) {
		band = 7;
	}
	return (osPriority_t)(band * 8);
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
			    int stacksize, int prio)
{
	osThreadAttr_t attr = { 0 };
	sys_thread_t t = { NULL };

	attr.name = name;
	attr.priority = sys_arch_prio_to_cmsis(prio);
	/* lwIP sizes are advisory; they are byte counts (the upstream port's
	 * stack-words option is not taken), floored at 2 KiB. */
	attr.stack_size = (stacksize > 0) ? (uint32_t)stacksize : 2048u;
	t.thread_handle = osThreadNew((osThreadFunc_t)thread, arg, &attr);

	return t;
}

/* --- critical sections ------------------------------------------------------------ */

#if SYS_LIGHTWEIGHT_PROT

sys_prot_t sys_arch_protect(void)
{
	/* Scheduler-level lock: other tasks cannot enter, interrupts are not
	 * masked. Nests by contract - the returned token is the previous state
	 * the matching call restores. */
	return (sys_prot_t)osKernelLock();
}

void sys_arch_unprotect(sys_prot_t pval)
{
	(void)osKernelRestoreLock((int32_t)pval);
}

#endif /* SYS_LIGHTWEIGHT_PROT */
