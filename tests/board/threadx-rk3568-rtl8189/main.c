/**
 * @file    main.c
 * @brief   ThreadX board RTL8189FTV image: base + shell + the SDIO bus line +
 *          the vendored Realtek SDIO vendor driver over it + the case table.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); the shape is the SDIO
 *          image's (tests/board/threadx-rk3568-sdio/main.c) with the driver's
 *          own bring-up chain added to the case table.
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, initialises the kernel facade, creates the runner
 * thread and starts the scheduler.
 *
 * The SDIO backend is not installed here: the driver registers itself through
 * the module-init channel at start-up, and the first case brings the bus and the
 * card up, so a bring-up failure is a case verdict with its reason on the
 * console rather than a boot that stops before the judgment channel exists.
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
	/* The driver's bring-up runs on this thread and is the deepest chain in the
	 * tree: the SDIO card service, the host glue and the controller each hold a
	 * frame, and the driver's own probe path - power sequence, firmware
	 * download, HAL init - runs on top of them. */
	.stack_size = 16384U,
};

/* Unrecoverable bring-up failure: report and park (a failure that cannot
 * speak is indistinguishable from a dead board). */
static void wlan8189_fatal( const char *what )
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

	uart_early_puts( "threadx-rk3568-rtl8189 bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		wlan8189_fatal( "kernel init failed" );
	}

	if( osThreadNew( board_suite_run, 0, &runner_thread_attr ) == 0 ) {
		wlan8189_fatal( "runner thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		wlan8189_fatal( "scheduler start failed" );
	}

	/* Not reached: the kernel does not return. */
	wlan8189_fatal( "scheduler returned" );
	return 0;
}
