/**
 * @file    FreeRTOSConfig.h
 * @brief   FreeRTOS kernel configuration for the host (POSIX port) build.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * Hand written kernel configuration for host builds that drive the FreeRTOS POSIX port
 * on Linux. Values marked as adapter requirements are checked with #error by
 * freertos_os2.h in the upstream CMSIS-FreeRTOS tree; do not weaken them without
 * re-reading that header.
 *
 * There is no device on a host, so CMSIS_device_header points at the stand-in shipped in
 * os/cmsis2/host-compat. A real target replaces that single line with its vendor header.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Scheduler and tick. */
#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configTICK_RATE_HZ                       ( ( TickType_t ) 1000 )
#define configUSE_16_BIT_TICKS                   0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                  0

/* Limits. configMAX_PRIORITIES must be exactly 56 for the CMSIS-RTOS2 adapter, whose
 * priority space (osPriority_t) has 56 levels and is mapped 1:1 onto the kernel. */
#define configMAX_PRIORITIES                     56
#define configMINIMAL_STACK_SIZE                 256
/* The host heap. Board flavors carry their own size in their variant headers; this
 * one is the host's and only the host's. It grew from 64K when the linuxkpi service
 * bodies became the real ones: their allocation now goes through this heap (the
 * FreeRTOS backend's osMemoryAlloc) instead of libc, so the urb-stress streams and
 * the host test tier's object churn live here - 64K was a board-shaped budget a host
 * has no reason to hold. */
#define configTOTAL_HEAP_SIZE                    ( ( size_t ) ( 1024 * 1024 ) )

/* Memory allocation. Static allocation is enabled because the application supplies the
 * idle and timer service task memory (freertos_hooks.c); dynamic allocation feeds every
 * CMSIS-RTOS2 object created with a NULL attribute. */
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         1

/* Timer service task: CMSIS-RTOS2 timers and event flags are built on it. */
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                40
#define configTIMER_QUEUE_LENGTH                 5
#define configTIMER_TASK_STACK_DEPTH             256

/* Optional kernel features required by the CMSIS-RTOS2 adapter. */
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              1
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_TRACE_FACILITY                 1
#define configUSE_EVENT_GROUPS                   1
#define configUSE_STREAM_BUFFERS                 1

/* Hooks. Idle and tick hooks stay off; a failed allocation must be loud, never silent. */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configUSE_DAEMON_TASK_STARTUP_HOOK       0
#define configUSE_MALLOC_FAILED_HOOK             1

/* Optional API surface the CMSIS-RTOS2 adapter requires (freertos_os2.h checks each). */
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskDelayUntil                  1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_eTaskGetState                    1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_xSemaphoreGetMutexHolder         1
#define INCLUDE_xTimerPendFunctionCall           1
/* Not checked by freertos_os2.h, but the adapter calls it (osThreadResume). */
#define INCLUDE_xTaskAbortDelay                  1

/* Host clock: the POSIX port reads CLOCK_MONOTONIC, so the "CPU clock" reported by
 * osKernelGetSysTimerFreq is that clock's 1 ns resolution. */
#define configCPU_CLOCK_HZ                       1000000000UL

/* The CMSIS-RTOS2 adapter requires CMSIS_device_header. On the host it resolves to the
 * stand-in in os/cmsis2/host-compat: no device and no SysTick, because the tick comes
 * from the port's timer thread. */
#define CMSIS_device_header                      "cmsis_host_device.h"

/* Errors must be visible: report and abort. */
extern void vAssertCalled( const char * pcFileName, unsigned long ulLineNumber );
#define configASSERT( x )    do { if( ( x ) == 0 ) { vAssertCalled( __FILE__, __LINE__ ); } } while( 0 )

#endif /* FREERTOS_CONFIG_H */
