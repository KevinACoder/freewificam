/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    host_cmsis.c
 * @brief   A minimal host CMSIS-RTOS2 backend for the storage suite.
 *
 * The storage stack's OS glue (ports/fatfs/fatfs_os.c) is written against
 * CMSIS-RTOS2 and the project's heap facade (osMemoryAlloc / osMemoryFree). The
 * RTOS is not what this suite tests, so it is stood in for here, exactly as the
 * NVMe controller is: the header is the real one (CMSIS_6's cmsis_os2.h, reached
 * through os/cmsis2/cmsis2.h), and only the handful of calls the glue makes are
 * implemented. The mutexes are process-local and never contended in a
 * single-threaded run; the allocator is the C heap.
 */

#include <stddef.h>
#include <stdlib.h>
#include <time.h>

#include "os/cmsis2/cmsis2.h"

/* The mutex object: a host-side record behind the opaque CMSIS handle. */
struct host_mutex {
	int locked;
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

/* A monotonic millisecond counter, for the timed paths. Standard C only (the
 * CPU clock), so the suite needs no feature-test macros; it is a fine proxy for
 * "the bounded wait did not spin". */
uint32_t osKernelGetTickCount(void)
{
	clock_t c = clock();

	if (c == (clock_t)-1) {
		return 0;
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
