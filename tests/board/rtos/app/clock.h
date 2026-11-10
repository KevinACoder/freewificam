/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   clock.h
 * @brief  The tier's monotonic microsecond clock, behind one call.
 *
 * The board tier reads the AArch64 system counter (CNTVCT/CNTFRQ, 24 MHz on
 * this SoC); the host tier reads CLOCK_MONOTONIC.  The conformance cases and
 * the timer jitter measurement take their readings through this call only, so
 * the same case source runs on both tiers.
 */

#ifndef RTOS_APP_CLOCK_H
#define RTOS_APP_CLOCK_H

#include <stdint.h>

/* One-time initialisation of the tier's clock.  Idempotent; safe before the
 * kernel starts (the board tier programs nothing, the counter is free-run). */
void rtos_clock_init(void);

/* Microseconds since an arbitrary but monotonic origin.  64-bit: the board
 * counter would wrap after ~24 000 years at 24 MHz divided by 1e6. */
uint64_t rtos_clock_us(void);

#endif /* RTOS_APP_CLOCK_H */