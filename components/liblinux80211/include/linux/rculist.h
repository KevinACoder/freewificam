/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/rculist.h - the RCU list iterators, used-surface face.
 *
 * The pinned header adds the read-copy-update walk variants over list.h.  This
 * port's RCU read section is a reader count (linuxkpi_core.c): a reader is
 * never preempted off the CPU by a writer's grace period, so the interface's
 * "the list cannot change under the reader" precondition is what the count
 * enforces, and the walk itself is the plain list walk.  rcu_dereference() is
 * already the identity in this port's contract header.
 */
#ifndef _LINUXKPI_LINUX_RCULIST_H_
#define _LINUXKPI_LINUX_RCULIST_H_

#include "../../linuxkpi_core.h"
#include <linux/list.h>

/* [LNX] include/linux/rculist.h: the read-side walk.  Plain list_next_entry is
 * safe here: the reader count keeps the writer's synchronize_rcu() waiting, so
 * no node is freed while this loop runs. */
#define	list_for_each_entry_rcu(pos, head, member, cond...)		\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = list_next_entry(pos, member))

#define	list_for_each_entry_safe_rcu(pos, n, head, member)		\
	for (pos = list_first_entry(head, typeof(*pos), member),	\
	     n = list_next_entry(pos, member);				\
	     &pos->member != (head);					\
	     pos = n, n = list_next_entry(n, member))

/* [LNX] include/linux/rculist.h: the writer-side spellings.  The publish/link
 * pair is a normal list operation under the same single-core argument. */
/* [LNX] include/linux/rculist.h: the first entry, or NULL for an empty list. */
#define	list_first_or_null_rcu(ptr, type, member)			\
({									\
	struct list_head *__ptr = (ptr);				\
	struct list_head *__next = __ptr->next;				\
	__next == __ptr ? NULL :					\
	    list_entry(__next, type, member);				\
})

#define	list_add_rcu(new, head)		list_add((new), (head))
#define	list_add_tail_rcu(new, head)	list_add_tail((new), (head))
#define	list_del_rcu(entry)		list_del_init(entry)
#define	list_replace_rcu(old, new)	list_replace((old), (new))

#endif /* _LINUXKPI_LINUX_RCULIST_H_ */
