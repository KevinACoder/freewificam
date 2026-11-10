/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   jiffies.c
 * @brief  The Linux clock: a real variable advanced by the RTOS tick, and the
 *         conversions/comparisons written against HZ == 1000.
 *
 * The case exists because S0 run-19 showed the old spelling (#define jiffies
 * lkpi_jiffies()) could not coexist with struct ieee80211_hw's `jiffies`
 * member.  What is pinned here is the replacement's *behaviour*: the tick hook
 * advances the variable while the caller only sleeps (no kernel-API call in
 * between), and the wrap-safe comparisons and HZ conversions answer as the
 * interface's do.  The variable's absolute value is deliberately not asserted:
 * the host's tick thread started before this task, and the board starts its
 * own counter at boot.
 */
#include <stdint.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"

#include "kpi_test.h"

int kpi_case_jiffies_tick(void)
{
	unsigned long before, after, via_fn;
	unsigned long delta;

	_Static_assert(HZ == 1000, "the host case pins the 1 kHz conversion");

	/* 1. The tick hook advances the clock while this task only sleeps.  If
	 *    the platform hook is missing or vApplicationTickHook never runs,
	 *    the variable stays put and this fails - which is the point. */
	before = jiffies;
	kpi_delay_ms(20);
	after = jiffies;
	delta = after - before;
	KPI_ASSERT(delta >= 1ul, "the tick hook did not advance jiffies");
	KPI_ASSERT(delta <= 200ul, "jiffies advanced too far for a 20 ms sleep");

	/* 2. The accessor reads the same variable the stack reads. */
	before = jiffies;
	via_fn = lkpi_jiffies();
	KPI_ASSERT(via_fn >= before, "lkpi_jiffies() went backwards");
	KPI_ASSERT(via_fn - before <= 1ul, "lkpi_jiffies() is a different clock");

	/* 3. HZ == 1000 conversions, in both directions. */
	KPI_ASSERT_EQ(msecs_to_jiffies(1), 1, "1 ms");
	KPI_ASSERT_EQ(msecs_to_jiffies(1000), 1000, "1 s");
	KPI_ASSERT_EQ(msecs_to_jiffies(0), 0, "0 ms");
	KPI_ASSERT_EQ(usecs_to_jiffies(1000), 1, "1 ms in usecs");
	KPI_ASSERT_EQ(jiffies_to_msecs(2000), 2000, "2 s in ms");
	KPI_ASSERT_EQ(jiffies_to_usecs(1), 1000, "1 tick in usecs");

	/* 4. Wrap-safe comparisons.  Synthetic values only: writing the global
	 *    would race the host's tick thread, and these two answers are what
	 *    the interface's signed-difference arithmetic is for. */
	KPI_ASSERT(time_after(5ul, (unsigned long)-5l), "time_after across wrap");
	KPI_ASSERT(time_before((unsigned long)-5l, 5ul), "time_before across wrap");
	KPI_ASSERT(time_after_eq(5ul, 5ul), "time_after_eq equal");
	KPI_ASSERT(!time_after(5ul, 6ul), "time_after must be strict");
	KPI_ASSERT(time_is_before_jiffies(jiffies - 100ul),
		   "time_is_before_jiffies(now - 100)");
	KPI_ASSERT(time_is_after_jiffies(jiffies + 100ul),
		   "time_is_after_jiffies(now + 100)");

	return KPI_PASS;
}
