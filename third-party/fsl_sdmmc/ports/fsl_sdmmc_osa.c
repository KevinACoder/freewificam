/*
 * @file   fsl_sdmmc_osa.c
 * @brief  OS abstraction the imported SDIO units call, implemented on the
 *         CMSIS-RTOS2 facade.
 *
 * @design adapter - ref upstream:third-party/fsl_sdmmc equivalent
 *         (osa/fsl_sdmmc_osa.c in the imported tree's origin), whose shape is a
 *         thin mapping of the SDMMC_OSA* API onto one OS's primitives. Here the
 *         primitives are the project facade's (os/cmsis2/cmsis2.h) rather than
 *         the SDK's OSA layer, so the port stays kernel-agnostic: the same file
 *         serves the ThreadX board build and the FreeRTOS host runtime.
 *
 * Semantics kept from upstream:
 *  - the event is a counting semaphore plus a flag word (the upstream
 *    SDMMC_OSA_POLLING_EVENT_BY_SEMPHORE form): a set ORs the flag and posts the
 *    semaphore; a wait takes the semaphore with a timeout and then reads the flag
 *    back, so a wakeup that does not satisfy the requested mask is not consumed
 *    as a success;
 *  - SDMMC_OSAMemoryAlignedAllocate() returns memory aligned to a caller-chosen
 *    power of two. The imported units stage block transfers through that buffer,
 *    so a word-aligned allocator is not enough: the alignment is met by
 *    over-allocating and keeping the raw pointer in the word in front, and
 *    SDMMC_OSAMemoryFree() unwinds it. Both allocate entry points take this path
 *    so free is uniform.
 */

/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 freewificam contributors */

#include <stdint.h>
#include <stddef.h>

#include "os/cmsis2/cmsis2.h"
#include "fsl_sdmmc_osa.h"
#include "sdmmc_lock_diag.h"

#if defined(__aarch64__)
#include "generic_timer.h"
#endif

/* --- microsecond spin ------------------------------------------------------- */

/* The system timer the facade exposes counts ticks, not microseconds, so a
 * microsecond wait has to spin on a finer source. On the board that is the
 * generic counter; a host runtime that does not carry one rounds the wait up to
 * a tick, and the divergence is recorded here rather than hidden. */
static void sdmmc_osa_spin_us(uint32_t microseconds)
{
#if defined(__aarch64__)
    uint64_t freq  = gtimer_freq_get();
    uint64_t delta = (((uint64_t)microseconds * freq) + 999999ULL) / 1000000ULL;
    uint64_t start = gtimer_count_get();

    while ((gtimer_count_get() - start) < delta)
    {
    }
#else
    (void)microseconds;
    (void)osDelay(1U);
#endif
}

/* --- lifecycle -------------------------------------------------------------- */

void SDMMC_OSAInit(void)
{
    /* Nothing to bring up: the facade's heap and the kernel are already running
     * when a card is initialised. Kept for API symmetry. */
}

void SDMMC_OSADeInit(void)
{
}

/* --- memory ----------------------------------------------------------------- */

void *SDMMC_OSAMemoryAllocate(uint32_t length)
{
    return SDMMC_OSAMemoryAlignedAllocate(length, sizeof(void *));
}

void *SDMMC_OSAMemoryAlignedAllocate(uint32_t length, uint32_t align)
{
    void *raw;
    uintptr_t base;
    uintptr_t aligned;

    if (length == 0U)
    {
        return NULL;
    }
    if (align < sizeof(void *))
    {
        align = sizeof(void *);
    }
    if ((align & (align - 1U)) != 0U)
    {
        /* Not a power of two: the alignment cannot be honoured. */
        return NULL;
    }

    raw = osMemoryAlloc((size_t)length + (size_t)align + sizeof(void *));
    if (raw == NULL)
    {
        return NULL;
    }

    base    = (uintptr_t)raw + sizeof(void *);
    aligned = (base + (uintptr_t)(align - 1U)) & ~(uintptr_t)(align - 1U);
    ((void **)aligned)[-1] = raw;

    return (void *)aligned;
}

void SDMMC_OSAMemoryFree(void *p)
{
    if (p != NULL)
    {
        osMemoryFree(((void **)p)[-1]);
    }
}

/* --- events ----------------------------------------------------------------- */

static uint32_t sdmmc_osa_ms_to_ticks(uint32_t milliseconds)
{
    if (milliseconds == osaWaitForever_c)
    {
        return osWaitForever;
    }

    return (uint32_t)(((uint64_t)milliseconds * osKernelGetTickFreq()) / 1000ULL);
}

