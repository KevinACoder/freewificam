/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wwd_rtos.c - WWD's host_rtos_* entry points over this platform's FreeRTOS.
 *
 * The kernel is FreeRTOS 11 (CMSIS-RTOS2 sits in front of it for the cases, but
 * the driver talks to the kernel directly: a driver that went through the
 * facade would inherit the facade's thread table and stack accounting, and the
 * WWD thread has to be a plain task with a plain priority).  The differences
 * from the vendor's FreeRTOS-9 port, all deliberate:
 *
 *   - the WWD thread's requested top priority is mapped to a band below the
 *     runner (AP6181_WWD_THREAD_PRIORITY), so a busy driver cannot starve the
 *     case that is waiting on it;
 *   - NEVER_TIMEOUT maps to portMAX_DELAY instead of overflowing a tick
 *     multiplication;
 *   - join is a semaphore the terminating task gives before deleting itself,
 *     because FreeRTOS has no "is this task finished" query and polling a
 *     freed task handle is undefined behaviour;
 *   - the millisecond delay is one kernel call; the vendor's sub-tick busy
 *     loop is not reproduced (this kernel ticks at 1 kHz).
 */

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "RTOS/wwd_rtos_interface.h"
#include "platform/wwd_platform_interface.h"
#include "wwd_constants.h"

#include "wiced_port_config.h"

#define AP6181_JOIN_SLOTS 4

static void note_thread(TaskHandle_t task);

static struct {
	TaskHandle_t task;
	SemaphoreHandle_t done;
} s_join[AP6181_JOIN_SLOTS];

static int
join_slot_of(TaskHandle_t task)
{
	int i;

	for (i = 0; i < AP6181_JOIN_SLOTS; i++)
		if (s_join[i].task == task)
			return i;
	return -1;
}

static int
join_slot_free(void)
{
	int i;

	for (i = 0; i < AP6181_JOIN_SLOTS; i++)
		if (s_join[i].task == NULL)
			return i;
	return -1;
}

static TickType_t
ms_to_ticks(uint32_t timeout_ms)
{
	TickType_t ticks;

	if (timeout_ms == NEVER_TIMEOUT)
		return portMAX_DELAY;
	ticks = pdMS_TO_TICKS(timeout_ms);
	if (timeout_ms > 0U && ticks == 0U)
		ticks = 1U;
	return ticks;
}

static UBaseType_t
map_priority(uint32_t priority)
{
	if (priority == 0U)
		return (UBaseType_t)RTOS_DEFAULT_THREAD_PRIORITY;
	if (priority >= (uint32_t)RTOS_HIGHEST_PRIORITY)
		return (UBaseType_t)AP6181_WWD_THREAD_PRIORITY;
	if (priority >= (uint32_t)configMAX_PRIORITIES)
		return (UBaseType_t)(configMAX_PRIORITIES - 1);
	return (UBaseType_t)priority;
}

wwd_result_t
host_rtos_create_thread(host_thread_type_t *thread,
			void (*entry_function)(wwd_thread_arg_t arg),
			const char *name, void *stack, uint32_t stack_size,
			uint32_t priority)
{
	return host_rtos_create_thread_with_arg(thread, entry_function, name,
						stack, stack_size, priority, 0);
}

wwd_result_t
host_rtos_create_thread_with_arg(host_thread_type_t *thread,
				 void (*entry_function)(wwd_thread_arg_t arg),
				 const char *name, void *stack,
				 uint32_t stack_size, uint32_t priority,
				 wwd_thread_arg_t arg)
{
	BaseType_t rc;

	(void)stack;		/* the kernel allocates from its heap */
	if (thread == NULL || entry_function == NULL)
		return WWD_THREAD_CREATE_FAILED;

	rc = xTaskCreate((TaskFunction_t)entry_function, name,
			 (configSTACK_DEPTH_TYPE)(stack_size / sizeof(StackType_t)),
			 (void *)(uintptr_t)arg, map_priority(priority), thread);
	if (rc != pdPASS)
		return WWD_THREAD_CREATE_FAILED;

	note_thread(*thread);
	return WWD_SUCCESS;
}

