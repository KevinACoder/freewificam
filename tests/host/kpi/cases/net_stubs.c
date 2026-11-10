/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_stubs.c
 * @brief  The absent-subsystem answers the S1a faces state: the socket helpers
 *         (no socket layer), the qdisc question (no queueing discipline), and
 *         the namespace identity.
 *
 * These are not "stubs that quietly succeed": each case pins the answer the
 * port claims, so a future change to the trimming (or to a face) has a place to
 * disagree.
 */
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"

#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/sch_generic.h>

#include "kpi_test.h"

int kpi_case_net_stubs(void)
{
	struct net n;
	struct sock sk;

	memset(&n, 0, sizeof(n));
	memset(&sk, 0, sizeof(sk));

	KPI_ASSERT(net_eq(&n, &n), "same namespace");
	KPI_ASSERT(!net_eq(&n, NULL), "different namespace");
	KPI_ASSERT(to_ns_common(&n) == &n.ns, "to_ns_common is the record");

	KPI_ASSERT(sock_net(&sk) == NULL, "no socket, no namespace");
	KPI_ASSERT(!sk_requests_wifi_status(NULL), "NULL socket");
	KPI_ASSERT(!sk_requests_wifi_status(&sk), "no wifi-status flag");
	sk.sk_flags = 1ul << SOCK_WIFI_STATUS;
	KPI_ASSERT(sk_requests_wifi_status(&sk), "flag read");

	KPI_ASSERT_EQ(sk.sk_pacing_shift, 0, "pacing shift initial");
	sk_pacing_shift_update(&sk, 7);
	KPI_ASSERT_EQ(sk.sk_pacing_shift, 7, "pacing shift stored");
	sk_pacing_shift_update(&sk, 7);
	KPI_ASSERT_EQ(sk.sk_pacing_shift, 7, "idempotent");

	KPI_ASSERT(qdisc_all_tx_empty(NULL), "no qdisc, no backlog");

	return KPI_PASS;
}
