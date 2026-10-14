/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    main.c
 * @brief   ThreadX board eMMC image: base + shell + the SD/MMC stack's MMC card
 *          protocol layer, this project's port and host glue, the board's eMMC
 *          controller driver, the block-device seam and FatFs, plus the case
 *          table.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); the shape is the
 *          PCIe/NVMe image's (tests/board/threadx-rk3568-pcie/main.c) with the
 *          storage line replaced by the eMMC one.
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, initialises the kernel facade, creates the runner
 * thread and starts the scheduler.
 *
 * The card bring-up is not done here: the first case does it, so a bring-up
 * failure is a case verdict with its reason on the console rather than a boot
 * that stops before the judgment channel exists.
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
	/* The card bring-up and the 1 GiB burn-in run on this thread; the FatFs
	 * paths and the stack's card initialization each hold a frame across
	 * their sequences, and the burn-in adds its file object. */
	.stack_size = 8192U,
};

/* Unrecoverable bring-up failure: report and park (a failure that cannot
 * speak is indistinguishable from a dead board). */
static void emmc_fatal( const char *what )
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

	uart_early_puts( "threadx-rk3568-emmc bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		emmc_fatal( "kernel init failed" );
	}

	if( osThreadNew( board_suite_run, 0, &runner_thread_attr ) == 0 ) {
		emmc_fatal( "runner thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		emmc_fatal( "scheduler start failed" );
	}

	/* Not reached: the kernel does not return. */
	emmc_fatal( "scheduler returned" );
	return 0;
}
