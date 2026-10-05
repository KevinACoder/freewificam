/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * generic_timer.h - the AArch64 Generic Timer register face. The only
 * headers components may include for counter/timer access; raw
 * cntfrq/cntp register pokes belong in generic_timer.c (and the tick
 * driver) alone.
 */

#ifndef ARCH_AARCH64_GENERIC_TIMER_H
#define ARCH_AARCH64_GENERIC_TIMER_H

#include <stdint.h>

/* the System Counter frequency in Hz (invariant, read-only) */
uint32_t gtimer_freq_get(void);

/* the current System Counter value (cntvct_el0), 64-bit */
uint64_t gtimer_count_get(void);

void gtimer_isb(void);

/* EL1 physical timer (CNTP): TVAL is the down-count in counter ticks;
 * the level line clears when TVAL is rewritten. Both setters order the
 * register write with an ISB. */
void gtimer_ptimer_tval_set(uint32_t ticks);
uint32_t gtimer_ptimer_tval_get(void);
/* CTL bit 0 = ENABLE, bit 1 = IMASK (0 = interrupt not masked) */
void gtimer_ptimer_ctl_set(uint32_t ctl);
uint32_t gtimer_ptimer_ctl_get(void);

#endif /* ARCH_AARCH64_GENERIC_TIMER_H */
