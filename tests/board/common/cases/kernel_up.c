/**
 * @file    kernel_up.c
 * @brief   Case kernel-up: the kernel clock is alive.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case body of the regression table (tests/board/common/board_suite.h);
 *          the fixed anchors this case is judged by are printed by the board
 *          glue and the board heap during kernel entry
 *          (os/threadx/board/rk3568-itx/tx_glue.c:111-132), so the body itself
 *          only has to prove the clock advances.
 *
 * The tick is armed inside the kernel's pre-scheduler window; the first
 * interrupt may still be in flight when this thread first runs, so the check
 * waits (bounded) instead of sampling once.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"

#define KERNEL_UP_SPINS 100U

static void kernel_up( void )
{
	unsigned spins;

	for( spins = 0; spins < KERNEL_UP_SPINS && osKernelGetTickCount() == 0U; spins++ ) {
		osDelay( 10U );
	}
	if( osKernelGetTickCount() == 0U ) {
		board_case_fail( "tick count never advanced" );
		return;
	}
	if( osKernelGetSysTimerFreq() == 0U ) {
		board_case_fail( "sys timer freq is zero" );
	}
}

BOARD_TEST( "kernel-up", kernel_up );
