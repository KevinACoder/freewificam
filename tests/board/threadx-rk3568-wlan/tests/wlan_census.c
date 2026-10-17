/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_census.c
 * @brief   The image's census face: the census clock's board-side
 *          implementation and the snapshot the dataplane case and the console
 *          command call.
 * @author  zhugengyu
 * @date    19.10.2026
 * @design  the same split the RTL8189FTV image draws
 *          (tests/board/threadx-rk3568-rtl8189/tests/wlan8189_census.c:50-58):
 *          the timestamps come from the architectural counter, and the portable
 *          linuxkpi layers must not touch an aarch64 register, so
 *          lkpi_census_now_ns() is implemented here, with the image that
 *          defines CONFIG_LKPI_CENSUS.
 *
 *          The clock is generic_timer.c's own entry point, which brackets the
 *          counter read with ISB on both sides (arch/aarch64/generic_timer.c:27)
 *          - a bare `mrs cntvct_el0` can be reordered against the work it is
 *          supposed to bracket, and an instrument that mis-times its own
 *          subjects answers the wrong question.
 *
 *          The snapshot is gated on the same macro the instruments are: with it
 *          off the image still builds and runs, it just has no census to print
 *          (and the suite's census anchors then go red, which is the honest
 *          outcome - no instrument, no evidence).
 */

#include <stdint.h>

#include "generic_timer.h"

#include "lkpi_census.h"

#include "wlan_census.h"

uint64_t
lkpi_census_now_ns(void)
{
	uint64_t freq = (uint64_t)gtimer_freq_get();

	if (freq == 0u)
		return 0u;
	return (gtimer_count_get() * 1000000000ULL) / freq;
}

void
wlan_census_reset(void)
{
#ifdef CONFIG_LKPI_CENSUS
	lkpi_usb_census_reset();
	lkpi_usb_cherryusb_census_reset();
	lkpi_80211_census_reset();
	wlan_seam_tx_census_reset();
#endif
}

void
wlan_census_print(void)
{
#ifdef CONFIG_LKPI_CENSUS
	lkpi_usb_census_print();
	lkpi_usb_cherryusb_census_print();
	lkpi_80211_census_print();
	wlan_seam_tx_census_print();
#endif
}
