/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   nla.c
 * @brief  The netlink attribute decoder: the half of the netlink face this
 *         port keeps real (ADR 2026-11-10-nl80211-scope, D1).
 *
 * A message is a byte stream a future control plane will hand us; the parser
 * must find the attributes, their payloads and their boundaries, and must
 * refuse a stream whose lengths do not add up.  The discard half (send) is not
 * exercised here because there is nothing to observe; that omission is the
 * ADR's D1 decision, not a coverage gap hidden by the bench.
 */
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/netlink.h>
#include <net/netlink.h>

#include "kpi_test.h"

int
kpi_case_nla(void)
{
	unsigned char buf[64];
	struct nlattr *a;
	struct nlattr *tb[4];
	uint32_t v = 0x11223344;
	uint32_t got = 0;
	int len;

	memset(buf, 0, sizeof(buf));
	a = (struct nlattr *)buf;
	a->nla_type = 1;
	a->nla_len = NLA_HDRLEN + sizeof(v);
	memcpy(nla_data(a), &v, sizeof(v));
	a = (struct nlattr *)(buf + NLA_ALIGN(a->nla_len));
	a->nla_type = 2;
	a->nla_len = NLA_HDRLEN + 2;
	memcpy(nla_data(a), "ok", 2);
	len = (int)((unsigned char *)a - buf) + NLA_ALIGN(a->nla_len);

	KPI_ASSERT_EQ(__nla_parse(tb, 3, (struct nlattr *)buf, len, NULL, 0, NULL),
		      0, "parse two attributes");
	KPI_ASSERT(tb[1] != NULL && tb[2] != NULL, "both attributes found");
	KPI_ASSERT(tb[3] == NULL, "an absent attribute is NULL");
	KPI_ASSERT_EQ(nla_type(tb[1]), 1, "attribute 1 type");
	KPI_ASSERT_EQ(nla_len(tb[1]), sizeof(v), "attribute 1 length");
	KPI_ASSERT(nla_find((struct nlattr *)buf, len, 2) == tb[2],
		   "nla_find agrees with the parse table");
	KPI_ASSERT_EQ(nla_memcpy(&got, tb[1], sizeof(got)), sizeof(got),
		      "nla_memcpy copies the payload");
	KPI_ASSERT(got == v, "payload bytes survive the copy");

	/* a truncated attribute: the parser must refuse, not read past the end */
	tb[1] = (struct nlattr *)0x1;
	KPI_ASSERT(__nla_parse(tb, 3, (struct nlattr *)buf, 6, NULL, 0, NULL) != 0,
		   "a truncated stream is refused");
	return KPI_PASS;
}
