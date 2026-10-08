/**
 * @file    main.c
 * @brief   FreeRTOS board regression image: base + shell + USB + case table.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); ref the shell image
 *          (tests/board/cherrysh-rk3568-smoke/main.c:58-85) for the boot shape
 *          and the ThreadX regression image for the runner table
 *          (tests/board/threadx-rk3568-regression/main.c).
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, initialises the kernel facade, prints the
 * kernel's own identity (through the facade, so no kernel header enters the
 * application), creates the runner thread and starts the scheduler.
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
	static osVersion_t version;
	static char id[32];

	uart_early_init();

	uart_early_puts( "freertos-rk3568-regression bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		reg_fatal( "kernel init failed" );
	}

	/* The kernel's own identity, through the facade: the judge anchors on
	 * these two lines. */
	if( osKernelGetInfo( &version, id, sizeof( id ) ) == osOK ) {
		uart_early_puts( "freertos: kernel " );
		uart_early_puts( id );
		uart_early_puts( " api=" );
		uart_early_put_hex32( ( uint32_t ) version.api );
		uart_early_puts( "\n" );
	}

	if( osThreadNew( board_suite_run, 0, &runner_thread_attr ) == 0 ) {
		reg_fatal( "runner thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		reg_fatal( "scheduler start failed" );
	}

	reg_fatal( "scheduler returned" );
	return 0;
}
