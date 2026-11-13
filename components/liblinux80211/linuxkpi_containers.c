/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_containers.c
 * @brief   The container bodies the imported stack names: IDR, rhltable, rbtree.
 *
 * @design three small first-party implementations of interfaces the kernel
 *         carries as libraries.  They are sized for the compiled set, not for
 *         a general kernel:
 *
 *         - IDR: a fixed slot array (the stack's ack-frame table asks for at
 *           most 0x100 ids; the NAN table is smaller).  No radix tree.
 *         - rhltable: a bucket array of singly linked `struct rhlist_head`
 *           chains, hashed over the key the parameter record names.  This is
 *           the shape mac80211's STA table asks for; there is no resize.
 *         - rbtree: the standard red-black insert/erase, because cfg80211's
 *           BSS tree is a real ordered tree and a fake would corrupt scanning.
 *
 *         Every limit is a compile-time constant and a caller that exceeds it
 *         gets an error, not a silent overwrite.
 * @date    10.11.2026
 */

#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/idr.h>
#include <linux/rhashtable.h>
#include <linux/rbtree.h>
#include <linux/rbtree_types.h>

/* ------------------------------------------------------------------ */
/* IDR                                                                 */
/* ------------------------------------------------------------------ */

#define	LKPI_IDR_SLOTS	1024

struct lkpi_idr_slots {
	void	*slot[LKPI_IDR_SLOTS];
};

static struct lkpi_idr_slots *
lkpi_idr_slots(struct idr *idr, bool create)
{
	struct lkpi_idr_slots *s;

	if (idr->idr_root != NULL)
		return idr->idr_root;
	if (!create)
		return NULL;
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return NULL;
	idr->idr_root = s;
	return s;
}

int
idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp)
{
	struct lkpi_idr_slots *s;
	int id;

	(void)gfp;
	if (idr == NULL || ptr == NULL)
		return -EINVAL;
	if (start < 0)
		start = 0;
	if (end <= 0 || end > LKPI_IDR_SLOTS)
		end = LKPI_IDR_SLOTS;
	s = lkpi_idr_slots(idr, true);
	if (s == NULL)
		return -ENOMEM;
	for (id = start; id < end; id++) {
		if (s->slot[id] == NULL) {
			s->slot[id] = ptr;
			idr->idr_next = (unsigned int)id + 1U;
			return id;
		}
	}
	return -ENOSPC;
}

void *
idr_remove(struct idr *idr, unsigned long id)
{
	struct lkpi_idr_slots *s = lkpi_idr_slots(idr, false);
	void *old;

	if (s == NULL || id >= LKPI_IDR_SLOTS)
		return NULL;
	old = s->slot[id];
	s->slot[id] = NULL;
	return old;
}

void *
idr_find(const struct idr *idr, unsigned long id)
{
	struct lkpi_idr_slots *s = idr->idr_root;

	if (s == NULL || id >= LKPI_IDR_SLOTS)
		return NULL;
	return s->slot[id];
}

int
idr_for_each(const struct idr *idr, int (*fn)(int id, void *p, void *data),
	     void *data)
{
	struct lkpi_idr_slots *s = idr->idr_root;
	int id;

	if (s == NULL || fn == NULL)
		return 0;
	for (id = 0; id < LKPI_IDR_SLOTS; id++) {
		int r;

		if (s->slot[id] == NULL)
			continue;
		r = fn(id, s->slot[id], data);
		if (r != 0)
			return r;
	}
	return 0;
}

void *
idr_get_next(struct idr *idr, int *nextid)
{
	struct lkpi_idr_slots *s = idr->idr_root;
	int id;

	if (s == NULL || nextid == NULL || *nextid < 0)
		return NULL;
	for (id = *nextid; id < LKPI_IDR_SLOTS; id++) {
		if (s->slot[id] != NULL) {
			*nextid = id;
			return s->slot[id];
		}
	}
	return NULL;
}

void
idr_destroy(struct idr *idr)
{
	if (idr == NULL || idr->idr_root == NULL)
		return;
	kfree(idr->idr_root);
	idr->idr_root = NULL;
}

/* ------------------------------------------------------------------ */
/* rhltable                                                            */
/* ------------------------------------------------------------------ */

#define	LKPI_RHT_BUCKETS	64

struct lkpi_rht {
	struct rhlist_head	**bucket;
	unsigned int		nbuckets;
};

static unsigned int
lkpi_rht_hash(const void *key, unsigned int len)
{
	const unsigned char *p = key;
	unsigned int h = 2166136261U;	/* FNV-1a */
	unsigned int i;

	for (i = 0; i < len; i++) {
		h ^= p[i];
		h *= 16777619U;
	}
	return h;
}

