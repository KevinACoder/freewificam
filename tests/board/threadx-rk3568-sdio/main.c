/**
 * @file    main.c
 * @brief   ThreadX board SDIO image: base + shell + the imported SDIO stack over
 *          this board's controller + the case table.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); the shape is the driver
 *          images' (tests/board/threadx-rk3568-rtw88/main.c) with the wireless
 *          chain replaced by the SDIO line's.
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, initialises the kernel facade, creates the runner
 * thread and starts the scheduler.
 *
 * The SDIO backend is not installed here: the first case installs it and brings
 * the card up, so a bring-up failure is a case verdict with its reason on the
 * console rather than a boot that stops before the judgment channel exists.
 */

#include <stdint.h>

#include "board_suite.h"
#include "os/cmsis2/cmsis2.h"

#include "arch_asm.h"
#include "uart.h"

#include "dwc_mmc.h"

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

static const osThreadAttr_t runner_thread_attr = {
	.name = "regress",
	.priority = osPriorityNormal,
	/* The SDIO bring-up runs on this thread, and it is deeper than the
	 * storage-shaped chains: the card service, the host glue and the controller
	 * driver each hold a frame across the command sequence. */
	.stack_size = 8192U,
};

/*
 * The datapath case's bring-up bisection switch, defined here because the flag
 * itself belongs to the controller driver and the cases do not carry that
 * header (sdio_cases.h says why). The wiring is the one place in this image that
 * owns both sides of that boundary.
 */
void sdio_suite_ref_data_path_set( bool on )
{
	dwc_mmc_ref_data_path_set( on );
}

/* Unrecoverable bring-up failure: report and park (a failure that cannot
 * speak is indistinguishable from a dead board). */
static void sdio_fatal( const char *what )
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

	uart_early_puts( "threadx-rk3568-sdio bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		sdio_fatal( "kernel init failed" );
	}

	if( osThreadNew( board_suite_run, 0, &runner_thread_attr ) == 0 ) {
		sdio_fatal( "runner thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		sdio_fatal( "scheduler start failed" );
	}

	/* Not reached: the kernel does not return. */
	sdio_fatal( "scheduler returned" );
	return 0;
}
