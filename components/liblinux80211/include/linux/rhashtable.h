/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/rhashtable.h - the resizable hash tables mac80211's STA books use,
 * used-surface face.
 *
 * The pinned tree (rhashtable.h + rhashtable-types.h) is a self-contained
 * library with its own deferred irq-work integration.  The compiled set names
 * the two records it embeds (struct rhashtable in the mesh tables, struct
 * rhlist_head in the STA records), the parameter record its initialisers fill,
 * and the five rhltable_* calls in sta_info.c.  The bodies are the port's
 * (S1c); the declarations below are the interface's signatures, the parameter
 * record by value exactly as the interface passes it.
 */
#ifndef _LINUXKPI_LINUX_RHASHTABLE_H_
#define _LINUXKPI_LINUX_RHASHTABLE_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/rhashtable-types.h: the list node a record embeds. */
struct rhash_head {
	struct rhash_head	*next;
};

struct rhlist_head {
	struct rhash_head	rhead;
	struct rhlist_head	*next;
};

/* [LNX] include/linux/rhashtable-types.h: the shape the STA tables' static
 * initialisers describe.  The interface's hashfn/obj_cmpfn members are not
 * named by a compiled initialiser yet and are left to the first consumer. */
struct rhashtable_params {
	unsigned short		nelem_hint;
	unsigned short		key_len;
	unsigned short		key_offset;
	unsigned short		head_offset;
	unsigned int		max_size;
	bool			automatic_shrinking;
};

/* [LNX] include/linux/rhashtable-types.h: opaque to this port; the mesh tables
 * embed it and pass it to nothing yet. */
struct rhashtable {
	void			*tbl;
	unsigned int		nelems;
	bool			running;
};

struct rhltable {
	struct rhashtable	ht;
};

/* [LNX] include/linux/rhashtable.h: the entry projection and the read-side walk
 * over one bucket's list; sta_info.h's for_each_sta_info() expands both. */
#define	rht_entry(tpos, pos, member)					\
({									\
	tpos = container_of(pos, typeof(*tpos), member);		\
	1;								\
})

#define	rhl_for_each_entry_rcu(tpos, pos, list, member)			\
	for (pos = (list);						\
	     pos && rht_entry(tpos, pos, member);			\
	     pos = rcu_dereference((pos)->next))

int rhltable_init(struct rhltable *hlt,
		  const struct rhashtable_params *params);
void rhltable_destroy(struct rhltable *hlt);
int rhltable_insert(struct rhltable *hlt, struct rhlist_head *list,
		    const struct rhashtable_params params);
int rhltable_remove(struct rhltable *hlt, struct rhlist_head *list,
		    const struct rhashtable_params params);
/* the port's own bucket-walk bound (see linuxkpi_containers.c): a chain longer
 * than the table's element count is cyclic, and these counters name it */
unsigned int lkpi_rht_walk_max(void);
unsigned int lkpi_rht_cycle_hits(void);

struct rhlist_head *rhltable_lookup(struct rhltable *hlt, const void *key,
				    const struct rhashtable_params params);

#endif /* _LINUXKPI_LINUX_RHASHTABLE_H_ */
