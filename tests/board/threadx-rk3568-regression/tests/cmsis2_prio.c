/**
 * @file    cmsis2_prio.c
 * @brief   Case cmsis2-prio: the CMSIS priority band maps onto the kernel.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case body of the regression table (tests/board/common/board_suite.h);
 *          the mapping anchor (os/cmsis2 -> kernel priority) is printed by the
 *          backend on every osPriorityNormal thread creation
 *          (os/threadx/cmsis2_threadx.c:97-115), so creating one probe thread
 *          with that band is what the judge's anchor needs.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"

static void probe_body( void *argument )
{
	(void)argument;
	for( ;; ) {
		osDelay( 1000U );
	}
}

static void cmsis2_prio( void )
{
	static const osThreadAttr_t probe_attr = {
		.name = "probe",
		.priority = osPriorityNormal,
		.stack_size = 1024U,
	};

	if( osThreadNew( probe_body, 0, &probe_attr ) == 0 ) {
		board_case_fail( "probe thread create failed" );
	}
}

BOARD_TEST( "cmsis2-prio", cmsis2_prio );
