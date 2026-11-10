/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   skb.c
 * @brief  The sk_buff pool's two contracts: geometry refusal and recovery
 *         after exhaustion.
 *
 * A refused skb_put must return NULL and leave the buffer's length alone; a
 * fully drained pool must return NULL (never a wild pointer) and hand slots
 * out again once the held ones are freed.  Both are the interfaces mac80211's
 * TX/RX paths assume.
 */
#include <stdint.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"

#include "kpi_test.h"

int kpi_case_skb_bounds(void)
{
	struct sk_buff *skb;
	unsigned char *p;
	unsigned int len;

	skb = lkpi_skb_alloc(128u, GFP_KERNEL);
	KPI_ASSERT(skb != NULL, "alloc");
	len = skb->len;
	p = skb_put(skb, 64u);
	KPI_ASSERT(p != NULL, "in-bounds put");
	KPI_ASSERT_EQ(skb->len, len + 64u, "length grew");
	/* one byte past the tail room: refused, length unchanged */
	p = skb_put(skb, skb->end - skb->tail + 1u);
	KPI_ASSERT(p == NULL, "out-of-bounds put refused");
	KPI_ASSERT_EQ(skb->len, len + 64u, "refusal left the length alone");
	kfree_skb(skb);
	return KPI_PASS;
}

int kpi_case_skb_pool(void)
{
	struct sk_buff *held[256];
	unsigned int n = 0;
	unsigned int i;

	while (n < (unsigned int)(sizeof(held) / sizeof(held[0]))) {
		held[n] = lkpi_skb_alloc(64u, GFP_KERNEL);
		if (held[n] == NULL)
			break;
		n++;
	}
	KPI_ASSERT(n > 0, "pool handed out at least one slot");
	KPI_ASSERT(n < (unsigned int)(sizeof(held) / sizeof(held[0])),
		   "pool exhaustion returned NULL");
	for (i = 0; i < n; i++)
		kfree_skb(held[i]);
	KPI_ASSERT(lkpi_skb_alloc(64u, GFP_KERNEL) != NULL,
		   "released slots are reusable");
	return KPI_PASS;
}
