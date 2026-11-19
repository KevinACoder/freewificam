/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/sched/signal.h
 * @brief   The pending-signal predicate a driver's thread loop tests.
 *
 * @design declaration-only compatibility face. The interface (include/linux/sched/
 *         signal.h) answers the predicate from two thread flags the task object
 *         carries (TIF_SIGPENDING and TIF_NOTIFY_SIGNAL). This project's kernels have
 *         no signal delivery to a kernel thread, so there is no such flag to read and
 *         the answer is false by construction - not by assumption, and the body says so
 *         and names the two flags the real body will read, so the day a port grows
 *         signal delivery the predicate is completed rather than discovered. The signal
 *         hooks themselves live in <linux/signal.h>, which this header pulls in the way
 *         the interface does. flush_signals() is declared with the interface's
 *         signature and left undefined: the wait loop that would call it is unreachable
 *         while the predicate is false, and a caller that makes it reachable has to be
 *         given the real body. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SCHED_SIGNAL_H_
#define _LINUXKPI_LINUX_SCHED_SIGNAL_H_

#include "../../../linuxkpi_core.h"

#include <linux/sched.h>
#include <linux/signal.h>
/* [LNX] include/linux/sched/signal.h includes <linux/pid.h>: the signal calls below take
 * a pid object, and the face keeps the interface's own include so the record and its
 * lookup are visible together with them. */
#include <linux/pid.h>

/*
 * [LNX] include/linux/sched/signal.h:387-398: signal_pending() is
 * `test_tsk_thread_flag(p, TIF_NOTIFY_SIGNAL) || test_tsk_thread_flag(p, TIF_SIGPENDING)`.
 * Neither flag has a home yet: the port's task object is the caller's thread identity,
 * not a Linux thread_info. Until it grows the flag word, "no signal can be pending" is
 * the platform's real answer.
 */
static __always_inline __unused int
signal_pending(struct task_struct *p)
{
	(void)p;
	return 0;
}

/*
 * [LNX] include/linux/sched/signal.h:346: `extern int send_sig(int, struct
 * task_struct *, int);` - deliver a signal to a task. brcmfmac asks the SDIO
 * watchdog thread to end with it before it stops the thread (sdio.c:2481).
 *
 * This port has no signal delivery: a task is stopped through kthread_stop()'s
 * stop flag, which the same teardown calls immediately after. The answer is
 * therefore the interface's "this signal could not be delivered" (-EINVAL, the
 * number it returns for a signal it cannot queue); the caller ignores the return and
 * the stop itself is the stop flag's, so nothing is lost by the honest refusal. The
 * body is linuxkpi_osdep.c.
 */
extern int send_sig(int sig, struct task_struct *p, int priv);

/* [LNX] include/linux/sched/signal.h:283:
 * extern void flush_signals(struct task_struct *); */
extern void flush_signals(struct task_struct *);

struct pid;

/* [LNX] include/linux/sched/signal.h:340:
 * extern int kill_pid(struct pid *pid, int sig, int priv); */
extern int kill_pid(struct pid *pid, int sig, int priv);

#endif /* _LINUXKPI_LINUX_SCHED_SIGNAL_H_ */