status_t SDMMC_OSAEventCreate(void *eventHandle)
{
    sdmmc_osa_event_t *event = (sdmmc_osa_event_t *)eventHandle;

    if (event == NULL)
    {
        return kStatus_InvalidArgument;
    }

    event->eventFlag = 0U;
    event->handle[0] = (uintptr_t)osSemaphoreNew(1U, 0U, NULL);
    if (event->handle[0] == 0U)
    {
        return kStatus_Fail;
    }

    return kStatus_Success;
}

status_t SDMMC_OSAEventWait(void *eventHandle, uint32_t eventType, uint32_t timeoutMilliseconds, uint32_t *event,
                            uint32_t flags)
{
    sdmmc_osa_event_t *handle = (sdmmc_osa_event_t *)eventHandle;
    uint32_t ticks;

    if ((handle == NULL) || (event == NULL))
    {
        return kStatus_InvalidArgument;
    }

    ticks = sdmmc_osa_ms_to_ticks(timeoutMilliseconds);

    for (;;)
    {
        if (osSemaphoreAcquire((osSemaphoreId_t)handle->handle[0], ticks) != osOK)
        {
            return kStatus_Timeout;
        }

        if ((flags & SDMMC_OSA_EVENT_FLAG_AND) != 0U)
        {
            if ((handle->eventFlag & eventType) == eventType)
            {
                *event = handle->eventFlag;
                return kStatus_Success;
            }
        }
        else if ((handle->eventFlag & eventType) != 0U)
        {
            *event = handle->eventFlag;
            return kStatus_Success;
        }

        if (timeoutMilliseconds == 0U)
        {
            return kStatus_Timeout;
        }
        /* The posted wakeup did not satisfy the mask: keep waiting. A zero
         * timeout cannot be re-armed (it would spin), so it is reported. */
        if (timeoutMilliseconds != osaWaitForever_c)
        {
            return kStatus_Timeout;
        }
    }
}

status_t SDMMC_OSAEventSet(void *eventHandle, uint32_t eventType)
{
    sdmmc_osa_event_t *handle = (sdmmc_osa_event_t *)eventHandle;
    os_critical_state_t state;

    if (handle == NULL)
    {
        return kStatus_InvalidArgument;
    }

    state = osCriticalEnter();
    handle->eventFlag |= eventType;
    osCriticalExit(state);

    (void)osSemaphoreRelease((osSemaphoreId_t)handle->handle[0]);

    return kStatus_Success;
}

status_t SDMMC_OSAEventGet(void *eventHandle, uint32_t eventType, uint32_t *flag)
{
    sdmmc_osa_event_t *handle = (sdmmc_osa_event_t *)eventHandle;

    if ((handle == NULL) || (flag == NULL))
    {
        return kStatus_InvalidArgument;
    }

    *flag = handle->eventFlag & eventType;

    return kStatus_Success;
}

status_t SDMMC_OSAEventClear(void *eventHandle, uint32_t eventType)
{
    sdmmc_osa_event_t *handle = (sdmmc_osa_event_t *)eventHandle;
    os_critical_state_t state;

    if (handle == NULL)
    {
        return kStatus_InvalidArgument;
    }

    state = osCriticalEnter();
    handle->eventFlag &= ~eventType;
    osCriticalExit(state);

    return kStatus_Success;
}

status_t SDMMC_OSAEventDestroy(void *eventHandle)
{
    sdmmc_osa_event_t *handle = (sdmmc_osa_event_t *)eventHandle;

    if (handle == NULL)
    {
        return kStatus_InvalidArgument;
    }

    (void)osSemaphoreDelete((osSemaphoreId_t)handle->handle[0]);
    handle->handle[0] = 0U;

    return kStatus_Success;
}

/* --- mutexes ---------------------------------------------------------------- */

status_t SDMMC_OSAMutexCreate(void *mutexHandle)
{
    sdmmc_osa_mutex_t *mutex = (sdmmc_osa_mutex_t *)mutexHandle;

    if (mutex == NULL)
    {
        return kStatus_InvalidArgument;
    }

    mutex->handle[0] = (uintptr_t)osMutexNew(NULL);
    if (mutex->handle[0] == 0U)
    {
        return kStatus_Fail;
    }

    return kStatus_Success;
}

