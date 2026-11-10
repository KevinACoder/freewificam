/**
 * @file    FreeRTOSConfig.h
 * @brief   Board-side FreeRTOS configuration for CMSIS-RTOS2 images (RK3568).
 * @author  zhugengyu
 * @date    07.10.2026
 * @design  proxy - the same kernel interface resolves to a different
 *          configuration header by include path; ref the host header
 *          os/freertos/FreeRTOSConfig.h and the include-path selection in
 *          tests/host/freertos-cmsis2-smoke/Makefile:21-29.
 *
 * This is the CMSIS-RTOS2 flavour of the board configuration: an image that
 * links the CMSIS-FreeRTOS adapter must satisfy that adapter's compile-time
 * checks (freertos_os2.h), which fix the priority space at 56 levels and
 * forbid the port-optimised task selection. The plain bring-up image keeps
 * its own, smaller configuration in the parent directory; the include path
 * decides which one a build sees, and only one is ever on it.
 *
 * The tick comes from the generic timer through the board glue, and the
 * counter frequency is read at runtime (CNTFRQ); configCPU_CLOCK_HZ below is
 * only what osKernelGetSysTimerFreq reports.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* --- scheduler ------------------------------------------------------------ */

#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configTICK_RATE_HZ                       ( ( TickType_t ) 1000 )
#define configUSE_16_BIT_TICKS                   0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                  0

/* The CMSIS-RTOS2 priority space (osPriority_t) has 56 levels and is mapped
 * 1:1 onto the kernel: freertos_os2.h rejects any other value, and it rejects
 * the port-optimised selection alongside it. */
#define configMAX_PRIORITIES                     56
#define configMINIMAL_STACK_SIZE                 256
#define configMAX_TASK_NAME_LEN                  16
/* 256 KiB: this image carries lwIP's threads and the storage stack, and the
 * kernel heap is where the facade's allocator pair sends their control blocks
 * and buffers.  The 64 KiB the bring-up image used ran out on the first
 * storage allocation (measured on the board, 2026-10-24).  The 8189FTV image
 * overrides the size at build time: the vendor stack's skb/control-block
 * demand is far above the storage image's, and the number is measured from the
 * heap high-water, not guessed here. */
#ifndef CONFIG_FREERTOS_HEAP_SIZE
#define CONFIG_FREERTOS_HEAP_SIZE		( 256U * 1024U )
#endif
#define configTOTAL_HEAP_SIZE                    ( ( size_t ) ( CONFIG_FREERTOS_HEAP_SIZE ) )

/* --- memory --------------------------------------------------------------- */

#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configUSE_MALLOC_FAILED_HOOK             1

/* --- features the CMSIS-RTOS2 adapter requires ---------------------------- */

#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                40
#define configTIMER_QUEUE_LENGTH                 5
#define configTIMER_TASK_STACK_DEPTH             512
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              1
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_TRACE_FACILITY                 1
#define configUSE_EVENT_GROUPS                   1
#define configUSE_STREAM_BUFFERS                 1

/* --- hooks ---------------------------------------------------------------- */

#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCHECK_FOR_STACK_OVERFLOW           2

/* --- API inclusion (each one is checked by freertos_os2.h) ---------------- */

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

/* --- interrupt controller geometry ---------------------------------------- */

/* GIC-600 implements four priority bits (16 unique levels). The tick sits at
 * the lowest usable level (raw 0xe0 = 14 << 4), which the port's tick handler
 * asserts; configMAX_API_CALL_INTERRUPT_PRIORITY must stay strictly between
 * 16/2 and 16 (port.c's compile-time checks). */
#define configUNIQUE_INTERRUPT_PRIORITIES        16
#define configMAX_API_CALL_INTERRUPT_PRIORITY    13

/* --- clock ---------------------------------------------------------------- */

/* The SoC's system counter frequency (CNTFRQ, 24 MHz on this board). The tick
 * reload is computed from the register at runtime; this constant is what
 * osKernelGetSysTimerFreq reports. */
#define configCPU_CLOCK_HZ                       24000000UL

/* --- device header -------------------------------------------------------- */

/* The adapter requires CMSIS_device_header; the board stand-in supplies the
 * AArch64 interrupt-state intrinsics (os/cmsis2/board-compat). */
#define CMSIS_device_header                      "cmsis_board_device.h"

/* --- tick supply ---------------------------------------------------------- */

/* The ARM_AARCH64_SRE port calls these two slots; the board glue implements
 * them (GIC line INTID 27 + CNTV). */
void freertos_board_tick_setup( void );
void freertos_board_tick_rearm( void );
#define configSETUP_TICK_INTERRUPT()             freertos_board_tick_setup()
#define configCLEAR_TICK_INTERRUPT()             freertos_board_tick_rearm()

/* --- assert --------------------------------------------------------------- */

void vAssertCalled( const char * pcFile, unsigned long ulLine );
#define configASSERT( x )    do { if( ( x ) == 0 ) { vAssertCalled( __FILE__, __LINE__ ); } } while( 0 )

#endif /* FREERTOS_CONFIG_H */
