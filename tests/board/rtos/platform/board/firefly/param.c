/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   param.c
 * @brief  Board tick instance (the one place the tick's numbers live) -
 *         firefly family.
 *
 * Source is the EL1 virtual timer (CNTV, PPI 27 - every firefly device tree
 * lists the arm,armv8-timer's PPI 11 line there); the rate matches this
 * image's tick (1000 Hz, the ThreadX key CONFIG_TX_TICKS_PER_SECOND; the
 * runtime value OS_Tick_Setup installs is what the kernel and the stamps
 * see). A version whose timer wiring ever differs overrides this file with
 * its own param.c; today every firefly SoC shares this one.
 */

#include "tick.h"

const struct os_tick_config board_tick_config = {
	.freq_hz = 1000u,
	.intid = 27u,
	.source = OS_TICK_SOURCE_VIRT,
};
