/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * generic_timer.h - CM4 stand-in for the AArch64 counter accessors used by
 * the shared log sink.  The DWT cycle counter is the monotonic counter on this
 * core; rtos_clock_init() enables it before the first stamped line.
 */

#ifndef FREEWIFICAM_ARMV7M_GENERIC_TIMER_H
#define FREEWIFICAM_ARMV7M_GENERIC_TIMER_H

#include <stdint.h>

extern uint32_t SystemCoreClock;

static inline uint64_t gtimer_count_get(void)
{
	return *(volatile uint32_t *)0xE0001004UL;
}

static inline uint32_t gtimer_freq_get(void)
{
	return SystemCoreClock;
}

#endif
