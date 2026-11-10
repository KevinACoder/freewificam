/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   skbgeom.c
 * @brief  The sk_buff header-offset block: the interface's fields, the port's
 *         sentinel fallback, and the copy behaviour.
 *
 * S1a added the Linux mac/network/transport offsets to the shim record.  The
 * cases pin the three answers that matter: a fresh buffer is "unset" and keeps
 * the port's old geometry (the 802.11 header is the data); once a caller records
 * an offset the interface's field arithmetic takes over; and a clone carries the
 * recorded offsets (a copy that fell back to the derivation would silently
 * disagree with its source).
 */
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"

#include "kpi_test.h"

#include <linux/if_ether.h>
#include <linux/if_vlan.h>

int kpi_case_skb_geometry(void)
{
	struct sk_buff *skb, *clone;
	unsigned char *data;
	uint16_t off;
	uint16_t *fc;

	skb = lkpi_skb_alloc(128u, GFP_KERNEL);
	KPI_ASSERT(skb != NULL, "alloc");
	KPI_ASSERT(!skb_mac_header_was_set(skb), "fresh buffer has no mac header");
	KPI_ASSERT(skb_mac_header(skb) == skb->data, "fallback is the data pointer");

	KPI_ASSERT(skb_put(skb, 40u) != NULL, "put");
	off = (uint16_t)(skb->data - skb->head);
	skb_reset_mac_header(skb);
	KPI_ASSERT(skb_mac_header_was_set(skb), "reset marks it set");
	KPI_ASSERT_EQ(skb->mac_header, off, "offset is data - head");
	KPI_ASSERT(skb_mac_header(skb) == skb->data, "set accessor is the field");

	KPI_ASSERT(skb_pull(skb, 4u) != NULL, "pull");
	KPI_ASSERT_EQ(skb->mac_header, off, "pull does not move the mac offset");
	KPI_ASSERT(skb_mac_header(skb) == skb->head + off,
		   "mac header stays where it was recorded");

	/* the interface's field arithmetic, once a caller records the offset */
	skb_set_network_header(skb, 8);
	KPI_ASSERT_EQ(skb->network_header,
		      (uint16_t)(skb->data - skb->head + 8),
		      "network offset");
	KPI_ASSERT(skb_network_header(skb) ==
		   skb->head + skb->network_header, "field accessor");
	KPI_ASSERT_EQ(skb_network_offset(skb), 8, "network offset");

	clone = skb_clone(skb, GFP_KERNEL);
	KPI_ASSERT(clone != NULL, "clone");
	KPI_ASSERT_EQ(clone->mac_header, skb->mac_header, "clone mac offset");
	KPI_ASSERT_EQ(clone->network_header, skb->network_header,
		      "clone network offset");
	kfree_skb(clone);
	kfree_skb(skb);

	/* unset offsets keep the port's frame-derived answer: an 802.11 data
	 * frame's payload opens with LLC/SNAP, eight bytes after the header */
	skb = lkpi_skb_alloc(128u, GFP_KERNEL);
	KPI_ASSERT(skb != NULL, "alloc 2");
	data = skb_put(skb, 24u + 8u + 20u);
	KPI_ASSERT(data != NULL, "put frame");
	memset(data, 0, 24u + 8u + 20u);
	fc = (uint16_t *)data;
	*fc = 0x0008;			/* data frame, no QoS */
	KPI_ASSERT(skb_network_header(skb) ==
		   skb->data + 24u + 8u, "802.11 derivation");
	KPI_ASSERT_EQ(skb_network_offset(skb), 24 + 8, "derived offset");
	kfree_skb(skb);
	return KPI_PASS;
}

int kpi_case_skb_helpers(void)
{
	struct sk_buff *skb;
	unsigned char *p;
	char buf[8];

	skb = lkpi_skb_alloc(128u, GFP_KERNEL);
	KPI_ASSERT(skb != NULL, "alloc");
	p = skb_put(skb, 16u);
	KPI_ASSERT(p != NULL, "put");
	memcpy(p, "ABCDEFGHIJKLMNOP", 16);

	KPI_ASSERT_EQ(skb_headlen(skb), 16, "linear length");
	KPI_ASSERT(pskb_may_pull(skb, 16u), "may pull the whole frame");
	KPI_ASSERT(!pskb_may_pull(skb, 17u), "cannot pull past the frame");

	/* postpull: the first n bytes come back after the pull */
	(void)skb_pull(skb, 4u);
	skb_postpull_data_move(skb, 4u, 6u);
	KPI_ASSERT(memcmp(skb->data, "ABCDEF", 6) == 0, "postpull restored");
	KPI_ASSERT_EQ(skb->len, 12, "postpull length");

	/* postpush: the pre-push data's first n bytes move under the new head */
	KPI_ASSERT(skb_push(skb, 4u) != NULL, "push");
	memcpy(skb->data, "XXXX", 4);
	skb_postpush_data_move(skb, 4u, 6u);
	KPI_ASSERT(memcmp(skb->data, "ABCDEF", 6) == 0, "postpush moved");
	KPI_ASSERT_EQ(skb->len, 16, "postpush length");

	KPI_ASSERT_EQ(skb_metadata_len(skb), 0, "no metadata block");

	/* cow_head: enough headroom answers 0, one byte more is the interface's
	 * OOM answer (this port cannot relocate the buffer) */
	KPI_ASSERT_EQ(skb_cow_head(skb, 0), 0, "zero headroom");
	KPI_ASSERT_EQ(skb_cow_head(skb, skb_headroom(skb)), 0, "exact headroom");
	KPI_ASSERT_EQ(skb_cow_head(skb, skb_headroom(skb) + 1u), -ENOMEM,
		      "short headroom refuses");
	KPI_ASSERT_EQ(pskb_trim(skb, 4u), 0, "trim");
	KPI_ASSERT_EQ(skb->len, 4, "trimmed length");
	KPI_ASSERT_EQ(pskb_trim(skb, 5u), -EINVAL, "trim past the frame refuses");
	skb->csum_start = (uint16_t)(skb_headroom(skb) + 8u);
	KPI_ASSERT_EQ(skb_checksum_start_offset(skb), 8, "csum start offset");
	(void)buf;
	kfree_skb(skb);
	return KPI_PASS;
}

int kpi_case_skb_vlan(void)
{
	struct sk_buff *skb;

	skb = lkpi_skb_alloc(64u, GFP_KERNEL);
	KPI_ASSERT(skb != NULL, "alloc");
	KPI_ASSERT(!skb_vlan_tag_present(skb), "no tag on a fresh buffer");

	__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), 0x1234);
	KPI_ASSERT(skb_vlan_tag_present(skb), "tag present");
	KPI_ASSERT_EQ(skb_vlan_tag_get(skb), 0x1234, "tci");
	KPI_ASSERT_EQ(skb->vlan_proto, htons(ETH_P_8021Q), "proto");
	KPI_ASSERT_EQ(skb_vlan_tag_get_id(skb), 0x234, "vid");
	KPI_ASSERT_EQ(skb_vlan_tag_get_prio(skb), 0, "prio");
	KPI_ASSERT(skb_vlan_tag_get_cfi(skb), "cfi");
	KPI_ASSERT(skb_vlan_tagged(skb), "tagged");

	__vlan_hwaccel_clear_tag(skb);
	KPI_ASSERT(!skb_vlan_tag_present(skb), "cleared");
	KPI_ASSERT_EQ(skb->vlan_all, 0, "vlan_all cleared");
	kfree_skb(skb);
	return KPI_PASS;
}
