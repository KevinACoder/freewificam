/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   macros.c
 * @brief  The compile-time and bit vocabulary the S1a faces added: memset_after,
 *         BIT_MASK, the pinned netdev feature positions, min/max over runtime
 *         values (the compiletime_assert fix), and the field helpers.
 */
#include <stdint.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"

#include <linux/string.h>
#include <linux/bitfield.h>
#include <linux/minmax.h>
#include <linux/netdevice.h>

#include "kpi_test.h"

struct padded {
	uint8_t	head;
	uint8_t	pad[3];
	uint32_t tail;
};

int kpi_case_macros(void)
{
	struct padded p;
	unsigned long long v;
	uint8_t b;

	p.head = 0x11;
	p.pad[0] = p.pad[1] = p.pad[2] = 0x22;
	p.tail = 0x33333333u;
	memset_after(&p, 0, head);
	KPI_ASSERT_EQ(p.head, 0x11, "memset_after leaves the member");
	KPI_ASSERT_EQ(p.pad[0] | p.pad[1] | p.pad[2], 0, "padding cleared");
	KPI_ASSERT_EQ(p.tail, 0, "tail cleared");

	KPI_ASSERT_EQ(BIT_MASK(0), 1ul, "BIT_MASK(0)");
	KPI_ASSERT_EQ(BIT_MASK(63), 1ull << 63, "BIT_MASK(63)");
	KPI_ASSERT_EQ(BIT_MASK(64), 1ul, "BIT_MASK(64) wraps");
	KPI_ASSERT_EQ(BIT_MASK(65), 2ul, "BIT_MASK(65)");

	/* the pinned netdev feature positions (netdev_features.h enum) */
	v = NETIF_F_HW_CSUM;
	KPI_ASSERT_EQ(v, 1ull << 3, "HW_CSUM at bit 3");
	v = NETIF_F_FRAGLIST;
	KPI_ASSERT_EQ(v, 1ull << 6, "FRAGLIST at bit 6");
	v = NETIF_F_HW_VLAN_CTAG_TX;
	KPI_ASSERT_EQ(v, 1ull << 7, "CTAG_TX at bit 7");
	v = NETIF_F_HW_VLAN_STAG_TX;
	KPI_ASSERT_EQ(v, 1ull << 45, "STAG_TX at bit 45");
	v = NETIF_F_HW_TC;
	KPI_ASSERT_EQ(v, 1ull << 49, "HW_TC at bit 49");

	/* min()/max() over runtime values: this is the path BUILD_BUG_ON used to
	 * reject when compiletime_assert was a _Static_assert */
	{
		int a = 3, b = 7;

		KPI_ASSERT_EQ(min(a, b), 3, "min runtime");
		KPI_ASSERT_EQ(max(a, b), 7, "max runtime");
	}
	KPI_ASSERT(statically_true(1), "statically_true(1)");
	KPI_ASSERT(!statically_true(0), "statically_true(0)");

	KPI_ASSERT_EQ(FIELD_PREP(0xf0u, 0xau), 0xa0u, "FIELD_PREP");
	KPI_ASSERT_EQ(FIELD_GET(0xf0u, 0xa0u), 0xau, "FIELD_GET");
	b = 0x05;
	u8_replace_bits(b, 0x3u, 0x0fu);
	KPI_ASSERT_EQ(b, 0x03u, "u8_replace_bits");
	return KPI_PASS;
}
