/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fsl_os_abstraction.h
 * @brief   The NXP OSA surface the imported wpa_supplicant-rtos port calls,
 *          backed by FreeRTOS in osa_lkpi.c.
 *
 * The NXP tree's FreeRTOS files (src/utils/os_freertos.c, eloop_freertos.c,
 * freertos/src/supp_main.c) are written against the MCUXpresso OSA API.  This
 * header states exactly that surface - the identifiers the imported files use,
 * nothing more - and osa_lkpi.c implements it with the FreeRTOS kernel the
 * image already carries.  A handle is a one-word slot the caller allocates with
 * the OSA_*_HANDLE_DEFINE macros; the kernel object pointer lives in the slot.
 */
#ifndef FSL_OS_ABSTRACTION_H
#define FSL_OS_ABSTRACTION_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* the kernel this abstraction is over: the NXP-port files expect the OSA
 * header to carry it (os_freertos.c uses FreeRTOS types directly) */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

/* the NXP entry asks the platform for its lwIP netif (net_get_*_interface) and
 * for the middleware's failure code; both live in the port's wm_net.h, which
 * the FreeRTOS branch of supp_main.c does not include itself */
#include "wm_net.h"

typedef enum {
	KOSA_StatusSuccess = 0,
	KOSA_StatusError = 1,
	KOSA_StatusTimeout = 2,
	KOSA_StatusIdle = 3,
	KOSA_StatusOther = 4,
} osa_status_t;

typedef void *osa_task_param_t;
typedef void (*osa_task_entry_t)(osa_task_param_t);

typedef void *osa_task_handle_t;
typedef void *osa_mutex_handle_t;
typedef void *osa_semaphore_handle_t;
typedef void *osa_event_handle_t;
typedef uint32_t osa_event_flags_t;

/* the "wait forever" sentinel the NXP call sites pass as a timeout */
#define osaWaitForever_c	0xFFFFFFFFU

/*
 * A handle is the address of a one-word slot: the macros below are the
 * carriers, and every OSA_* call casts the array to its handle type - the same
 * shape the NXP SDK's static handle definitions have.  They are also usable as
 * struct members (src/utils/block_alloc.h embeds one).
 */
#define OSA_TASK_HANDLE_DEFINE(name)		void *name[1]
#define OSA_MUTEX_HANDLE_DEFINE(name)		void *name[1]
#define OSA_SEMAPHORE_HANDLE_DEFINE(name)	void *name[1]
#define OSA_EVENT_HANDLE_DEFINE(name)		void *name[1]

struct osa_task_def {
	osa_task_entry_t	entry;
	uint32_t		priority;
	uint32_t		stack_size;
	uint32_t		instances;
	uint32_t		use_float;
};

#define OSA_TASK_DEFINE(func, prio, inst, stacksz, fp)			\
	const struct osa_task_def func##_task_def = {			\
		(osa_task_entry_t)(func), (uint32_t)(prio),		\
		(uint32_t)(stacksz), (uint32_t)(inst), (uint32_t)(fp)	\
	}

#define OSA_TASK(def)			(&(def##_task_def))
#define PRIORITY_RTOS_TO_OSA(p)		((uint32_t)(p))

osa_status_t OSA_TaskCreate(osa_task_handle_t handle,
			    const struct osa_task_def *def,
			    osa_task_param_t arg);
void OSA_TaskYield(void);
void OSA_TimeDelay(uint32_t ms);
uint32_t OSA_TimeGetMsec(void);
uint32_t OSA_Rand(void);

void *OSA_MemoryAllocate(uint32_t size);
void OSA_MemoryFree(void *ptr);

osa_status_t OSA_MutexCreate(osa_mutex_handle_t handle);
osa_status_t OSA_MutexDestroy(osa_mutex_handle_t handle);
osa_status_t OSA_MutexLock(osa_mutex_handle_t handle, uint32_t timeout_ms);
osa_status_t OSA_MutexUnlock(osa_mutex_handle_t handle);

osa_status_t OSA_SemaphoreCreateBinary(osa_semaphore_handle_t handle);
osa_status_t OSA_SemaphoreDestroy(osa_semaphore_handle_t handle);
osa_status_t OSA_SemaphorePost(osa_semaphore_handle_t handle);
osa_status_t OSA_SemaphoreWait(osa_semaphore_handle_t handle,
			       uint32_t timeout_ms);

osa_status_t OSA_EventCreate(osa_event_handle_t handle, uint8_t auto_clear);
osa_status_t OSA_EventDestroy(osa_event_handle_t handle);
osa_status_t OSA_EventSet(osa_event_handle_t handle, osa_event_flags_t flags);
osa_status_t OSA_EventWait(osa_event_handle_t handle, osa_event_flags_t flags,
			   uint8_t auto_clear, uint32_t timeout_ms,
			   osa_event_flags_t *out_flags);

#endif /* FSL_OS_ABSTRACTION_H */
