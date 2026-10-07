/**
 * @file    main.c
 * @brief   FreeRTOS RK3568 bring-up smoke: kernel starts, tick runs, tasks switch.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design  task/priority demo - two priority levels plus a same-priority pair
 *          for time slicing; the anchor lines below are the board-test
 *          contract (fixed, greppable text).
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU;
 * this file brings the console up, prints the banner, creates the tasks and
 * starts the scheduler. The tick is enabled by the port inside
 * xPortStartScheduler() through the board glue (configSETUP_TICK_INTERRUPT).
 *
 * Anchor lines:
 *   freertos-rk3568 bring-up <build-tag>
 *   freertos: kernel V11.3.1 port=ARM_AARCH64_SRE guest=EL1
 *   gicv3: ready ctlr=<hex> waker=<hex>        (glue, on scheduler start)
 *   tick: source=cntv irq=27 freq=1000Hz       (glue, on scheduler start)
 *   task[hi] running loop=0
 *   task[lo] running loop=0
 *   freertos: tick=1000 switches=<n>           (every 1000 ticks, >=3 beats)
 *   freertos: smoke PASS
 *
 * After PASS the workers are suspended and the image idles (idle task + tick)
 * - quiet on the console until the board is power-cycled.
 */

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "arch_asm.h"
#include "uart.h"

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

/* Scheduling-activation counters, one per worker. "switches" in the anchor
 * line is their sum: a task increments its own slot at the top of its loop,
 * so a growing sum is direct evidence that the scheduler keeps switching
 * between tasks. */
static volatile uint32_t act_hi;
static volatile uint32_t act_lo_a;
static volatile uint32_t act_lo_b;

static TaskHandle_t hi_handle;
static TaskHandle_t lo_a_handle;
static TaskHandle_t lo_b_handle;

static void put_dec( uint32_t value )
{
	char buf[12];
	int i = 0;

	if( value == 0U ) {
		uart_early_putc( '0' );
		return;
	}
	while( ( value != 0U ) && ( i < (int)sizeof( buf ) ) ) {
		buf[i++] = (char)( '0' + ( value % 10U ) );
		value /= 10U;
	}
	while( i > 0 ) {
		uart_early_putc( buf[--i] );
	}
}

/* Unrecoverable smoke failure: report and park (an assert that cannot speak
 * is indistinguishable from a dead board). */
static void smoke_fatal( const char *what )
{
	uart_early_puts( "fatal: " );
	uart_early_puts( what );
	uart_early_puts( "\n" );
	uart_early_puts( "fatal: parked; power-cycle to recover\n" );

	ARCH_DAIF_SET( 0xf );
	for( ;; ) {
		ARCH_WFE();
	}
}

/* --- workers --------------------------------------------------------------- */

/* The higher-priority worker: runs periodically, so its line proves the
 * preemptive path (it preempts the spinning pair below). */
static void task_hi( void *arg )
{
	(void)arg;
	for( ;; ) {
		if( act_hi == 0U ) {
			uart_early_puts( "task[hi] running loop=0\n" );
		}
		act_hi++;
		vTaskDelay( pdMS_TO_TICKS( 100 ) );
	}
}

/* The same-priority pair: both stay ready and spin, so the tick's time
 * slicing rotates between them - their counters should stay within a small
 * factor of each other. */
static void task_lo_a( void *arg )
{
	(void)arg;
	for( ;; ) {
		if( act_lo_a == 0U ) {
			uart_early_puts( "task[lo] running loop=0\n" );
		}
		act_lo_a++;
	}
}

static void task_lo_b( void *arg )
{
	(void)arg;
	for( ;; ) {
		act_lo_b++;
	}
}

/* --- reporter -------------------------------------------------------------- */

static void task_report( void *arg )
{
	(void)arg;
	TickType_t next = xTaskGetTickCount() + pdMS_TO_TICKS( 1000 );
	uint32_t beat = 0U;

	for( ;; ) {
		vTaskDelayUntil( &next, pdMS_TO_TICKS( 1000 ) );
		beat++;
		uart_early_puts( "freertos: tick=" );
		put_dec( beat * 1000U );
		uart_early_puts( " switches=" );
		put_dec( act_hi + act_lo_a + act_lo_b );
		uart_early_puts( "\n" );
		if( beat >= 3U ) {
			break;
		}
	}

	uart_early_puts( "freertos: smoke PASS\n" );

	/* Idle the image: suspend the workers, then this task. What remains is
	 * the idle task (WFI) and the tick - quiet, and alive until the board
	 * is power-cycled. */
	vTaskSuspend( hi_handle );
	vTaskSuspend( lo_a_handle );
	vTaskSuspend( lo_b_handle );
	vTaskSuspend( NULL );

	for( ;; ) {
		ARCH_WFE();
	}
}

int main( void )
{
	uart_early_init();

	uart_early_puts( "freertos-rk3568 bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );
	uart_early_puts( "freertos: kernel " );
	uart_early_puts( tskKERNEL_VERSION_NUMBER );
	uart_early_puts( " port=ARM_AARCH64_SRE guest=EL1\n" );

	if( xTaskCreate( task_report, "report", 512, 0, 3, 0 ) != pdPASS ) {
		smoke_fatal( "task create failed (report)" );
	}
	if( xTaskCreate( task_hi, "hi", 512, 0, 2, &hi_handle ) != pdPASS ) {
		smoke_fatal( "task create failed (hi)" );
	}
	if( xTaskCreate( task_lo_a, "lo_a", 512, 0, 1, &lo_a_handle ) != pdPASS ) {
		smoke_fatal( "task create failed (lo_a)" );
	}
	if( xTaskCreate( task_lo_b, "lo_b", 512, 0, 1, &lo_b_handle ) != pdPASS ) {
		smoke_fatal( "task create failed (lo_b)" );
	}

	vTaskStartScheduler();

	/* Not reached: the scheduler does not return. */
	smoke_fatal( "scheduler returned" );
	return 0;
}
