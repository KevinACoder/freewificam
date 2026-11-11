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
#include <linux/ieee80211.h>
#include <linux/if_vlan.h>

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

/*
 * struct_group() must be a union, not two structs in sequence: the members it
 * names and the NAME alias have to overlap.  When the macro doubled the group,
 * struct ieee80211_hdr's seq_ctrl moved from 22 to 40 and mac80211's
 * defragment handler read two bytes past a 30-byte auth frame (2026-11-11
 * C5-open, RX_DROP_U_DEFRAG_MISMATCH).  This case is the layout lock.
 */
int kpi_case_ieee80211_hdr(void)
{
	unsigned char frame[30];
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)frame;
	unsigned int i;

	KPI_ASSERT_EQ(offsetof(struct ieee80211_hdr, seq_ctrl), 22,
		      "seq_ctrl at 22");
	KPI_ASSERT_EQ(offsetof(struct ieee80211_hdr, addr4), 24,
		      "addr4 at 24");
	KPI_ASSERT_EQ(sizeof(struct ieee80211_hdr), 30,
		      "header is 30 bytes");
	KPI_ASSERT_EQ(offsetof(struct ieee80211_hdr, addrs), 4,
		      "the named group overlaps addr1");
	KPI_ASSERT_EQ(offsetof(struct vlan_ethhdr, h_vlan_proto), 12,
		      "vlan_ethhdr's group is 12 bytes");

	for (i = 0; i < sizeof(frame); i++)
		frame[i] = 0;
	frame[0] = 0xb0;	/* auth, no flags */
	frame[22] = 0x40;
	frame[23] = 0x91;
	KPI_ASSERT_EQ(le16_to_cpu(hdr->seq_ctrl), 0x9140,
		      "seq_ctrl reads the on-air bytes");
	KPI_ASSERT_EQ(ieee80211_get_sn(hdr), 0x914, "sequence number");
	KPI_ASSERT_EQ((unsigned long)(void *)hdr->addrs.addr1,
		      (unsigned long)(void *)(frame + 4),
		      "the named group points at the same bytes");
	return KPI_PASS;
}