static struct lkpi_rht *
lkpi_rht(struct rhltable *hlt)
{
	return hlt != NULL ? hlt->ht.tbl : NULL;
}

int
rhltable_init(struct rhltable *hlt, const struct rhashtable_params *params)
{
	struct lkpi_rht *t;
	unsigned int nb = LKPI_RHT_BUCKETS;

	if (hlt == NULL)
		return -EINVAL;
	if (params != NULL && params->nelem_hint > nb)
		nb = params->nelem_hint;
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	t->bucket = kcalloc(nb, sizeof(*t->bucket), GFP_KERNEL);
	if (t->bucket == NULL) {
		kfree(t);
		return -ENOMEM;
	}
	t->nbuckets = nb;
	hlt->ht.tbl = t;
	hlt->ht.nelems = 0;
	hlt->ht.running = true;
	return 0;
}

void
rhltable_destroy(struct rhltable *hlt)
{
	struct lkpi_rht *t = lkpi_rht(hlt);

	if (t == NULL)
		return;
	kfree(t->bucket);
	kfree(t);
	hlt->ht.tbl = NULL;
	hlt->ht.nelems = 0;
	hlt->ht.running = false;
}

static struct rhlist_head *
lkpi_rht_bucket(struct lkpi_rht *t, const void *key, unsigned int key_len)
{
	unsigned int h = lkpi_rht_hash(key, key_len) % t->nbuckets;

	return t->bucket[h];
}

int
rhltable_insert(struct rhltable *hlt, struct rhlist_head *list,
		const struct rhashtable_params params)
{
	struct lkpi_rht *t = lkpi_rht(hlt);
	unsigned int h;
	const void *key;

	if (t == NULL || list == NULL)
		return -EINVAL;
	key = (const char *)list - params.head_offset + params.key_offset;
	h = lkpi_rht_hash(key, params.key_len) % t->nbuckets;
	list->next = t->bucket[h];
	t->bucket[h] = list;
	hlt->ht.nelems++;
	return 0;
}

/*
 * A bucket chain is a list of nodes; a chain longer than the table's own
 * element count, or one that revisits a node, is cyclic.  The interface's
 * rhashtable cannot produce that (it is a hash table with per-bucket lists,
 * and a node is removed before it is re-inserted), but this shim's insertion
 * is the port's own and a double insert would make `next` point into its own
 * chain.  The counters exist so a lookup that does not terminate names itself
 * instead of freezing a receive worker; the bound is the table's element count
 * (a legitimate chain is at most that long) with a hard ceiling for the case
 * where the count itself is wrong.
 */
#define LKPI_RHT_WALK_MAX	4096u

static unsigned int rht_walk_last;
static unsigned int rht_walk_max;
static unsigned int rht_cycle_hits;

unsigned int lkpi_rht_walk_max(void)
{

	return rht_walk_max;
}

unsigned int lkpi_rht_cycle_hits(void)
{

	return rht_cycle_hits;
}

int
rhltable_remove(struct rhltable *hlt, struct rhlist_head *list,
		const struct rhashtable_params params)
{
	struct lkpi_rht *t = lkpi_rht(hlt);
	struct rhlist_head **pp;
	const void *key;
	unsigned int h;

	if (t == NULL || list == NULL)
		return -EINVAL;
	key = (const char *)list - params.head_offset + params.key_offset;
	h = lkpi_rht_hash(key, params.key_len) % t->nbuckets;
	for (pp = &t->bucket[h]; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == list) {
			*pp = list->next;
			list->next = NULL;
			hlt->ht.nelems--;
			return 0;
		}
	}
	return -ENOENT;
}

