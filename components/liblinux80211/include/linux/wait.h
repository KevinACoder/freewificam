/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/wait.h
 * @brief   The wait-queue object and the wait-for-condition entry points a driver
 *          blocks on.
 *
 * @design declaration-only compatibility face. The wait-queue record and the
 *         wait_event family are part of the core contract every file of this face
 *         includes, and their bodies are the core's
 *         (components/linuxkpi/linuxkpi_core.c, on the CMSIS-RTOS2 condition
 *         shell). This header exists because the kernel's own spelling is what its
 *         sources include, so a consumer that reaches struct wait_queue_head,
 *         wait_event() or wait_event_timeout() through it names the same
 *         declarations the core publishes. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_WAIT_H_
#define _LINUXKPI_LINUX_WAIT_H_

#include "../../linuxkpi_core.h"

#include <linux/list.h>

struct task_struct;

/*
 * [LNX] include/linux/wait.h:44-58: the wait-queue entry and DECLARE_WAITQUEUE().
 * The interface's record is the list link, the task pointer and the wake callback
 * the entry is registered with; this face carries the same three fields because a
 * driver that declares one by hand (brcmfmac's SDIO control-frame wait,
 * sdio.c:1731) then passes its address to add_wait_queue() and remove_wait_queue().
 */
struct wait_queue_entry;

typedef int (*wait_queue_func_t)(struct wait_queue_entry *wq_entry,
				 unsigned int mode, int flags, void *key);

struct wait_queue_entry {
	unsigned int		flags;
	void			*private;	/* the waiting task */
	wait_queue_func_t	func;
	struct list_head	entry;
};

void init_waitqueue_entry(struct wait_queue_entry *wq_entry,
			  struct task_struct *p);
void add_wait_queue(struct wait_queue_head *wq_head,
		    struct wait_queue_entry *wq_entry);
void remove_wait_queue(struct wait_queue_head *wq_head,
		       struct wait_queue_entry *wq_entry);

/* [LNX] include/linux/wait.h:52-57: the declaration a caller writes on the stack. */
#define	DECLARE_WAITQUEUE(name, tsk)					\
	struct wait_queue_entry name = {				\
		.private = (tsk),					\
		.entry = LIST_HEAD_INIT((name).entry),			\
	}

#endif /* _LINUXKPI_LINUX_WAIT_H_ */
