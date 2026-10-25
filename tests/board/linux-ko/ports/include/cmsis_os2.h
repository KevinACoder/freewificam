/*
 * @file
 * @brief The CMSIS-RTOS2 face the port's single osal is written against.
 *
 * The port's osal (components/net80211/ports/osal/cmsis2/osal_cmsis2.c) serves ThreadX,
 * FreeRTOS and the host through one CMSIS-RTOS2 adapter.  The Linux ko is one
 * more backend, so it supplies the same face: blocking mutexes (recursive),
 * counting semaphores, the thread identity, and the millisecond tick base.
 *
 * Only the members the port uses are declared; the values of osStatus_t are the
 * CMSIS-RTOS2 ones.
 */

#ifndef _WLAN_LINUX_CMSIS_OS2_H_
#define _WLAN_LINUX_CMSIS_OS2_H_

#include <stdint.h>

typedef int32_t osStatus_t;
typedef void *osMutexId_t;
typedef void *osSemaphoreId_t;
typedef void *osThreadId_t;

typedef struct {
	const char *name;
	uint32_t attr_bits;
	void *cb_mem;
	uint32_t cb_size;
} osMutexAttr_t;

typedef struct {
	const char *name;
	uint32_t attr_bits;
	void *cb_mem;
	uint32_t cb_size;
} osSemaphoreAttr_t;

#define osMutexRecursive	0x00000001u
#define osMutexPrioInherit	0x00000002u

#define osWaitForever		0xFFFFFFFFu

#define osOK			0
#define osError			1
#define osErrorTimeout		2
#define osErrorResource		3
#define osErrorParameter	4
#define osErrorNoMemory		5
#define osErrorISR		6

osMutexId_t osMutexNew(const osMutexAttr_t *attr);
osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout);
osStatus_t osMutexRelease(osMutexId_t mutex_id);
osStatus_t osMutexDelete(osMutexId_t mutex_id);

osSemaphoreId_t osSemaphoreNew(uint32_t max_count, uint32_t initial_count,
    const osSemaphoreAttr_t *attr);
osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout);
osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id);
osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id);

osThreadId_t osThreadGetId(void);
uint32_t osKernelGetTickCount(void);
uint32_t osKernelGetTickFreq(void);
osStatus_t osDelay(uint32_t ticks);

#endif /* _WLAN_LINUX_CMSIS_OS2_H_ */
