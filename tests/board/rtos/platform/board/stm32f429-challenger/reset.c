/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * reset.c - STM32F429 warm reset.
 *
 * The board's nRST is the lab's hardware reset; this is the in-image path the
 * shared app uses for RTOS_BOARD_AUTO_RESET.  AIRCR.SYSRESETREQ is the ARMv7-M
 * architectural reset request.
 */

#include <stdint.h>

#include "reset.h"

#define SCB_AIRCR (*(volatile uint32_t *)0xE000ED0CUL)

void board_system_reset(void)
{
	SCB_AIRCR = (0x5FAUL << 16) | (1UL << 2);
	for (;;) {
		__asm__ __volatile__("dsb" ::: "memory");
	}
}
