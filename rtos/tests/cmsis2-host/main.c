/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   main.c
 * @brief  Host tier entry: the same conformance case, the kernel's own host
 *         port underneath.
 *
 * The board image's main.c brings a console and a board up; here the process
 * is the console.  The kernel facade is identical, so the case source is the
 * same file - a backend difference shows up as a different verdict for the
 * same bytes.
 */

#include <stdint.h>
#include <stdio.h>

#include "os/cmsis2/cmsis2.h"

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

int main(void)
{
	printf("rtos: banner backend=%s kernel=%s build=%s\n",
	       RTOS_BACKEND_NAME, RTOS_KERNEL_NAME, FW_BUILD_TAG);

	if (osKernelInitialize() != osOK) {
		fprintf(stderr, "rtos: fatal kernel init failed\n");
		return 2;
	}

	printf("rtos: kernel ready tick=%luHz\n",
	       (unsigned long)osKernelGetTickFreq());

	if (rtos_runner_start() != 0) {
		fprintf(stderr, "rtos: fatal runner thread create failed\n");
		return 2;
	}

	if (osKernelStart() != osOK) {
		fprintf(stderr, "rtos: fatal scheduler start failed\n");
		return 2;
	}

	/* The FreeRTOS host port returns here when the runner stops the
	 * scheduler; the ThreadX host port ends the process from inside. */
	printf("rtos: host kernel returned\n");
	return 0;
}