/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/list.h
 * @brief   The list helpers drivers call that the component's contract header does not
 *          carry.
 *
 * @design declaration-only compatibility face; the list type and most operations
 *         already live in the contract header (components/linuxkpi/linuxkpi_core.h,
 *         its list section), so this file adds only the missing operations rather than
 *         a second list implementation - the same single definition of `struct
 *         list_head` is used by both, and the additions follow the interface's
 *         semantics (include/linux/list.h). Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_LIST_H_
#define _LINUXKPI_LINUX_LIST_H_

#include "../../linuxkpi_core.h"

/*
 * Move every node of `list` to the front of `head` and leave `list` empty. What
 * distinguishes it from the tail variant is only where the nodes land.
 */
static inline void list_splice_init(struct list_head *list,
				    struct list_head *head)
{
	struct list_head *pos, *tmp, *first;

	if (list_empty(list))
		return;
	first = head->next;
	for (pos = list->next; pos != list; pos = tmp) {
		tmp = pos->next;
		/* Insert before the first node of `head`, which is what makes this
		 * the front-splicing variant; node order is preserved. */
		list_add_tail(pos, first);
	}
	INIT_LIST_HEAD(list);
}

#endif /* _LINUXKPI_LINUX_LIST_H_ */
