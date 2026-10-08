/**
 * @file    main.c
 * @brief   ThreadX board regression image: base + shell + the case table.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); ref the shell image it
 *          evolves from (tests/board/cherrysh-threadx-rk3568-smoke/main.c:58-85)
 *          for the same call shape, plus the shared runner thread
 *          (tests/board/common/board_suite.c) whose table carries the cases.
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, initialises the kernel facade, creates the runner
 * thread and starts the scheduler. The runner prints the judgment channel; the
 * shell REPL is created by the suite's first case, so the host-sent interaction
 * steps land on a live console.
 */

#include <stdint.h>

#include "board_suite.h"
#include "os/cmsis2/cmsis2.h"

#include "arch_asm.h"
#include "uart.h"

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

static const osThreadAttr_t runner_thread_attr = {
	.name = "regress",
	.priority = osPriorityNormal,
	.stack_size = 4096U,
};

/* Unrecoverable bring-up failure: report and park (a failure that cannot
 * speak is indistinguishable from a dead board). */
static void reg_fatal( const char *what )
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

int main( void )
{
	uart_early_init();

	uart_early_puts( "threadx-rk3568-regression bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		reg_fatal( "kernel init failed" );
	}

	if( osThreadNew( board_suite_run, 0, &runner_thread_attr ) == 0 ) {
		reg_fatal( "runner thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		reg_fatal( "scheduler start failed" );
	}

	/* Not reached: the kernel does not return. */
	reg_fatal( "scheduler returned" );
	return 0;
}
