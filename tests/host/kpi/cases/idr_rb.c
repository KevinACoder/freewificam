/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   idr_rb.c
 * @brief  The idr/rbtree cases the bench cannot run yet.
 *
 * mac80211's ack table is an idr and minstrel's rate table is an rbtree; the
 * component has neither implementation (only the vendored declarations), so
 * there is nothing to pin.  The skip is explicit and lands as a gap line -
 * a silent absence would read as coverage.
 */
#include "kpi_test.h"

int kpi_case_idr_rb(void)
{
	KPI_SKIP("no idr/rbtree implementation in this round; gap recorded in docs/LINUXKPI-GAP.md");
}
