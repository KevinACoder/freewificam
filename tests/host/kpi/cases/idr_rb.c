/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   idr_rb.c
 * @brief  The container bodies the stack's registration path uses: IDR and
 *         rhltable.
 *
 * IDR: mac80211's ack-frame table allocates ids in [0, 0x100) and iterates
 * them; the host bench pins allocate/find/for_each/get_next/remove/destroy.
 *
 * rhltable: mac80211's STA table is an rhltable keyed by the STA address; the
 * bench pins insert/lookup/remove/destroy over two records.
 *
 * The red-black tree is not walked here: its first real consumer is
 * cfg80211's BSS tree on the C4 scan path, and a synthetic tree would pin the
 * implementation, not the consumer.  When C4 lands, that path is its test.
 */
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"

#include <linux/idr.h>
#include <linux/rhashtable.h>

#include "kpi_test.h"

static int
count_entry(int id, void *p, void *data)
{
	unsigned int *n = data;

	(void)id;
	(void)p;
	(*n)++;
	return 0;
}

static int
stop_at_two(int id, void *p, void *data)
{
	unsigned int *n = data;

	(void)id;
	(void)p;
	(*n)++;
	return *n == 2 ? 1 : 0;
}

int
kpi_case_idr_rb(void)
{
	struct idr idr;
	void *a = (void *)0x1000;
	void *b = (void *)0x2000;
	void *c = (void *)0x3000;
	unsigned int n = 0;
	int id0, id1, id2;
	int next;

	idr_init(&idr);
	id0 = idr_alloc(&idr, a, 0, 0x100, GFP_KERNEL);
	id1 = idr_alloc(&idr, b, 0, 0x100, GFP_KERNEL);
	id2 = idr_alloc(&idr, c, 0, 0x100, GFP_KERNEL);
	KPI_ASSERT(id0 >= 0 && id1 >= 0 && id2 >= 0, "three ids allocated");
	KPI_ASSERT(id0 != id1 && id1 != id2 && id0 != id2, "ids are distinct");
	KPI_ASSERT(idr_find(&idr, (unsigned long)id0) == a, "find id0");
	KPI_ASSERT(idr_find(&idr, (unsigned long)id1) == b, "find id1");
	KPI_ASSERT(idr_for_each(&idr, count_entry, &n) == 0, "for_each returns 0");
	KPI_ASSERT_EQ(n, 3, "for_each visited three entries");

	n = 0;
	KPI_ASSERT(idr_for_each(&idr, stop_at_two, &n) == 1,
		   "for_each stops on the callback's non-zero");
	KPI_ASSERT_EQ(n, 2, "stopped after two entries");

	next = id0;
	KPI_ASSERT(idr_get_next(&idr, &next) == a, "get_next at id0");
	next = id0 + 1;
	KPI_ASSERT(idr_get_next(&idr, &next) == b, "get_next at id0+1");

	KPI_ASSERT(idr_remove(&idr, (unsigned long)id1) == b, "remove b");
	KPI_ASSERT(idr_find(&idr, (unsigned long)id1) == NULL, "b is gone");
	next = id1;
	KPI_ASSERT(idr_get_next(&idr, &next) == c, "get_next skips the hole");
	idr_destroy(&idr);
	return KPI_PASS;
}

struct rht_probe {
	struct rhlist_head	node;
	unsigned char		addr[6];
	unsigned int		mark;
};

static const struct rhashtable_params rht_params = {
	.nelem_hint = 4,
	.head_offset = offsetof(struct rht_probe, node),
	.key_offset = offsetof(struct rht_probe, addr),
	.key_len = 6,
	.max_size = 64,
};

int
kpi_case_rhtable(void)
{
	struct rhltable ht;
	struct rht_probe p1;
	struct rht_probe p2;
	struct rhlist_head *hit;
	unsigned char key1[6] = { 0x02, 0, 0, 0, 0, 1 };
	unsigned char key2[6] = { 0x02, 0, 0, 0, 0, 2 };
	unsigned char miss[6] = { 0x02, 0, 0, 0, 0, 3 };

	memset(&p1, 0, sizeof(p1));
	memset(&p2, 0, sizeof(p2));
	memcpy(p1.addr, key1, sizeof(key1));
	memcpy(p2.addr, key2, sizeof(key2));

	KPI_ASSERT_EQ(rhltable_init(&ht, &rht_params), 0, "init");
	KPI_ASSERT_EQ(rhltable_insert(&ht, &p1.node, rht_params), 0, "insert p1");
	KPI_ASSERT_EQ(rhltable_insert(&ht, &p2.node, rht_params), 0, "insert p2");
	hit = rhltable_lookup(&ht, key1, rht_params);
	KPI_ASSERT(hit == &p1.node, "lookup key1");
	hit = rhltable_lookup(&ht, key2, rht_params);
	KPI_ASSERT(hit == &p2.node, "lookup key2");
	KPI_ASSERT(rhltable_lookup(&ht, miss, rht_params) == NULL,
		   "a missing key returns NULL");
	KPI_ASSERT_EQ(rhltable_remove(&ht, &p1.node, rht_params), 0, "remove p1");
	KPI_ASSERT(rhltable_lookup(&ht, key1, rht_params) == NULL,
		   "p1 is gone");
	KPI_ASSERT(rhltable_lookup(&ht, key2, rht_params) == &p2.node,
		   "p2 survived p1's removal");
	rhltable_destroy(&ht);
	return KPI_PASS;
}
