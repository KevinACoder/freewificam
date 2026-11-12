/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    osa_lkpi.c
 * @brief   The NXP OSA surface over the FreeRTOS kernel this image carries.
 *
 * The imported FreeRTOS files of the NXP port (src/utils/os_freertos.c,
 * src/utils/eloop_freertos.c, freertos/src/supp_main.c) are written against
 * MCUXpresso's OSA.  This file is the whole backend: a handle slot holds the
 * kernel object pointer, timeouts are milliseconds, and the event group backs
 * OSA events.  A mutex is recursive because the port's callers nest the
 * supplicant lock inside the same task (hostapd's eloop timeout path).
 */
#include <stdint.h>
#include <stddef.h>

#include "fsl_os_abstraction.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#define OSA_MS_TO_TICKS(ms)						\
	(((ms) == osaWaitForever_c) ? portMAX_DELAY :			\
	 ((TickType_t)(((uint32_t)(ms) * (uint32_t)configTICK_RATE_HZ + 999u) / 1000u)))

static void **osa_slot(osa_task_handle_t handle)
{
	return (void **)handle;
}

osa_status_t
OSA_TaskCreate(osa_task_handle_t handle, const struct osa_task_def *def,
	       osa_task_param_t arg)
{
	TaskHandle_t task = NULL;
	BaseType_t rc;

	if (handle == NULL || def == NULL || def->entry == NULL)
		return KOSA_StatusError;
	if (def->instances > 1)
		return KOSA_StatusError;
	rc = xTaskCreate((TaskFunction_t)def->entry, "hostap",
			 (configSTACK_DEPTH_TYPE)(def->stack_size / sizeof(StackType_t)),
			 arg, (UBaseType_t)def->priority, &task);
	if (rc != pdPASS)
		return KOSA_StatusError;
	*osa_slot(handle) = (void *)task;
	return KOSA_StatusSuccess;
}

void
OSA_TaskYield(void)
{
	taskYIELD();
}

void
OSA_TimeDelay(uint32_t ms)
{
	vTaskDelay(OSA_MS_TO_TICKS(ms));
}

uint32_t
OSA_TimeGetMsec(void)
{
	return (uint32_t)(xTaskGetTickCount() * (TickType_t)portTICK_PERIOD_MS);
}

uint32_t
OSA_Rand(void)
{
	static uint32_t state = 0x2545f491u;
	uint32_t tick = (uint32_t)xTaskGetTickCount();

	state = state * 1664525u + 1013904223u + tick;
	return state;
}

void *
OSA_MemoryAllocate(uint32_t size)
{
	if (size == 0u)
		return NULL;
	return pvPortMalloc((size_t)size);
}

void
OSA_MemoryFree(void *ptr)
{
	if (ptr != NULL)
		vPortFree(ptr);
}

osa_status_t
OSA_MutexCreate(osa_mutex_handle_t handle)
{
	SemaphoreHandle_t m;

	if (handle == NULL)
		return KOSA_StatusError;
	m = xSemaphoreCreateRecursiveMutex();
	if (m == NULL)
		return KOSA_StatusError;
	*osa_slot(handle) = (void *)m;
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_MutexDestroy(osa_mutex_handle_t handle)
{
	void **slot = osa_slot(handle);

	if (slot == NULL || *slot == NULL)
		return KOSA_StatusError;
	vSemaphoreDelete((SemaphoreHandle_t)*slot);
	*slot = NULL;
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_MutexLock(osa_mutex_handle_t handle, uint32_t timeout_ms)
{
	void **slot = osa_slot(handle);

	if (slot == NULL || *slot == NULL)
		return KOSA_StatusError;
	if (xSemaphoreTakeRecursive((SemaphoreHandle_t)*slot,
				    OSA_MS_TO_TICKS(timeout_ms)) != pdTRUE)
		return KOSA_StatusTimeout;
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_MutexUnlock(osa_mutex_handle_t handle)
{
	void **slot = osa_slot(handle);

	if (slot == NULL || *slot == NULL)
		return KOSA_StatusError;
	if (xSemaphoreGiveRecursive((SemaphoreHandle_t)*slot) != pdTRUE)
		return KOSA_StatusError;
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_SemaphoreCreateBinary(osa_semaphore_handle_t handle)
{
	SemaphoreHandle_t s;

	if (handle == NULL)
		return KOSA_StatusError;
	s = xSemaphoreCreateBinary();
	if (s == NULL)
		return KOSA_StatusError;
	*osa_slot(handle) = (void *)s;
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_SemaphoreDestroy(osa_semaphore_handle_t handle)
{
	void **slot = osa_slot(handle);

	if (slot == NULL || *slot == NULL)
		return KOSA_StatusError;
	vSemaphoreDelete((SemaphoreHandle_t)*slot);
	*slot = NULL;
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_SemaphorePost(osa_semaphore_handle_t handle)
{
	void **slot = osa_slot(handle);
	BaseType_t rc;

	if (slot == NULL || *slot == NULL)
		return KOSA_StatusError;
	rc = xSemaphoreGive((SemaphoreHandle_t)*slot);
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_SemaphoreWait(osa_semaphore_handle_t handle, uint32_t timeout_ms)
{
	void **slot = osa_slot(handle);
	BaseType_t rc;

	if (slot == NULL || *slot == NULL)
		return KOSA_StatusError;
	rc = xSemaphoreTake((SemaphoreHandle_t)*slot,
			    OSA_MS_TO_TICKS(timeout_ms));
	if (rc != pdTRUE)
		return KOSA_StatusTimeout;
	return KOSA_StatusSuccess;
}

/*
 * OSA events are FreeRTOS event groups.  The FSL API splits one concern our
 * first cut merged: the third argument of OSA_EventWait() is "wait for all
 * bits", NOT a per-call auto-clear; clearing is an instance property fixed at
 * OSA_EventCreate().  The NXP eloop waits with wait-all=false and relies on the
 * create-time auto-clear to consume the dummy wake-up bit, so keeping a tiny
 * handle table for the flag is what makes its loop block instead of spin.
 */
struct osa_event_slot {
	EventGroupHandle_t	group;
	uint8_t			auto_clear;
};

#define OSA_EVENT_MAX	8
static struct osa_event_slot osa_events[OSA_EVENT_MAX];

static struct osa_event_slot *
osa_event_of(EventGroupHandle_t g)
{
	unsigned int i;

	for (i = 0; i < OSA_EVENT_MAX; i++)
		if (osa_events[i].group == g)
			return &osa_events[i];
	return NULL;
}

osa_status_t
OSA_EventCreate(osa_event_handle_t handle, uint8_t auto_clear)
{
	struct osa_event_slot *slot;
	EventGroupHandle_t g;
	unsigned int i;

	if (handle == NULL)
		return KOSA_StatusError;
	g = xEventGroupCreate();
	if (g == NULL)
		return KOSA_StatusError;
	slot = NULL;
	for (i = 0; i < OSA_EVENT_MAX; i++) {
		if (osa_events[i].group == NULL) {
			slot = &osa_events[i];
			break;
		}
	}
	if (slot == NULL) {
		vEventGroupDelete(g);
		return KOSA_StatusError;
	}
	slot->group = g;
	slot->auto_clear = auto_clear ? 1u : 0u;
	*osa_slot(handle) = (void *)g;
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_EventDestroy(osa_event_handle_t handle)
{
	void **slot = osa_slot(handle);
	struct osa_event_slot *ev;

	if (slot == NULL || *slot == NULL)
		return KOSA_StatusError;
	ev = osa_event_of((EventGroupHandle_t)*slot);
	if (ev != NULL) {
		ev->group = NULL;
		ev->auto_clear = 0;
	}
	vEventGroupDelete((EventGroupHandle_t)*slot);
	*slot = NULL;
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_EventSet(osa_event_handle_t handle, osa_event_flags_t flags)
{
	void **slot = osa_slot(handle);

	if (slot == NULL || *slot == NULL)
		return KOSA_StatusError;
	(void)xEventGroupSetBits((EventGroupHandle_t)*slot, (EventBits_t)flags);
	return KOSA_StatusSuccess;
}

osa_status_t
OSA_EventWait(osa_event_handle_t handle, osa_event_flags_t flags,
	      uint8_t wait_all, uint32_t timeout_ms,
	      osa_event_flags_t *out_flags)
{
	void **slot = osa_slot(handle);
	struct osa_event_slot *ev;
	EventBits_t bits;

	if (out_flags != NULL)
		*out_flags = 0;
	if (slot == NULL || *slot == NULL)
		return KOSA_StatusError;
	ev = osa_event_of((EventGroupHandle_t)*slot);
	bits = xEventGroupWaitBits((EventGroupHandle_t)*slot, (EventBits_t)flags,
				   (ev != NULL && ev->auto_clear) ? pdTRUE : pdFALSE,
				   wait_all ? pdTRUE : pdFALSE,
				   OSA_MS_TO_TICKS(timeout_ms));
	if (out_flags != NULL)
		*out_flags = (osa_event_flags_t)bits;
	if (bits == 0)
		return KOSA_StatusTimeout;
	return KOSA_StatusSuccess;
}
