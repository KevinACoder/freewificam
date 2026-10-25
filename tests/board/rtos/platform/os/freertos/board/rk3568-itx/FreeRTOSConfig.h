/**
 * @file    FreeRTOSConfig.h
 * @brief   Board-side FreeRTOS configuration (RK3568, single core, EL1).
 * @author  zhugengyu
 * @date    07.10.2026
 * @design  proxy - the same kernel interface resolves to a different
 *          configuration header by include path; ref the host header
 *          os/freertos/FreeRTOSConfig.h and the include-path selection in
 *          tests/host/freertos-cmsis2-smoke/Makefile:21-29.
 *
 * This header is found by name (FreeRTOS.h includes "FreeRTOSConfig.h"): the
 * board build puts os/freertos/board first on the include path and does NOT
 * include os/freertos, so no host key can leak into the board build and the
 * host header stays untouched.
 *
 * Deliberately absent: configCPU_CLOCK_HZ and CMSIS_device_header. The port
 * reads CNTFRQ_EL0 at runtime for the tick period, and this image has no
 * CMSIS device layer; both keys belong to the host configuration.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* --- scheduler ------------------------------------------------------------ */

#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configTICK_RATE_HZ                       ( ( TickType_t ) 1000 )
#define configUSE_16_BIT_TICKS                   0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configUSE_TICKLESS_IDLE                  0
#define configMAX_PRIORITIES                     8
#define configMINIMAL_STACK_SIZE                 256
#define configMAX_TASK_NAME_LEN                  16

/* --- memory --------------------------------------------------------------- */

#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configSUPPORT_STATIC_ALLOCATION          0
#define configTOTAL_HEAP_SIZE                    ( 64 * 1024 )
#define configUSE_MALLOC_FAILED_HOOK             1

/* --- features this image uses --------------------------------------------- */

#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_TIMERS                         0
#define configUSE_MUTEXES                        0
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            0
#define configUSE_QUEUE_SETS                     0
#define configUSE_EVENT_GROUPS                   0
#define configUSE_STREAM_BUFFERS                 0
#define configUSE_TRACE_FACILITY                 0
#define configGENERATE_RUN_TIME_STATS            0

/* --- hooks ---------------------------------------------------------------- */

#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0

/* Paint and check the stack tail: a bring-up image wants the overflow to be
 * a named line, not a silent corruption. */
#define configCHECK_FOR_STACK_OVERFLOW           2

/* configUSE_TASK_FPU_SUPPORT is left undefined: the default (1) keeps tasks
 * without an FPU context, and the build is -mgeneral-regs-only, so no FP
 * instruction exists to need one. */

/* --- API inclusion -------------------------------------------------------- */

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

/* --- interrupt controller geometry ---------------------------------------- */

/* GIC-600 implements four priority bits (16 unique levels; measured on this
 * board, board_conf.h). The tick sits at the lowest usable level (raw 0xe0 =
 * 14 << 4) - the port's tick handler asserts exactly that (port.c), and
 * configMAX_API_CALL_INTERRUPT_PRIORITY must stay strictly between 16/2 and
 * 16 with room below the tick (port.c's compile-time checks). */
#define configUNIQUE_INTERRUPT_PRIORITIES        16
#define configMAX_API_CALL_INTERRUPT_PRIORITY    13

/* --- tick supply ---------------------------------------------------------- */

/* The ARM_AARCH64_SRE port calls these two slots; the board glue implements
 * them (GIC line INTID 27 + CNTV). */
void freertos_board_tick_setup( void );
void freertos_board_tick_rearm( void );
#define configSETUP_TICK_INTERRUPT()             freertos_board_tick_setup()
#define configCLEAR_TICK_INTERRUPT()             freertos_board_tick_rearm()

/* --- assert --------------------------------------------------------------- */

/* Report and park - an assert that cannot speak is indistinguishable from a
 * dead board. */
void vAssertCalled( const char * pcFile, unsigned long ulLine );
#define configASSERT( x )    do { if( ( x ) == 0 ) { vAssertCalled( __FILE__, __LINE__ ); } } while( 0 )

#endif /* FREERTOS_CONFIG_H */
