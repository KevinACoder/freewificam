/**
 * @file    c9-handle-trunc.c
 * @brief   C9: CMSIS-RTOS2 objects whose handle address is above 4 GiB.
 * @author  zhugengyu
 * @date    07.10.2026
 *
 * The upstream adapter used to pack a flag bit into a couple of handles through
 * uint32_t casts, which truncate on a 64-bit host: osMutexAcquire() faulted and the
 * timer callback dispatch dereferenced a truncated pointer.  The fix is
 * patches/CMSIS-FreeRTOS/0001-*.patch, replayed by the build.
 *
 * One case per invocation, so that a fault is the process's own business:
 *
 *   c9-handle-trunc mutex      recursive mutex: create, acquire twice, release twice
 *   c9-handle-trunc timer      periodic timer: start, expiry callbacks, stop, delete
 *   c9-handle-trunc mempool    pool from a static array: create, alloc, free, delete
 *
 * Every case asserts first that the object handle lives above 4 GiB.  That is the
 * precondition of this test: a build with low addresses (no PIE, or a 32-bit target)
 * would exercise nothing, so it must fail loudly instead of passing green.
 *
 * Anchors on stdout: "c9: <case> handle=0x..." then "c9: <case> ok".
 * Exit codes: 0 pass, 1 failed assertion, 2 precondition unmet, signal = the historical
 * red signature (the truncating build faults before it can report).
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#define TAG               "c9: "
#define HANDLE_FLOOR      UINT64_C(0x100000000)
#define THREAD_STACK_SIZE 2048U
#define POOL_BLOCKS       4U
#define POOL_BLOCK_SIZE   32U
#define TIMER_PERIOD_TICKS 10U
#define TIMER_WAIT_TICKS  50U

static const char *g_case;
static int         g_failed;

static osMutexId_t      g_mutex;
static osTimerId_t      g_timer;
static osMemoryPoolId_t g_pool;
static volatile uint32_t g_timer_callbacks;

static uint64_t g_pool_mem[(POOL_BLOCKS * POOL_BLOCK_SIZE) / sizeof(uint64_t)];

static void fail( const char * pcWhat )
{
    fflush( stdout );
    fprintf( stderr, TAG "%s FAIL: %s\n", g_case, pcWhat );
    fflush( stderr );
    g_failed = 1;
}

/* The precondition: the handle must live above 4 GiB, or this case proves nothing. */
static int precondition_handle( const void * handle )
{
    printf( TAG "%s handle=%p\n", g_case, handle );
    fflush( stdout );

    if( (uintptr_t) handle < (uintptr_t) HANDLE_FLOOR )
    {
        fprintf( stderr, TAG "%s precondition unmet: handle %p is not above 4 GiB\n",
                 g_case, handle );
        fflush( stderr );
        return 0;
    }

    return 1;
}

static void timer_callback( void * argument )
{
    (void) argument;
    g_timer_callbacks++;
}

static void mutex_case( void * argument )
{
    osStatus_t rc;

    (void) argument;

    /* Recursive: the second acquire exercises the flag bit the adapter packs in. */
    rc = osMutexAcquire( g_mutex, osWaitForever );
    if( rc != osOK )
    {
        fail( "osMutexAcquire" );
    }
    rc = osMutexAcquire( g_mutex, osWaitForever );
    if( rc != osOK )
    {
        fail( "osMutexAcquire (recursive)" );
    }
    rc = osMutexRelease( g_mutex );
    if( rc != osOK )
    {
        fail( "osMutexRelease" );
    }
    rc = osMutexRelease( g_mutex );
    if( rc != osOK )
    {
        fail( "osMutexRelease (recursive)" );
    }

    cmsis2_host_end_scheduler();
}

static void timer_case( void * argument )
{
    osStatus_t rc;

    (void) argument;

    rc = osTimerStart( g_timer, TIMER_PERIOD_TICKS );
    if( rc != osOK )
    {
        fail( "osTimerStart" );
    }

    osDelay( TIMER_WAIT_TICKS );

    printf( TAG "%s callbacks=%" PRIu32 "\n", g_case, g_timer_callbacks );
    fflush( stdout );
    if( g_timer_callbacks == 0U )
    {
        fail( "timer never fired" );
    }

    rc = osTimerStop( g_timer );
    if( rc != osOK )
    {
        fail( "osTimerStop" );
    }
    rc = osTimerDelete( g_timer );
    if( rc != osOK )
    {
        fail( "osTimerDelete" );
    }

    cmsis2_host_end_scheduler();
}

static void mempool_case( void * argument )
{
    void * block;

    (void) argument;

    block = osMemoryPoolAlloc( g_pool, osWaitForever );
    printf( TAG "%s block=%p\n", g_case, block );
    fflush( stdout );

    if( block == NULL )
    {
        fail( "osMemoryPoolAlloc" );
    }
    else
    {
        memset( block, 0xA5, POOL_BLOCK_SIZE );
        if( osMemoryPoolFree( g_pool, block ) != osOK )
        {
            fail( "osMemoryPoolFree" );
        }
    }

    cmsis2_host_end_scheduler();
}

static void usage( const char * argv0 )
{
    fprintf( stderr, "usage: %s mutex|timer|mempool\n", argv0 );
}

int main( int argc, char ** argv )
{
    static const osThreadAttr_t thread_attr = {
        .name       = "c9",
        .priority   = osPriorityNormal,
        .stack_size = THREAD_STACK_SIZE,
    };
    static const osMutexAttr_t mutex_attr = {
        .name      = "c9-mutex",
        .attr_bits = osMutexRecursive,
    };
    static const osMemoryPoolAttr_t pool_attr = {
        .name    = "c9-pool",
        .mp_mem  = g_pool_mem,
        .mp_size = sizeof( g_pool_mem ),
    };
    osThreadFunc_t entry;

    if( argc != 2 )
    {
        usage( argv[0] );
        return 1;
    }
    g_case = argv[1];

    if( osKernelInitialize() != osOK )
    {
        fail( "osKernelInitialize" );
        return 1;
    }

    if( strcmp( g_case, "mutex" ) == 0 )
    {
        g_mutex = osMutexNew( &mutex_attr );
        if( g_mutex == NULL )
        {
            fail( "osMutexNew" );
            return 1;
        }
        if( !precondition_handle( g_mutex ) )
        {
            return 2;
        }
        entry = mutex_case;
    }
    else if( strcmp( g_case, "timer" ) == 0 )
    {
        g_timer = osTimerNew( timer_callback, osTimerPeriodic, NULL, NULL );
        if( g_timer == NULL )
        {
            fail( "osTimerNew" );
            return 1;
        }
        if( !precondition_handle( g_timer ) )
        {
            return 2;
        }
        entry = timer_case;
    }
    else if( strcmp( g_case, "mempool" ) == 0 )
    {
        g_pool = osMemoryPoolNew( POOL_BLOCKS, POOL_BLOCK_SIZE, &pool_attr );
        if( g_pool == NULL )
        {
            fail( "osMemoryPoolNew" );
            return 1;
        }
        if( !precondition_handle( g_pool ) )
        {
            return 2;
        }
        entry = mempool_case;
    }
    else
    {
        usage( argv[0] );
        return 1;
    }

    if( osThreadNew( entry, NULL, &thread_attr ) == NULL )
    {
        fail( "osThreadNew" );
        return 1;
    }

    if( osKernelStart() != osOK )
    {
        fail( "osKernelStart" );
        return 1;
    }

    if( g_failed )
    {
        printf( TAG "%s FAIL\n", g_case );
        return 1;
    }

    printf( TAG "%s ok\n", g_case );
    return 0;
}
