/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   main.c
 * @brief  Board entry: console, kernel facade, the runner thread, scheduler.
 *
 * Boot chain: start.S has normalised to EL1 and enabled the MMU; this file
 * brings the polled console up, prints the two banner lines the board runner
 * keys on, initialises the kernel through the CMSIS-RTOS2 facade, creates the
 * runner thread and starts the scheduler.  The backend is selected by the
 * build (CMSIS2_BACKEND_FREERTOS / _THREADX) and this file names no kernel
 * header: the same source is both images.
 */

#include <stdint.h>

#include "os/cmsis2/cmsis2.h"

#include "arch_asm.h"
#include "uart.h"

#include "cases.h"
#include "clock.h"
#include "runner.h"

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

#ifndef RTOS_BACKEND_NAME
#define RTOS_BACKEND_NAME "unknown"
#endif

#ifndef RTOS_KERNEL_NAME
#define RTOS_KERNEL_NAME "unknown"
#endif

/* Unrecoverable bring-up failure: report and park.  A failure that cannot
 * speak is indistinguishable from a dead board. */
static void fatal(const char *what)
{
	uart_early_puts("rtos: fatal ");
	uart_early_puts(what);
	uart_early_puts("\n");
	uart_early_puts("rtos: parked; power-cycle to recover\n");

	ARCH_DAIF_SET(0xf);
	for (;;) {
		ARCH_WFE();
	}
}

int main(void)
{
	uart_early_init();
	rtos_clock_init();

	uart_early_puts("rtos: banner backend=" RTOS_BACKEND_NAME
			" kernel=" RTOS_KERNEL_NAME
			" build=" FW_BUILD_TAG "\n");

	if (osKernelInitialize() != osOK) {
		fatal("kernel init failed");
	}

#if defined(RTOS_CASE_WLAN)
	/* The vendor stack's linuxkpi services, created before the scheduler just
	 * as in the reference image; the case itself runs the module_init and the
	 * SDIO bring-up once the runner is scheduled. */
	case_wlan_prepare();
#endif

	uart_early_puts("rtos: kernel ready tick=");
	{
		uint32_t freq = osKernelGetTickFreq();
		char buf[12];
		int i = 0;

		if (freq == 0u) {
			uart_early_putc('0');
		}
		while ((freq != 0u) && (i < (int)sizeof(buf))) {
			buf[i++] = (char)('0' + (freq % 10u));
			freq /= 10u;
		}
		while (i > 0) {
			uart_early_putc(buf[--i]);
		}
	}
	uart_early_puts("Hz\n");

	if (rtos_runner_start() != 0) {
		fatal("runner thread create failed");
	}

	if (osKernelStart() != osOK) {
		fatal("scheduler start failed");
	}

	fatal("scheduler returned");
	return 0;
}