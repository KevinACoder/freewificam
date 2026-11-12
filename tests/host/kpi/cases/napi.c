/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   napi.c
 * @brief  The NAPI budget contract: a poll that returns its whole budget must
 *         be followed by another poll, not by a cleared SCHED latch.
 *
 * The 2026-11-12 AP transmit stall's mechanical verdict was `napi-budget`:
 * the receive poll returned exactly the weight (64), iwlwifi's
 * iwl_pcie_napi_poll() therefore did not re-enable CSR_INT_MASK, and the port's
 * single-shot scheduler cleared SCHED and never asked again - the device kept
 * beaconing, the host was deaf.  This case is the host pin for the fixed
 * contract: the drain round is what the driver re-enables the interrupt on.
 *
 * The fake poll has iwlwifi's shape: it returns the budget while work remains,
 * and on the drain round it calls napi_complete_done() and returns less than
 * the budget.  A second scenario proves the re-poll bound: a poll that never
 * drains stops at the cap (the counter records it) instead of spinning.
 */
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/netdevice.h>

#include "linux80211/diag.h"
#include "kpi_test.h"

static int	napi_fake_calls;
static int	napi_fake_budget_calls;
static int	napi_fake_reenabled;

static int
napi_fake_poll(struct napi_struct *n, int budget)
{
	napi_fake_calls++;
	if (napi_fake_calls <= napi_fake_budget_calls)
		return budget;
	napi_fake_reenabled++;
	(void)napi_complete_done(n, 0);
	return 0;
}

static void
napi_reset(struct napi_struct *n)
{
	(void)memset(n, 0, sizeof(*n));
	n->poll = napi_fake_poll;
	n->weight = NAPI_POLL_WEIGHT;
	napi_fake_calls = 0;
	napi_fake_budget_calls = 0;
	napi_fake_reenabled = 0;
}

int
kpi_case_napi_budget(void)
{
	struct lkpi_napi_health a, b;
	struct napi_struct n;

	/* Three budget rounds, then the drain: four polls, three hits, one
	 * complete, and the drain round is the one that re-armed the vector. */
	napi_reset(&n);
	napi_fake_budget_calls = 3;
	lkpi_napi_health_get(&a);
	KPI_ASSERT(napi_schedule_prep(&n), "prep accepts the first schedule");
	__napi_schedule(&n);
	lkpi_napi_health_get(&b);
	KPI_ASSERT_EQ(napi_fake_calls, 4, "polls after three budget rounds");
	KPI_ASSERT_EQ(napi_fake_reenabled, 1, "drain rounds");
	KPI_ASSERT_EQ(b.polls - a.polls, 4, "polls counted");
	KPI_ASSERT_EQ(b.budget_hits - a.budget_hits, 3, "budget hits counted");
	KPI_ASSERT_EQ(b.completes - a.completes, 1, "one completion");
	KPI_ASSERT_EQ(b.rounds_exhausted - a.rounds_exhausted, 0,
		      "the cap was not reached");
	KPI_ASSERT_EQ(b.last_ret, 0, "the last poll returned less than the weight");

	/* A poll that never drains stops at the bound and says so; it must not
	 * spin the interrupt thread forever and must not claim a completion. */
	napi_reset(&n);
	napi_fake_budget_calls = 1000;
	lkpi_napi_health_get(&a);
	KPI_ASSERT(napi_schedule_prep(&n), "prep accepts the second schedule");
	__napi_schedule(&n);
	lkpi_napi_health_get(&b);
	KPI_ASSERT(napi_fake_calls >= 8, "the re-poll actually ran more rounds");
	KPI_ASSERT_EQ(napi_fake_reenabled, 0, "an undrained poll never re-arms");
	KPI_ASSERT_EQ(b.completes - a.completes, 0, "no completion");
	KPI_ASSERT_EQ(b.rounds_exhausted - a.rounds_exhausted, 1,
		      "the cap is recorded");
	return KPI_PASS;
}
