/*
 * @file   cmsis_os2_ext.h
 * @brief  Project extensions to the CMSIS-RTOS2 face (os/cmsis2 owns it).
 *
 * These are NOT CMSIS-RTOS2 standard symbols; the vendored cmsis_os2.h
 * (V2.3.0) must not grow them. Components that run partly in interrupt
 * context (the cherrysh RX chain, later the USB osal) declare their ISR
 * calls through this header so the extension stays visible at the call
 * sites that depend on it.
 */

#ifndef FREEWEBCAM_CMSIS_OS2_EXT_H
#define FREEWEBCAM_CMSIS_OS2_EXT_H

#include "cmsis_os2.h"

/* osThreadFlagsSet for interrupt context. The ThreadX twin routes this to
 * the same tx_thread_flags_set the task-context call uses (callable from
 * ISR on the UP kernel); the distinct name keeps the call sites honest
 * about the context they run in. Same return contract: the flags-after-
 * set value, or the CMSIS error bits. */
uint32_t osThreadFlagsSetFromISR(osThreadId_t thread_id, uint32_t flags);

/* --- caller-storage primitives (S6 de-ThreadX round) ------------------ */
/*
 * Freestanding Linux-kernel-style objects (completions, callouts) embed
 * their wake channel / timer by value, so the component needs primitives
 * whose control block lives in caller-provided storage instead of the
 * heap the standard osSemaphoreNew/osTimerNew allocate from. The shells
 * are plain word arrays: the OS port (os/cmsis2) asserts they hold its
 * object, components never include an OS header to know the size.
 */

/* 64B: holds the OS semaphore object (asserted in cmsis2_threadx.c). */
typedef struct os_sem_static {
	void	*priv[8];
} os_sem_static_t;

/* OS timer object storage + the expiry it runs (the OS timer callback
 * receives only the timer, so the shell carries fn/arg for it). */
typedef void (*os_timer_static_fn_t)(void *arg);
typedef struct os_timer_static {
	void			*priv[12];	/* >= OS timer object */
	os_timer_static_fn_t	fn;
	void			*arg;
} os_timer_static_t;

osStatus_t osSemStaticInit(os_sem_static_t *s, const char *name,
    uint32_t initial_count);
osStatus_t osSemStaticDestroy(os_sem_static_t *s);
/* timeout in ticks; 0 = try, osWaitForever = block */
osStatus_t osSemStaticAcquire(os_sem_static_t *s, uint32_t timeout);
osStatus_t osSemStaticRelease(os_sem_static_t *s);

/* created stopped and one-shot; (re)arm with osTimerStaticStart. */
osStatus_t osTimerStaticCreate(os_timer_static_t *t, const char *name,
    os_timer_static_fn_t fn, void *arg);
osStatus_t osTimerStaticStart(os_timer_static_t *t, uint32_t ticks);
osStatus_t osTimerStaticStop(os_timer_static_t *t);

/* Interrupt-level critical section (DAIF save/restore on aarch64).
 * CMSIS osKernelLock is a scheduler lock, not an interrupt lock - the
 * USB completion paths need the latter. Pair strictly. */
typedef unsigned long long os_critical_state_t;

os_critical_state_t osCriticalEnter(void);
void osCriticalExit(os_critical_state_t state);

#endif /* FREEWEBCAM_CMSIS_OS2_EXT_H */
