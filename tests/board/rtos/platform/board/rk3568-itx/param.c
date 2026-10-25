/*
 * @file   param.c
 * @brief  Board tick instance (the one place the tick's numbers live).
 *
 * Default source is the EL1 virtual timer (CNTV, PPI 27); the build key
 * TICK_SOURCE_PHYS_NS selects the EL1 non-secure physical timer (CNTP,
 * PPI 30) - the adjudication path kept for the rounds that need an
 * independent line. The rate matches this image's tick (1000 Hz, the
 * ThreadX key CONFIG_TX_TICKS_PER_SECOND; the runtime value OS_Tick_Setup
 * installs is what the kernel and the stamps see).
 */

#include "tick.h"

#ifdef TICK_SOURCE_PHYS_NS
const struct os_tick_config board_tick_config = {
	.freq_hz = 1000u,
	.intid = 30u,
	.source = OS_TICK_SOURCE_PHYS_NS,
};
#else
const struct os_tick_config board_tick_config = {
	.freq_hz = 1000u,
	.intid = 27u,
	.source = OS_TICK_SOURCE_VIRT,
};
#endif
