/*
 * @file   generic_timer.h
 * @brief  Host spelling: the generic counter the console stamp reads.
 *
 * The outlet suite drives the counter itself (host_shim.c), so the stamp is a
 * known function of the value the case set. The board's header is
 * arch/aarch64/generic_timer.h.
 */

#ifndef ULOG_LEVEL_TEST_GENERIC_TIMER_H
#define ULOG_LEVEL_TEST_GENERIC_TIMER_H

#include <stdint.h>

uint32_t gtimer_freq_get(void);
uint64_t gtimer_count_get(void);

#endif /* ULOG_LEVEL_TEST_GENERIC_TIMER_H */