wwd_result_t
host_rtos_create_configed_thread(host_thread_type_t *thread,
				 void (*entry_function)(wwd_thread_arg_t arg),
				 const char *name, void *stack,
				 uint32_t stack_size, uint32_t priority,
				 host_rtos_thread_config_type_t *config)
{
	(void)config;
	return host_rtos_create_thread(thread, entry_function, name, stack,
				       stack_size, priority);
}

wwd_result_t
host_rtos_finish_thread(host_thread_type_t *thread)
{
	int slot;

	if (thread == NULL || *thread == NULL)
		return WWD_THREAD_CREATE_FAILED;

	slot = join_slot_of(*thread);
	if (slot >= 0 && s_join[slot].done != NULL)
		(void)xSemaphoreGive(s_join[slot].done);

	vTaskDelete(*thread);
	*thread = NULL;
	return WWD_SUCCESS;
}

wwd_result_t
host_rtos_join_thread(host_thread_type_t *thread)
{
	int slot;

	if (thread == NULL)
		return WWD_THREAD_CREATE_FAILED;
	slot = join_slot_of(*thread);
	if (slot < 0 || s_join[slot].done == NULL)
		return WWD_SUCCESS;	/* nothing to wait for */
	(void)xSemaphoreTake(s_join[slot].done, portMAX_DELAY);
	return WWD_SUCCESS;
}

wwd_result_t
host_rtos_delete_terminated_thread(host_thread_type_t *thread)
{
	int slot;

	if (thread == NULL)
		return WWD_SUCCESS;
	slot = join_slot_of(*thread);
	if (slot >= 0) {
		if (s_join[slot].done != NULL)
			vSemaphoreDelete(s_join[slot].done);
		s_join[slot].done = NULL;
		s_join[slot].task = NULL;
	}
	return WWD_SUCCESS;
}

wwd_result_t
host_rtos_init_semaphore(host_semaphore_type_t *semaphore)
{
	if (semaphore == NULL)
		return WWD_SEMAPHORE_ERROR;
	*semaphore = xSemaphoreCreateCounting((UBaseType_t)0x7FFFFFFFU,
					      (UBaseType_t)0);
	return (*semaphore != NULL) ? WWD_SUCCESS : WWD_SEMAPHORE_ERROR;
}

wwd_result_t
host_rtos_get_semaphore(host_semaphore_type_t *semaphore, uint32_t timeout_ms,
			wiced_bool_t will_set_in_isr)
{
	(void)will_set_in_isr;
	if (semaphore == NULL || *semaphore == NULL)
		return WWD_SEMAPHORE_ERROR;
	if (xSemaphoreTake(*semaphore, ms_to_ticks(timeout_ms)) == pdTRUE)
		return WWD_SUCCESS;
	return WWD_TIMEOUT;
}

wwd_result_t
host_rtos_set_semaphore(host_semaphore_type_t *semaphore,
			wiced_bool_t called_from_ISR)
{
	BaseType_t rc;

	if (semaphore == NULL || *semaphore == NULL)
		return WWD_SEMAPHORE_ERROR;

	if (called_from_ISR == WICED_TRUE ||
	    host_platform_is_in_interrupt_context() == WICED_TRUE) {
		BaseType_t woken = pdFALSE;

		rc = xSemaphoreGiveFromISR(*semaphore, &woken);
		portYIELD_FROM_ISR(woken);
	} else {
		rc = xSemaphoreGive(*semaphore);
	}
	return (rc == pdPASS) ? WWD_SUCCESS : WWD_SEMAPHORE_ERROR;
}

wwd_result_t
host_rtos_deinit_semaphore(host_semaphore_type_t *semaphore)
{
	if (semaphore != NULL && *semaphore != NULL) {
		vSemaphoreDelete(*semaphore);
		*semaphore = NULL;
	}
	return WWD_SUCCESS;
}

wwd_time_t
host_rtos_get_time(void)
{
	return (wwd_time_t)(xTaskGetTickCount() * (TickType_t)portTICK_PERIOD_MS);
}

wwd_result_t
host_rtos_delay_milliseconds(uint32_t num_ms)
{
	TickType_t ticks = pdMS_TO_TICKS(num_ms);

	if (ticks == 0U && num_ms > 0U)
		ticks = 1U;
	if (ticks != 0U)
		vTaskDelay(ticks);
	return WWD_SUCCESS;
}

