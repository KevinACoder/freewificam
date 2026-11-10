/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   clock_board.c
 * @brief  Board tier clock: the AArch64 system counter.
 *
 * The counter is the same time base the tick is armed from (CNTV is derived
 * from it), so a tick-vs-wall comparison measures the kernel's tick against
 * the counter it runs on - not against a second oscillator.
 */

#include "clock.h"

#include "generic_timer.h"

void rtos_clock_init(void)
{
	/* The counter free-runs from reset; nothing to program here. */
}

uint64_t rtos_clock_us(void)
{
	uint32_t freq = gtimer_freq_get();
	uint64_t count = gtimer_count_get();

	if (freq == 0u) {
		return 0u;
	}

	/* Split to keep both products inside 64 bits: freq is 24 MHz on this
	 * SoC, so count/freq is whole seconds and the remainder is < freq. */
	return (count / freq) * 1000000u +
	       ((count % freq) * 1000000u) / freq;
}