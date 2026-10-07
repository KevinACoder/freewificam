/**
 * @file    main.c
 * @brief   CherrySH on the ThreadX board line: the console shell over the polled
 *          UART, with the CMSIS-RTOS2 backend's board form underneath.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); ref the FreeRTOS-based
 *          sibling tests/board/cherrysh-rk3568-smoke/main.c:22-82 (the same
 *          call shape on the other board base).
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, initialises the kernel facade, wires the shell
 * and starts the scheduler. On this base the kernel entry goes through the
 * facade's board form (the SPSel hand-over stub behind osKernelStart), and the
 * tick is armed by the board glue inside the kernel's single pre-scheduler
 * window - so nothing here touches either.
 *
 * What the board test sees: the shell's prompt, the echo of typed characters,
 * the builtin command list from `help`, completion of `cl` to `clear`, and the
 * prompt returning after `exit` (the board's exit resets the session - the
 * image keeps running).
 */

#include <stdint.h>

#include "os/cmsis2/cmsis2.h"
#include "ports/cherrysh/shell_port.h"

#include "arch_asm.h"
#include "uart.h"

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

static const osThreadAttr_t shell_thread_attr = {
	.name = "shell",
	.priority = osPriorityNormal,
	.stack_size = 4096U,
};

/* Unrecoverable bring-up failure: report and park (a failure that cannot
 * speak is indistinguishable from a dead board). */
static void shell_fatal( const char *what )
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

	uart_early_puts( "cherrysh-threadx-rk3568 bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		shell_fatal( "kernel init failed" );
	}

	if( shell_port_init() != 0 ) {
		shell_fatal( "shell init failed" );
	}

	if( osThreadNew( shell_port_repl_task, 0, &shell_thread_attr ) == 0 ) {
		shell_fatal( "shell thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		shell_fatal( "scheduler start failed" );
	}

	/* Not reached: the kernel does not return. */
	shell_fatal( "scheduler returned" );
	return 0;
}