struct rhlist_head *
rhltable_lookup(struct rhltable *hlt, const void *key,
		const struct rhashtable_params params)
{
	struct lkpi_rht *t = lkpi_rht(hlt);
	struct rhlist_head *list;
	unsigned int h;

	if (t == NULL || key == NULL)
		return NULL;
	h = lkpi_rht_hash(key, params.key_len) % t->nbuckets;
	{
		unsigned int steps = 0u;

		for (list = t->bucket[h]; list != NULL; list = list->next) {
			const void *obj_key =
				(const char *)list - params.head_offset +
				params.key_offset;

			if (++steps > hlt->ht.nelems + 1u ||
			    steps > LKPI_RHT_WALK_MAX) {
				rht_cycle_hits++;
				if (rht_cycle_hits <= 4u)
					lkpi_printf(ULOG_ERROR_LEVEL,
					    "lkpi: rhashtable walk did not terminate bucket=%u nelems=%u steps=%u\n",
					    h, (unsigned)hlt->ht.nelems,
					    steps);
				break;
			}
			if (memcmp(key, obj_key, params.key_len) == 0) {
				if (steps > rht_walk_max)
					rht_walk_max = steps;
				rht_walk_last = steps;
				return list;
			}
		}
		rht_walk_last = steps;
		if (steps > rht_walk_max)
			rht_walk_max = steps;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* rbtree                                                              */
/* ------------------------------------------------------------------ */

#define	rb_is_red(n)	(!((n)->__rb_parent_color & 1UL))
#define	rb_is_black(n)	((n)->__rb_parent_color & 1UL)
#define	rb_set_parent_color(n, p, c)					\
	((n)->__rb_parent_color = (unsigned long)(p) | (c))
#define	rb_set_parent(n, p)						\
	((n)->__rb_parent_color = (unsigned long)(p) |			\
	 ((n)->__rb_parent_color & 1UL))

static void
rb_rotate_left(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *right = node->rb_right;
	struct rb_node *parent = rb_parent(node);

	node->rb_right = right->rb_left;
	if (right->rb_left != NULL)
		rb_set_parent(right->rb_left, node);
	right->rb_left = node;

	rb_set_parent(right, parent);
	if (parent == NULL)
		root->rb_node = right;
	else if (node == parent->rb_left)
		parent->rb_left = right;
	else
		parent->rb_right = right;
	rb_set_parent(node, right);
}

static void
rb_rotate_right(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *left = node->rb_left;
	struct rb_node *parent = rb_parent(node);

	node->rb_left = left->rb_right;
	if (left->rb_right != NULL)
		rb_set_parent(left->rb_right, node);
	left->rb_right = node;

	rb_set_parent(left, parent);
	if (parent == NULL)
		root->rb_node = left;
	else if (node == parent->rb_right)
		parent->rb_right = left;
	else
		parent->rb_left = left;
	rb_set_parent(node, left);
}

void
rb_insert_color(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *parent;
	struct rb_node *gparent;

	while ((parent = rb_parent(node)) != NULL && rb_is_red(parent)) {
		gparent = rb_parent(parent);
		if (gparent == NULL)
			break;
		if (parent == gparent->rb_left) {
			struct rb_node *uncle = gparent->rb_right;

			if (uncle != NULL && rb_is_red(uncle)) {
				rb_set_parent_color(parent, gparent, 1);
				rb_set_parent_color(uncle, gparent, 1);
				rb_set_parent_color(gparent, rb_parent(gparent), 0);
				node = gparent;
				continue;
			}
			if (node == parent->rb_right) {
				struct rb_node *tmp;

				rb_rotate_left(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}
			rb_set_parent_color(parent, gparent, 1);
			rb_set_parent_color(gparent, rb_parent(gparent), 0);
			rb_rotate_right(gparent, root);
		} else {
			struct rb_node *uncle = gparent->rb_left;

			if (uncle != NULL && rb_is_red(uncle)) {
				rb_set_parent_color(parent, gparent, 1);
				rb_set_parent_color(uncle, gparent, 1);
				rb_set_parent_color(gparent, rb_parent(gparent), 0);
				node = gparent;
				continue;
			}
			if (node == parent->rb_left) {
				struct rb_node *tmp;

				rb_rotate_right(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}
			rb_set_parent_color(parent, gparent, 1);
			rb_set_parent_color(gparent, rb_parent(gparent), 0);
			rb_rotate_left(gparent, root);
		}
	}
	rb_set_parent_color(root->rb_node, NULL, 1);
}

static struct rb_node *
rb_successor(struct rb_node *node)
{
	struct rb_node *parent;

	if (node->rb_right != NULL) {
		node = node->rb_right;
		while (node->rb_left != NULL)
			node = node->rb_left;
		return node;
	}
	parent = rb_parent(node);
	while (parent != NULL && node == parent->rb_right) {
		node = parent;
		parent = rb_parent(parent);
	}
	return parent;
}

static void
rb_erase_rebalance(struct rb_node *node, struct rb_node *parent,
		   struct rb_root *root)
{
	while (node != root->rb_node && (node == NULL || rb_is_black(node))) {
		if (parent == NULL)
			break;
		if (node == parent->rb_left) {
			struct rb_node *sibling = parent->rb_right;

			if (sibling != NULL && rb_is_red(sibling)) {
				rb_set_parent_color(sibling, parent, 1);
				rb_set_parent_color(parent, rb_parent(parent), 0);
				rb_rotate_left(parent, root);
				sibling = parent->rb_right;
			}
			if (sibling == NULL)
				break;
			if ((sibling->rb_left == NULL ||
			     rb_is_black(sibling->rb_left)) &&
			    (sibling->rb_right == NULL ||
			     rb_is_black(sibling->rb_right))) {
				rb_set_parent_color(sibling,
						    rb_parent(sibling), 0);
				node = parent;
				parent = rb_parent(parent);
				continue;
			}
			if (sibling->rb_right == NULL ||
			    rb_is_black(sibling->rb_right)) {
				if (sibling->rb_left != NULL)
					rb_set_parent_color(sibling->rb_left,
							    sibling, 1);
				rb_set_parent_color(sibling,
						    rb_parent(sibling), 0);
				rb_rotate_right(sibling, root);
				sibling = parent->rb_right;
			}
			rb_set_parent_color(sibling, parent,
					    rb_is_black(parent) ? 1 : 0);
			rb_set_parent_color(parent, rb_parent(parent), 1);
			if (sibling->rb_right != NULL)
				rb_set_parent_color(sibling->rb_right,
						    sibling, 1);
			rb_rotate_left(parent, root);
			node = root->rb_node;
			parent = NULL;
		} else {
			struct rb_node *sibling = parent->rb_left;

			if (sibling != NULL && rb_is_red(sibling)) {
				rb_set_parent_color(sibling, parent, 1);
				rb_set_parent_color(parent, rb_parent(parent), 0);
				rb_rotate_right(parent, root);
				sibling = parent->rb_left;
			}
			if (sibling == NULL)
				break;
			if ((sibling->rb_right == NULL ||
			     rb_is_black(sibling->rb_right)) &&
			    (sibling->rb_left == NULL ||
			     rb_is_black(sibling->rb_left))) {
				rb_set_parent_color(sibling,
						    rb_parent(sibling), 0);
				node = parent;
				parent = rb_parent(parent);
				continue;
			}
			if (sibling->rb_left == NULL ||
			    rb_is_black(sibling->rb_left)) {
				if (sibling->rb_right != NULL)
					rb_set_parent_color(sibling->rb_right,
							    sibling, 1);
				rb_set_parent_color(sibling,
						    rb_parent(sibling), 0);
				rb_rotate_left(sibling, root);
				sibling = parent->rb_left;
			}
			rb_set_parent_color(sibling, parent,
					    rb_is_black(parent) ? 1 : 0);
			rb_set_parent_color(parent, rb_parent(parent), 1);
			if (sibling->rb_left != NULL)
				rb_set_parent_color(sibling->rb_left,
						    sibling, 1);
			rb_rotate_right(parent, root);
			node = root->rb_node;
			parent = NULL;
		}
	}
	if (node != NULL)
		rb_set_parent_color(node, rb_parent(node), 1);
}

void
rb_erase(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *child;
	struct rb_node *parent;
	struct rb_node *tmp;
	bool left = true;

	if (node == NULL || root == NULL)
		return;
	parent = rb_parent(node);
	if (node->rb_left == NULL) {
		child = node->rb_right;
	} else if (node->rb_right == NULL) {
		child = node->rb_left;
	} else {
		tmp = rb_successor(node);
		child = tmp->rb_right;
		parent = rb_parent(tmp);
		left = (parent != NULL && tmp == parent->rb_left);
		if (tmp->rb_right != NULL)
			rb_set_parent(tmp->rb_right, tmp);
		rb_set_parent(tmp, parent);
		if (parent != NULL) {
			if (left)
				parent->rb_left = tmp;
			else
				parent->rb_right = tmp;
		}
		tmp->rb_left = node->rb_left;
		rb_set_parent(node->rb_left, tmp);
		tmp->rb_right = node->rb_right;
		if (node->rb_right != NULL)
			rb_set_parent(node->rb_right, tmp);
		rb_set_parent_color(tmp, rb_parent(node),
				    node->__rb_parent_color & 1UL);
		if (parent == node)
			parent = tmp;
		node = tmp;
	}
	if (child != NULL) {
		rb_set_parent(child, parent);
		if (parent != NULL) {
			if (left)
				parent->rb_left = child;
			else
				parent->rb_right = child;
		} else {
			root->rb_node = child;
		}
		if (rb_is_black(node))
			rb_erase_rebalance(child, parent, root);
	} else if (parent != NULL) {
		if (left)
			parent->rb_left = NULL;
		else
			parent->rb_right = NULL;
		if (rb_is_black(node))
			rb_erase_rebalance(NULL, parent, root);
	} else {
		root->rb_node = NULL;
	}
}
