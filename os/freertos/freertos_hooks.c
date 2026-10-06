/**
 * @file    freertos_hooks.c
 * @brief   FreeRTOS application-side obligations for the host port: fail-visible hooks
 *          and the static memory donors for the idle and timer service tasks.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * With configSUPPORT_STATIC_ALLOCATION == 1 and configKERNEL_PROVIDED_STATIC_MEMORY left
 * at its default 0, the kernel asks the application for the memory of the idle task and,
 * because configUSE_TIMERS == 1, of the timer service task. Both hooks print and abort so
 * that a failure is never silent; the message tag can be overridden at build time so that
 * a test suite can recognise its own failures.
 */

#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#ifndef FREERTOS_HOOKS_TAG
#define FREERTOS_HOOKS_TAG    "freertos-hooks"
#endif

void vAssertCalled( const char * pcFileName, unsigned long ulLineNumber )
{
    fprintf( stderr, FREERTOS_HOOKS_TAG ": ASSERT %s:%lu\n", pcFileName, ulLineNumber );
    fflush( stderr );
    abort();
}

void vApplicationMallocFailedHook( void )
{
    fprintf( stderr, FREERTOS_HOOKS_TAG ": MALLOC-FAIL\n" );
    fflush( stderr );
    abort();
}

void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer,
                                    StackType_t ** ppxIdleTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE * puxIdleTaskStackSize )
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t  uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *puxIdleTaskStackSize   = ( configSTACK_DEPTH_TYPE ) configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer,
                                     StackType_t ** ppxTimerTaskStackBuffer,
                                     configSTACK_DEPTH_TYPE * puxTimerTaskStackSize )
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t  uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *puxTimerTaskStackSize   = ( configSTACK_DEPTH_TYPE ) configTIMER_TASK_STACK_DEPTH;
}
