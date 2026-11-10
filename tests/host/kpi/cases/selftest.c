/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   selftest.c
 * @brief  The bench's negative control: without --break it passes, with
 *         --break it fails.  tools/rtos/run-host-test.sh runs it both ways, so
 *         "all cases pass" cannot be a harness that silently never asserts.
 */
#include "kpi_test.h"

int kpi_case_selftest(void)
{
	KPI_ASSERT(kpi_break == 0, "injected failure (--break)");
	return KPI_PASS;
}
