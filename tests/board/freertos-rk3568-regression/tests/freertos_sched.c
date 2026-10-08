/**
 * @file    freertos_sched.c
 * @brief   Case freertos-sched: two kernel tasks run and are preempted.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case row of the regression table (tests/board/common/board_suite.h);
 *          the two-task shape and the anchor lines follow the board smoke's
 *          task loop (tests/board/freertos-rk3568-smoke/main.c:96-111), which
 *          is the FreeRTOS line's own liveness evidence.
 *
 * The kernel-specific counterpart of the shared thread-sched case: this one
 * prints the task lines the FreeRTOS judge anchors on, so a scheduler
 * regression is localized to the kernel that broke.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#define SCHED_TARGET 5U
#define SCHED_SPINS  200U

static volatile unsigned sched_hi_done;
static volatile unsigned sched_lo_done;

static void hi_body( void *argument )
{
	(void)argument;
	for( unsigned i = 0; i < SCHED_TARGET; i++ ) {
		osDelay( 10U );
	}
	sched_hi_done = 1U;
	board_log( "task[hi] running loop=%u\n", sched_hi_done );
	for( ;; ) {
		osDelay( 1000U );
	}
}

static void lo_body( void *argument )
{
	(void)argument;
	for( unsigned i = 0; i < SCHED_TARGET; i++ ) {
		osDelay( 10U );
	}
	sched_lo_done = 1U;
	board_log( "task[lo] running loop=%u\n", sched_lo_done );
	for( ;; ) {
		osDelay( 1000U );
	}
}

static void freertos_sched( void )
{
	static const osThreadAttr_t hi_attr = {
		.name = "t_hi",
		.priority = osPriorityAboveNormal,
		.stack_size = 2048U,
	};
	static const osThreadAttr_t lo_attr = {
		.name = "t_lo",
		.priority = osPriorityBelowNormal,
		.stack_size = 2048U,
	};
	unsigned spins;

	if( osThreadNew( hi_body, 0, &hi_attr ) == 0 ) {
		board_case_fail( "hi task create failed" );
		return;
	}
	if( osThreadNew( lo_body, 0, &lo_attr ) == 0 ) {
		board_case_fail( "lo task create failed" );
		return;
	}
	for( spins = 0; spins < SCHED_SPINS; spins++ ) {
		if( sched_hi_done != 0U && sched_lo_done != 0U ) {
			return;
		}
		osDelay( 10U );
	}
	board_case_fail( "two-task scheduling stalled" );
}

BOARD_TEST( "freertos-sched", freertos_sched );
