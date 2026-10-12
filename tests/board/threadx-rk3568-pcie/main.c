/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    main.c
 * @brief   ThreadX board PCIe/NVMe image: base + shell + the DesignWare PCIe
 *          root complex, the GICv3 ITS message-interrupt domain and the NVMe
 *          block driver, plus the case table.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); the shape is the SDIO
 *          image's (tests/board/threadx-rk3568-sdio/main.c) with the SDIO line
 *          replaced by the PCIe + NVMe line.
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, initialises the kernel facade, creates the runner
 * thread and starts the scheduler.
 *
 * The PCIe bring-up is not done here: the first case does it, so a bring-up
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
	/* The PCIe/NVMe bring-up runs on this thread, and it is deep: the ITS
	 * command queue, the MSI-X programming and the NVMe probe each hold a
	 * frame across their register sequences. */
	.stack_size = 8192U,
};

/* Unrecoverable bring-up failure: report and park (a failure that cannot
 * speak is indistinguishable from a dead board). */
static void pcie_fatal( const char *what )
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

	uart_early_puts( "threadx-rk3568-pcie bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		pcie_fatal( "kernel init failed" );
	}

	if( osThreadNew( board_suite_run, 0, &runner_thread_attr ) == 0 ) {
		pcie_fatal( "runner thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		pcie_fatal( "scheduler start failed" );
	}

	/* Not reached: the kernel does not return. */
	pcie_fatal( "scheduler returned" );
	return 0;
}
