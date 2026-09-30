/*
 * @file   param.c
 * @brief  Board tick instance (the one place the tick's numbers live).
 *
 * Default source is the EL1 virtual timer (CNTV, PPI 27); the build key
 * CONFIG_TICK_SOURCE=phys_ns selects the EL1 non-secure physical timer
 * (CNTP, PPI 30) - SPEC v3 §3.5.3 / §4/R2 判据 5's adjudication path.
 */

#include "tick.h"

#ifdef TICK_SOURCE_PHYS_NS
const struct os_tick_config board_tick_config = {
	.freq_hz = 100u,
	.intid = 30u,
	.source = OS_TICK_SOURCE_PHYS_NS,
};
#else
const struct os_tick_config board_tick_config = {
	.freq_hz = 100u,
	.intid = 27u,
	.source = OS_TICK_SOURCE_VIRT,
};
#endif