/* --- observation: who holds which OSA mutex, and for how long ------------ */
#define SDMMC_LOCK_SLOTS 4
struct sdmmc_lock_slot {
	void		*handle;
	void		*owner;
	uint32_t	since;
	uint32_t	waiters;
};
static struct sdmmc_lock_slot sdmmc_lock_slots[SDMMC_LOCK_SLOTS];

static struct sdmmc_lock_slot *sdmmc_lock_slot(void *handle)
{
	unsigned int i;
	struct sdmmc_lock_slot *free_slot = NULL;

	for (i = 0; i < SDMMC_LOCK_SLOTS; i++) {
		if (sdmmc_lock_slots[i].handle == handle)
			return &sdmmc_lock_slots[i];
		if (sdmmc_lock_slots[i].handle == NULL && free_slot == NULL)
			free_slot = &sdmmc_lock_slots[i];
	}
	if (free_slot != NULL)
		free_slot->handle = handle;
	return free_slot;
}

void sdmmc_osa_lock_diag_get(struct sdmmc_lock_diag *out)
{
	unsigned int i;
	struct sdmmc_lock_slot *best = NULL;
	uint32_t now;

	if (out == NULL)
		return;
	out->owner = "-";
	out->held_ms = 0u;
	out->waiters = 0u;
	out->valid = 0u;
	now = osKernelGetTickCount();
	for (i = 0; i < SDMMC_LOCK_SLOTS; i++) {
		struct sdmmc_lock_slot *s = &sdmmc_lock_slots[i];

		if (s->handle == NULL)
			continue;
		if (s->owner != NULL) {
			if (best == NULL || s->owner != NULL)
				best = s;
		}
		if (s->waiters != 0u)
			out->waiters += s->waiters;
	}
	if (best != NULL && best->owner != NULL) {
		const char *name = osThreadGetName((osThreadId_t)best->owner);

		out->owner = (name != NULL) ? name : "?";
		out->held_ms = (uint32_t)(now - best->since);
		out->valid = 1u;
	}
}

status_t SDMMC_OSAMutexLock(void *mutexHandle, uint32_t millisec)
{
    sdmmc_osa_mutex_t *mutex = (sdmmc_osa_mutex_t *)mutexHandle;

    if (mutex == NULL)
    {
        return kStatus_InvalidArgument;
    }

    {
        struct sdmmc_lock_slot *slot = sdmmc_lock_slot(mutexHandle);

        if (slot != NULL)
            slot->waiters++;
        if (osMutexAcquire((osMutexId_t)mutex->handle[0], sdmmc_osa_ms_to_ticks(millisec)) != osOK)
        {
            if (slot != NULL && slot->waiters != 0u)
                slot->waiters--;
            return kStatus_Timeout;
        }
        if (slot != NULL) {
            if (slot->waiters != 0u)
                slot->waiters--;
            slot->owner = osThreadGetId();
            slot->since = osKernelGetTickCount();
        }
    }

    return kStatus_Success;
}

status_t SDMMC_OSAMutexUnlock(void *mutexHandle)
{
    sdmmc_osa_mutex_t *mutex = (sdmmc_osa_mutex_t *)mutexHandle;

    if (mutex == NULL)
    {
        return kStatus_InvalidArgument;
    }

    {
        struct sdmmc_lock_slot *slot = sdmmc_lock_slot(mutexHandle);
        uint32_t rc = (osMutexRelease((osMutexId_t)mutex->handle[0]) == osOK) ?
            (uint32_t)kStatus_Success : (uint32_t)kStatus_Fail;

        if (slot != NULL && slot->owner == osThreadGetId()) {
            slot->owner = NULL;
            slot->since = 0u;
        }
        return (status_t)rc;
    }
}

status_t SDMMC_OSAMutexDestroy(void *mutexHandle)
{
    sdmmc_osa_mutex_t *mutex = (sdmmc_osa_mutex_t *)mutexHandle;

    if (mutex == NULL)
    {
        return kStatus_InvalidArgument;
    }

    (void)osMutexDelete((osMutexId_t)mutex->handle[0]);
    mutex->handle[0] = 0U;

    return kStatus_Success;
}

/* --- delay ------------------------------------------------------------------ */

void SDMMC_OSADelay(uint32_t milliseconds)
{
    if (milliseconds != 0U)
    {
        (void)osDelay(sdmmc_osa_ms_to_ticks(milliseconds));
    }
}

uint32_t SDMMC_OSADelayUs(uint32_t microseconds)
{
    sdmmc_osa_spin_us(microseconds);

    return microseconds;
}
