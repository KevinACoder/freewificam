/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/kthread.h
 * @brief   The kernel-thread creation and control calls a driver's worker paths use.
 *
 * @design declaration-only compatibility face. The interface (include/linux/kthread.h)
 *         is a small surface: one creation function with a printf-style name, the
 *         convenience macros over it, and the stop protocol. Its shape is transcribed
 *         exactly - including kthread_create()/kthread_run() being macros over
 *         kthread_create_on_node(), which is what keeps the call sites' argument lists
 *         (the name format plus its arguments) spelled the way upstream spells them. The
 *         functions are declarations with no body: a thread here is a port task, so
 *         creating it, binding it and stopping it are the port's mechanism, and the
 *         driver's stop protocol (kthread_should_stop in a wait loop) only becomes real
 *         together with that port. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_KTHREAD_H_
#define _LINUXKPI_LINUX_KTHREAD_H_

#include "../../linuxkpi_core.h"

#include <linux/sched.h>

/* [LNX] include/linux/numa.h: the "no particular node" value the creation helper takes;
 * this project's targets have one memory node, which is what the value means here too. */
#ifndef NUMA_NO_NODE
#define	NUMA_NO_NODE		(-1)
#endif

/* [LNX] include/linux/kthread.h:29-33 */
__printf(4, 5)
struct task_struct *kthread_create_on_node(int (*threadfn)(void *data),
					   void *data,
					   int node,
					   const char namefmt[], ...);

/* [LNX] include/linux/kthread.h:45-46 */
#define	kthread_create(threadfn, data, namefmt, arg...)			\
	kthread_create_on_node(threadfn, data, NUMA_NO_NODE, namefmt, ##arg)

/* [LNX] include/linux/kthread.h:69-77 */
#define	kthread_run(threadfn, data, namefmt, ...)			\
({									\
	struct task_struct *__k						\
		= kthread_create(threadfn, data, namefmt, ## __VA_ARGS__); \
	if (!IS_ERR(__k))						\
		wake_up_process(__k);					\
	__k;								\
})

/* [LNX] include/linux/kthread.h:104,107-109 */
void kthread_bind(struct task_struct *k, unsigned int cpu);
int kthread_stop(struct task_struct *k);
bool kthread_should_stop(void);

/* [LNX] include/linux/kthread.h:120: the exit path of a thread that was started with a
 * completion to signal. */
void kthread_complete_and_exit(struct completion *, long) __noreturn;

#endif /* _LINUXKPI_LINUX_KTHREAD_H_ */
