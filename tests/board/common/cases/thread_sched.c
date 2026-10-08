/**
 * @file    thread_sched.c
 * @brief   Case thread-sched: two priorities run side by side.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case body of the regression table (tests/board/common/board_suite.h);
 *          the two-thread shape and the lo thread's one-shot anchor follow the
 *          bring-up smoke (tests/board/threadx-rk3568-smoke/main.c:74-90).
 *
 * Both threads advance a counter through osDelay, so the case passes only when
 * the scheduler is preempting and switching between two priorities.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#define SCHED_TARGET 5U
#define SCHED_SPINS  200U

static volatile unsigned sched_hi;
static volatile unsigned sched_lo;

static void sched_hi_body( void *argument )
{
	(void)argument;
	while( sched_hi < SCHED_TARGET ) {
		sched_hi++;
		osDelay( 10U );
	}
	for( ;; ) {
		osDelay( 1000U );
	}
}

static void sched_lo_body( void *argument )
{
	(void)argument;
	sched_lo++;
	board_log( "thr[lo]: n=%lu\n", ( unsigned long )sched_lo );
	while( sched_lo < SCHED_TARGET ) {
		sched_lo++;
		osDelay( 10U );
	}
	for( ;; ) {
		osDelay( 1000U );
	}
}

static void thread_sched( void )
{
	static const osThreadAttr_t hi_attr = {
		.name = "hi",
		.priority = osPriorityAboveNormal,
		.stack_size = 4096U,
	};
	static const osThreadAttr_t lo_attr = {
		.name = "lo",
		.priority = osPriorityBelowNormal,
		.stack_size = 4096U,
	};
	unsigned spins;

	if( osThreadNew( sched_hi_body, 0, &hi_attr ) == 0 ) {
		board_case_fail( "hi thread create failed" );
		return;
	}
	if( osThreadNew( sched_lo_body, 0, &lo_attr ) == 0 ) {
		board_case_fail( "lo thread create failed" );
		return;
	}
	for( spins = 0; spins < SCHED_SPINS; spins++ ) {
		if( sched_hi >= SCHED_TARGET && sched_lo >= SCHED_TARGET ) {
			return;
		}
		osDelay( 10U );
	}
	board_case_fail( "two-priority scheduling stalled" );
}

BOARD_TEST( "thread-sched", thread_sched );
