/*
 * @file   cmsis2_shim.c
 * @brief  CMSIS-RTOS2 kernel-info/time subset on the bare-metal base
 *         (no scheduler). SPEC v3 §3.7.2.
 *
 * What is implemented (bare-run semantics defined by the spec):
 *   osKernelGetInfo/GetState/GetTickCount/GetTickFreq/GetSysTimerCount/
 *   GetSysTimerFreq, osDelay (tick units), osKernelInitialize (zeroing),
 *   osKernelLock/Unlock/RestoreLock (documented no-ops).
 *
 * What is deliberately NOT provided: threads, signals, semaphores, mutexes,
 * event flags, message queues, timers, memory pools. The link fails on any
 * such reference ("better fewer than wrong" discipline, SPEC §2.2) - a
 * bare-metal main loop has no scheduler to back them.
 *
 * Layering: this shim includes only cmsis_os2.h (vendored interface) and
 * os/baremetal/tick.h (OS_Tick_* seam). The ThreadX round replaces this
 * file wholesale; app code carries over untouched (SPEC §3.10).
 */

#include <stdint.h>
#include <stddef.h>

#include "cmsis_os2.h"
#include "tick.h"
#include "generic_timer.h"
#include "arch_asm.h"

#ifndef CONFIG_CMSIS2_VERSION
#define CONFIG_CMSIS2_VERSION "baremetal"
#endif

/* Generic counter reads (same registers the tick driver uses; see
 * os/baremetal/tick.c for the CNTP/CNTV selection rationale). */
static inline uint64_t read_cntvct(void)
{
	uint64_t v;

	v = gtimer_count_get();
	return v;
}

static inline uint32_t read_cntfrq(void)
{
	return gtimer_freq_get();
}

static osStatus_t os_delay(uint32_t ticks)
{
	uint64_t target;

	if (ticks == 0U) {
		return osOK;
	}
	if (osKernelGetState() != osKernelRunning) {
		return osError;
	}
	target = os_tick_get_count64() + (uint64_t)ticks;
	while (os_tick_get_count64() < target) {
		/* WFI until the next tick interrupt (or anything else) wakes
		 * us; the tick ISR advances the count and returns, so the
		 * loop re-evaluates with interrupts enabled - no masked
		 * window beyond the register read and compare. */
		ARCH_WFI();
	}
	return osOK;
}

/* --- kernel info/state --------------------------------------------------- */

osStatus_t osKernelInitialize(void)
{
	/* Nothing to initialise on the bare base: the tick is already up
	 * (main() brought it in R2 order). Keep the documented semantics:
	 * a no-op that reports success. */
	return osOK;
}

osStatus_t osKernelStart(void)
{
	/* There is no scheduler to start; main() IS the scheduler. */
	return osError;
}

osKernelState_t osKernelGetState(void)
{
	/* The bare main loop is the running state by definition (spec
	 * choice, documented): before main's loop it would be Ready, but
	 * this shim is only ever called from within it. */
	return osKernelRunning;
}

osStatus_t osKernelGetInfo(osVersion_t *version, char *id_buf, uint32_t id_size)
{
	if (version != NULL) {
		/* api/arm/kernel in CMSIS 5.x terms. */
		version->api = 20030000U;	/* CMSIS-RTOS2 API 2.3.0 */
		version->kernel = 0U;		/* bare base, no kernel version */
	}
	if (id_buf != NULL && id_size > 0U) {
		const char *src = "freewebcam " CONFIG_CMSIS2_VERSION;
		uint32_t i = 0U;

		while (src[i] != '\0' && i + 1U < id_size) {
			id_buf[i] = src[i];
			i++;
		}
		id_buf[i] = '\0';
	}
	return osOK;
}

uint32_t osKernelGetTickCount(void)
{
	return os_tick_get_count32();
}

uint32_t osKernelGetTickFreq(void)
{
	return os_tick_freq_hz();
}

uint32_t osKernelGetSysTimerCount(void)
{
	return (uint32_t)read_cntvct();
}

uint32_t osKernelGetSysTimerFreq(void)
{
	return read_cntfrq();
}

int32_t osKernelLock(void)
{
	/* No scheduler to lock: the bare loop already owns the CPU. The
	 * documented contract is "no-op returning the previous lock
	 * state", and the state is permanently 0 (unlocked). */
	return 0;
}

int32_t osKernelUnlock(void)
{
	return 0;
}

int32_t osKernelRestoreLock(int32_t lock)
{
	(void)lock;
	return 0;
}

osStatus_t osDelay(uint32_t ticks)
{
	return os_delay(ticks);
}
