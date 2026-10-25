/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   clock_host.c
 * @brief  Host tier clock: CLOCK_MONOTONIC.
 */

#define _POSIX_C_SOURCE 200809L

#include <time.h>

#include "clock.h"

static struct timespec clock_origin;
static int clock_started;

void rtos_clock_init(void)
{
	if (clock_started) {
		return;
	}
	(void)clock_gettime(CLOCK_MONOTONIC, &clock_origin);
	clock_started = 1;
}

uint64_t rtos_clock_us(void)
{
	struct timespec now;
	uint64_t us;

	if (!clock_started) {
		rtos_clock_init();
	}

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0u;
	}

	us = (uint64_t)(now.tv_sec - clock_origin.tv_sec) * 1000000u;
	if (now.tv_nsec >= clock_origin.tv_nsec) {
		us += (uint64_t)(now.tv_nsec - clock_origin.tv_nsec) / 1000u;
	} else {
		us -= 1000000u;
		us += 1000000u - (uint64_t)(clock_origin.tv_nsec - now.tv_nsec) / 1000u;
	}

	return us;
}