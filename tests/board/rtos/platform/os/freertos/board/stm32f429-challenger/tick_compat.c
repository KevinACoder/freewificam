/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * tick_compat.c - the two os_tick_* queries the shared log sink uses, served
 * by the kernel tick on this board.  The CM4F port owns SysTick; there is no
 * separate generic-timer tick layer in an F429 image.
 */

#include "FreeRTOS.h"
#include "task.h"

#include "tick.h"

uint32_t os_tick_freq_hz(void)
{
	return (uint32_t)configTICK_RATE_HZ;
}

uint32_t os_tick_get_count32(void)
{
	return (uint32_t)xTaskGetTickCount();
}
