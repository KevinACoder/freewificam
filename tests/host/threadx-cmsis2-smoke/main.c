/**
 * @file    main.c
 * @brief   Host smoke test: the ThreadX kernel behind the CMSIS-RTOS2 API.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * The same scenario as the FreeRTOS smoke test of this directory's sibling: two threads
 * at normal priority share one message queue. The producer publishes 1, 2, 3 with zero
 * timeout, delays five ticks and checks that the kernel tick advanced; the consumer
 * blocks on the queue and drains it. The anchor lines on stdout carry the prefix
 * "os2-smoke: " and are the machine-checked evidence of the run.
 *
 * One difference follows from the backend: there is no osThreadExit() on this side, so
 * the consumer simply returns from its thread function when it is done, which the
 * backend turns into a parked thread.
 *
 * This file sees the CMSIS-RTOS2 API only: os/cmsis2/cmsis2.h is the single entry point,
 * and the host exit seam it declares keeps kernel headers out of application code.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "os/cmsis2/cmsis2.h"

#define TAG               "os2-smoke: "
#define QUEUE_DEPTH       4U
#define ITEM_COUNT        3U
#define DELAY_TICKS       5U
#define THREAD_STACK_SIZE 2048U

static osMessageQueueId_t xQueue;

static void fail( const char * pcWhat );

static void noopTimer( void * argument )
{
    ( void ) argument;
}

/*
 * The static timer table behind osTimerNew is sized per image (the board images
 * pass their own CMSIS2_MAX_TIMERS on the compile line; this smoke pins a small
 * one).  Two properties are what a caller depends on, and both were once wrong
 * on this backend: the table holds exactly that many objects - the next create is
 * refused with NULL, never a silent overrun - and a released slot is reusable,
 * so a subsystem that creates and deletes timers does not drain the table for
 * the lifetime of the image.
 */
static void timerTable( void )
{
    osTimerId_t ids[ CMSIS2_MAX_TIMERS + 1U ];
    uint32_t i;

    for( i = 0U; i < ( uint32_t ) CMSIS2_MAX_TIMERS; i++ )
    {
        ids[ i ] = osTimerNew( noopTimer, osTimerOnce, NULL, NULL );

        if( ids[ i ] == NULL )
        {
            fail( "timer create within the table" );
        }
    }

    ids[ CMSIS2_MAX_TIMERS ] = osTimerNew( noopTimer, osTimerOnce, NULL, NULL );

    if( ids[ CMSIS2_MAX_TIMERS ] != NULL )
    {
        fail( "timer create past the table was not refused" );
    }

    printf( TAG "timer table bounded (n=%u), the next create is refused\n",
            ( unsigned ) CMSIS2_MAX_TIMERS );
    fflush( stdout );

    if( osTimerDelete( ids[ 0 ] ) != osOK )
    {
        fail( "timer delete" );
    }

    ids[ 0 ] = osTimerNew( noopTimer, osTimerOnce, NULL, NULL );

    if( ids[ 0 ] == NULL )
    {
        fail( "a released timer slot was not reusable" );
    }

    printf( TAG "a released timer slot is reusable\n" );
    fflush( stdout );

    for( i = 0U; i < ( uint32_t ) CMSIS2_MAX_TIMERS; i++ )
    {
        ( void ) osTimerDelete( ids[ i ] );
    }
}

static void fail( const char * pcWhat )
{
    fflush( stdout );
    fprintf( stderr, TAG "FAIL %s\n", pcWhat );
    fflush( stderr );
    abort();
}

static void consumer( void * argument )
{
    ( void ) argument;

    for( uint32_t i = 0U; i < ITEM_COUNT; i++ )
    {
        uint32_t value = 0U;

        if( osMessageQueueGet( xQueue, &value, NULL, osWaitForever ) != osOK )
        {
            fail( "queue get" );
        }

        printf( TAG "got %" PRIu32 "\n", value );
        fflush( stdout );
    }
}

static void producer( void * argument )
{
    ( void ) argument;

    /* The kernel is running here: this thread exists only because the scheduler started
     * the threads that were registered before the kernel start call. */
    if( osKernelGetState() != osKernelRunning )
    {
        fail( "kernel state" );
    }

    printf( TAG "kernel running\n" );
    fflush( stdout );

    timerTable();

    for( uint32_t value = 1U; value <= ITEM_COUNT; value++ )
    {
        if( osMessageQueuePut( xQueue, &value, 0U, 0U ) != osOK )
        {
            fail( "queue put" );
        }
    }

    osDelay( DELAY_TICKS );

    uint32_t ticks = osKernelGetTickCount();

    if( ticks < DELAY_TICKS )
    {
        fail( "tick did not advance" );
    }

    printf( TAG "tick advanced n=%" PRIu32 "\n", ticks );
    printf( TAG "PASS\n" );
    fflush( stdout );

    cmsis2_host_end_scheduler();
}

int main( void )
{
    static const osThreadAttr_t xConsumerAttr = {
        .name       = "consumer",
        .priority   = osPriorityNormal,
        .stack_size = THREAD_STACK_SIZE,
    };
    static const osThreadAttr_t xProducerAttr = {
        .name       = "producer",
        .priority   = osPriorityNormal,
        .stack_size = THREAD_STACK_SIZE,
    };

    if( osKernelInitialize() != osOK )
    {
        fail( "kernel init" );
    }

    xQueue = osMessageQueueNew( QUEUE_DEPTH, sizeof( uint32_t ), NULL );

    if( xQueue == NULL )
    {
        fail( "queue create" );
    }

    /* The consumer is created first so that it reaches the empty queue before the
     * producer publishes; both threads then run at the same priority. */
    if( osThreadNew( consumer, NULL, &xConsumerAttr ) == NULL )
    {
        fail( "consumer create" );
    }

    if( osThreadNew( producer, NULL, &xProducerAttr ) == NULL )
    {
        fail( "producer create" );
    }

    /* On this backend the call does not come back: the kernel runs on this thread until
     * the process ends. The branch is kept so that the two smoke tests read the same. */
    if( osKernelStart() != osOK )
    {
        fail( "kernel start" );
    }

    return 0;
}
