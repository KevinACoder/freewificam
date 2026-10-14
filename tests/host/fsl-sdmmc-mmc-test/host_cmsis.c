/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    host_cmsis.c
 * @brief   A minimal host CMSIS-RTOS2 backend, the project heap facade and the
 *          architectural counter the port's OS abstraction reaches for.
 *
 * The port's OS abstraction (ports/fsl_sdmmc/fsl_sdmmc_osa.c) is real code under
 * test here, so the kernel it rides on is stood in for rather than the
 * abstraction itself: mutexes are process-local and never contended in a
 * single-threaded run, the allocator is the C heap, and the counter is the C
 * library's clock. Same shape as the PCIe/NVMe suite's host backend.
 */

#include <stddef.h>
#include <stdlib.h>
#include <time.h>

#include "os/cmsis2/cmsis2.h"

struct host_mutex {
	int locked;
};

struct host_semaphore {
	unsigned int count;
};

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
	struct host_mutex *m;

	(void)attr;

	m = malloc(sizeof(*m));
	if (m != NULL) {
		m->locked = 0;
	}

	return (osMutexId_t)m;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout)
{
	struct host_mutex *m = (struct host_mutex *)mutex_id;

	(void)timeout;

	if (m == NULL) {
		return osErrorParameter;
	}
	m->locked = 1;

	return osOK;
}

osStatus_t osMutexRelease(osMutexId_t mutex_id)
{
	struct host_mutex *m = (struct host_mutex *)mutex_id;

	if (m == NULL) {
		return osErrorParameter;
	}
	m->locked = 0;

	return osOK;
}

osStatus_t osMutexDelete(osMutexId_t mutex_id)
{
	free((void *)mutex_id);

	return osOK;
}

osSemaphoreId_t osSemaphoreNew(uint32_t max_count, uint32_t initial_count,
			       const osSemaphoreAttr_t *attr)
{
	struct host_semaphore *s;

	(void)max_count;
	(void)attr;

	s = malloc(sizeof(*s));
	if (s != NULL) {
		s->count = initial_count;
	}

	return (osSemaphoreId_t)s;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout)
{
	struct host_semaphore *s = (struct host_semaphore *)semaphore_id;

	/* The host suite never contends: a wait with a timeout reports the
	 * timeout, which is what the abstraction's own wait checks. */
	if (s == NULL) {
		return osErrorParameter;
	}
	if (s->count > 0U) {
		s->count--;
		return osOK;
	}

	return (timeout == 0U) ? osErrorResource : osErrorTimeout;
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id)
{
	struct host_semaphore *s = (struct host_semaphore *)semaphore_id;

	if (s == NULL) {
		return osErrorParameter;
	}
	s->count++;

	return osOK;
}

osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id)
{
	free((void *)semaphore_id);

	return osOK;
}

osStatus_t osDelay(uint32_t ticks)
{
	(void)ticks;

	/* The suite runs single-threaded: a delay is a yield, and the waits under
	 * test are bounded by their own timeouts rather than by time passing. */
	return osOK;
}

uint32_t osKernelGetTickFreq(void)
{
	return 1000U;
}

uint32_t osKernelGetTickCount(void)
{
	clock_t c = clock();

	if (c == (clock_t)-1) {
		return 0U;
	}

	return (uint32_t)((uint64_t)c * 1000u / (uint64_t)CLOCKS_PER_SEC);
}

/* --- the project heap facade (cmsis_os2_ext.h) ----------------------------- */

void *osMemoryAlloc(size_t size)
{
	return malloc(size);
}

void osMemoryFree(void *ptr)
{
	free(ptr);
}

/* --- the critical-section extension ---------------------------------------- */

os_critical_state_t osCriticalEnter(void)
{
	return 0;
}

void osCriticalExit(os_critical_state_t state)
{
	(void)state;
}

/*
 * --- the architectural counter the OS abstraction spins on -----------------
 *
 * Standard C only (the CPU clock), so the suite needs no feature-test macros;
 * what the abstraction does with it is spin for a requested number of
 * microseconds, and a counter that always advances serves that the same way a
 * nanosecond counter would.
 */
uint32_t gtimer_freq_get(void)
{
	return (uint32_t)CLOCKS_PER_SEC;
}

uint64_t gtimer_count_get(void)
{
	clock_t c = clock();

	return (c == (clock_t)-1) ? 0U : (uint64_t)c;
}
