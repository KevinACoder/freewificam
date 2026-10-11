/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * generic_timer.c - the AArch64 Generic Timer (System Counter + EL1
 * physical/virtual timers) register face. The single place raw
 * CNTP/CNTV/cntfrq register access lives: bare register pokes must not be
 * scattered through component/port code.
 *
 * CNTFRQ is the counter frequency (read-only, invariant). CNTP and CNTV are
 * free-running interval timers; each is a down-counter whose level
 * interrupt line clears when TVAL is rewritten, so every setter here ends
 * with an ISB to order the write against the line.
 */

#include <stdint.h>

#include "generic_timer.h"

uint32_t
gtimer_freq_get(void)
{
	uint64_t freq;

	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
	return (uint32_t)freq;
}

uint64_t
gtimer_count_get(void)
{
	uint64_t count;

	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(count));
	return count;
}

void
gtimer_isb(void)
{
	__asm__ __volatile__("isb" ::: "memory");
}

void
gtimer_ptimer_tval_set(uint32_t ticks)
{

	__asm__ __volatile__("msr cntp_tval_el0, %0" ::"r"(ticks));
	__asm__ __volatile__("isb" ::: "memory");
}

uint32_t
gtimer_ptimer_tval_get(void)
{
	uint32_t value;

	__asm__ __volatile__("mrs %0, cntp_tval_el0" : "=r"(value));
	return value;
}

void
gtimer_ptimer_ctl_set(uint32_t ctl)
{

	__asm__ __volatile__("msr cntp_ctl_el0, %0" ::"r"(ctl));
	__asm__ __volatile__("isb" ::: "memory");
}

uint32_t
gtimer_ptimer_ctl_get(void)
{
	uint32_t value;

	__asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(value));
	return value;
}

void
gtimer_vtimer_tval_set(uint32_t ticks)
{

	__asm__ __volatile__("msr cntv_tval_el0, %0" ::"r"(ticks));
	__asm__ __volatile__("isb" ::: "memory");
}

uint32_t
gtimer_vtimer_tval_get(void)
{
	uint32_t value;

	__asm__ __volatile__("mrs %0, cntv_tval_el0" : "=r"(value));
	return value;
}

void
gtimer_vtimer_ctl_set(uint32_t ctl)
{

	__asm__ __volatile__("msr cntv_ctl_el0, %0" ::"r"(ctl));
	__asm__ __volatile__("isb" ::: "memory");
}

uint32_t
gtimer_vtimer_ctl_get(void)
{
	uint32_t value;

	__asm__ __volatile__("mrs %0, cntv_ctl_el0" : "=r"(value));
	return value;
}
