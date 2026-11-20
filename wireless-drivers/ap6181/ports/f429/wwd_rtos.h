/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wwd_rtos.h - the WWD RTOS port's type and policy surface, over FreeRTOS 11.
 *
 * The vendor driver asks this header for four handle types, the priority band
 * helpers and its thread stack size.  The port keeps the names the driver
 * expects and fills them with this platform's kernel; the numeric policy lives
 * in wiced_port_config.h.
 */

#ifndef INCLUDED_WWD_RTOS_H_
#define INCLUDED_WWD_RTOS_H_

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "wiced_port_config.h"

#define RTOS_LOWEST_PRIORITY		(0)
#define RTOS_HIGHEST_PRIORITY		(configMAX_PRIORITIES - 1)
#define RTOS_DEFAULT_THREAD_PRIORITY	(1)

#define RTOS_HIGHER_PRIORTIY_THAN(x) \
	(((x) < RTOS_HIGHEST_PRIORITY) ? ((x) + 1) : RTOS_HIGHEST_PRIORITY)
#define RTOS_LOWER_PRIORTIY_THAN(x) \
	(((x) > RTOS_LOWEST_PRIORITY) ? ((x) - 1) : RTOS_LOWEST_PRIORITY)

/* The kernel allocates the stack from its heap; the driver passes NULL. */
#define RTOS_USE_DYNAMIC_THREAD_STACK

#define WWD_THREAD_STACK_SIZE		(AP6181_WWD_THREAD_STACK)

typedef SemaphoreHandle_t	host_semaphore_type_t;
typedef SemaphoreHandle_t	host_mutex_type_t;
typedef TaskHandle_t		host_thread_type_t;
typedef QueueHandle_t		host_queue_type_t;

typedef struct {
	uint8_t info;		/* not used by this port */
} host_rtos_thread_config_type_t;

#endif /* INCLUDED_WWD_RTOS_H_ */