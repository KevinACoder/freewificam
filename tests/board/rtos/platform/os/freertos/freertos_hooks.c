/**
 * @file    freertos_hooks.c
 * @brief   FreeRTOS application-side obligations: fail-visible hooks and the static
 *          memory donors for the idle and timer service tasks.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * With configSUPPORT_STATIC_ALLOCATION == 1 and configKERNEL_PROVIDED_STATIC_MEMORY left
 * at its default 0, the kernel asks the application for the memory of the idle task and,
 * because configUSE_TIMERS == 1, of the timer service task. Both donors hand over static
 * storage.
 *
 * The two failure hooks treat their events differently on purpose:
 *   - an assertion is a broken invariant: print and abort, so a failure is never
 *     silent and never continues on corrupt state;
 *   - an allocation failure is a resource event, not an invariant break: print and
 *     return. The kernel's allocator (Heap_4 here) has already refused the request
 *     by the time the hook runs, so the caller receives NULL and decides. Tests in
 *     this repository deliberately drive allocation-failure paths through the
 *     allocator seam, and a board's own policy for a truly exhausted heap belongs
 *     to the board layer, not to this shared hook. The message tag can be overridden
 *     at build time so a suite recognises its own run in a log.
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
    /* not fatal: see the file header - the allocator has already answered NULL */
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