wwd_result_t
host_rtos_init_mutex(host_mutex_type_t *mutex)
{
	if (mutex == NULL)
		return WWD_RTOS_ERROR;
	*mutex = xSemaphoreCreateMutex();
	return (*mutex != NULL) ? WWD_SUCCESS : WWD_RTOS_ERROR;
}

wwd_result_t
host_rtos_lock_mutex(host_mutex_type_t *mutex)
{
	if (mutex == NULL || *mutex == NULL)
		return WWD_RTOS_ERROR;
	return (xSemaphoreTake(*mutex, portMAX_DELAY) == pdTRUE) ?
		WWD_SUCCESS : WWD_RTOS_ERROR;
}

wwd_result_t
host_rtos_unlock_mutex(host_mutex_type_t *mutex)
{
	if (mutex == NULL || *mutex == NULL)
		return WWD_RTOS_ERROR;
	return (xSemaphoreGive(*mutex) == pdTRUE) ? WWD_SUCCESS : WWD_RTOS_ERROR;
}

wwd_result_t
host_rtos_deinit_mutex(host_mutex_type_t *mutex)
{
	if (mutex != NULL && *mutex != NULL) {
		vSemaphoreDelete(*mutex);
		*mutex = NULL;
	}
	return WWD_SUCCESS;
}

wwd_result_t
host_rtos_init_queue(host_queue_type_t *queue, void *buffer,
		     uint32_t buffer_size, uint32_t message_size)
{
	(void)buffer;
	if (queue == NULL || message_size == 0U)
		return WWD_QUEUE_ERROR;
	*queue = xQueueCreate((UBaseType_t)(buffer_size / message_size),
			      (UBaseType_t)message_size);
	return (*queue != NULL) ? WWD_SUCCESS : WWD_QUEUE_ERROR;
}

wwd_result_t
host_rtos_push_to_queue(host_queue_type_t *queue, void *message,
			uint32_t timeout_ms)
{
	BaseType_t rc;

	if (queue == NULL || *queue == NULL)
		return WWD_QUEUE_ERROR;
	if (host_platform_is_in_interrupt_context() == WICED_TRUE ||
	    timeout_ms == 0U) {
		BaseType_t woken = pdFALSE;

		rc = xQueueSendToBackFromISR(*queue, message, &woken);
		portYIELD_FROM_ISR(woken);
	} else {
		rc = xQueueSendToBack(*queue, message, ms_to_ticks(timeout_ms));
	}
	return (rc == pdPASS) ? WWD_SUCCESS : WWD_QUEUE_ERROR;
}

wwd_result_t
host_rtos_pop_from_queue(host_queue_type_t *queue, void *message,
			 uint32_t timeout_ms)
{
	BaseType_t rc;

	if (queue == NULL || *queue == NULL)
		return WWD_QUEUE_ERROR;
	if (host_platform_is_in_interrupt_context() == WICED_TRUE) {
		BaseType_t woken = pdFALSE;

		rc = xQueueReceiveFromISR(*queue, message, &woken);
		portYIELD_FROM_ISR(woken);
	} else {
		rc = xQueueReceive(*queue, message, ms_to_ticks(timeout_ms));
	}
	if (rc == errQUEUE_EMPTY)
		return WWD_TIMEOUT;
	return (rc == pdPASS) ? WWD_SUCCESS : WWD_QUEUE_ERROR;
}

wwd_result_t
host_rtos_deinit_queue(host_queue_type_t *queue)
{
	if (queue != NULL && *queue != NULL) {
		vQueueDelete(*queue);
		*queue = NULL;
	}
	return WWD_SUCCESS;
}

unsigned long
host_rtos_get_tickrate(void)
{
	return (unsigned long)configTICK_RATE_HZ;
}

/* The join slot is recorded when a thread is created, not when it exits: the
 * terminating task gives the semaphore and then deletes itself, and the waiter
 * must have the handle before that happens. */
static void
note_thread(TaskHandle_t task)
{
	int slot;

	if (task == NULL)
		return;
	slot = join_slot_free();
	if (slot < 0)
		return;
	s_join[slot].task = task;
	s_join[slot].done = xSemaphoreCreateCounting((UBaseType_t)1,
						     (UBaseType_t)0);
}