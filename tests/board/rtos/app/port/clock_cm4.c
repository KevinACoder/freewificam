/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * clock_cm4.c - the app tier's monotonic microsecond clock on Cortex-M4.
 *
 * The DWT cycle counter is the only free-running 32-bit counter available
 * before and after the scheduler starts.  At 168 MHz it wraps every ~25.6 s;
 * the tick case's measurements are far shorter, and the overflow accumulator
 * below still gives a monotonic 64-bit reading across a wrap.
 */

#include <stdint.h>

#include "clock.h"

#define CoreDebug_DEMCR (*(volatile uint32_t *)0xE000EDFCUL)
#define DWT_CTRL        (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT      (*(volatile uint32_t *)0xE0001004UL)

#define DEMCR_TRCENA    (1UL << 24)
#define DWT_CTRL_CYCCNTENA (1UL << 0)

extern uint32_t SystemCoreClock;

static uint32_t last_cycles;
static uint64_t wrap_cycles;

void rtos_clock_init(void)
{
	CoreDebug_DEMCR |= DEMCR_TRCENA;
	DWT_CYCCNT = 0;
	DWT_CTRL |= DWT_CTRL_CYCCNTENA;
	last_cycles = 0;
	wrap_cycles = 0;
}

uint64_t rtos_clock_us(void)
{
	uint32_t now = DWT_CYCCNT;

	if (now < last_cycles) {
		wrap_cycles += (1ULL << 32);
	}
	last_cycles = now;

	uint64_t cycles = wrap_cycles + now;
	uint32_t hz = (SystemCoreClock != 0) ? SystemCoreClock : 16000000UL;

	return (cycles / hz) * 1000000ULL +
	       ((cycles % hz) * 1000000ULL) / hz;
}
