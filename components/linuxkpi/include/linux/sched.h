/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/sched.h
 * @brief   The task object and the scheduling calls a driver's thread code uses.
 *
 * @design declaration-only compatibility face. The interface (include/linux/sched.h)
 *         describes `struct task_struct`, a several-hundred-field record holding a
 *         task's whole state; what a driver actually reaches for on this port is the
 *         calling thread's identity, so the struct here carries exactly the three
 *         fields that identify it and are read by the vendored bytes - the state word,
 *         the pid and the command name - in the interface's own field order. This is
 *         the first-party body of an interface that has per-task state on a Linux
 *         kernel and thread identity on this project's kernels: `current` is the
 *         calling thread's task object, and its body is the port's job (naming the
 *         calling thread and owning the per-thread objects), so it is declared and
 *         deliberately left undefined rather than faked - a caller that needs it must
 *         get a real implementation, and the linker is where that shows. The state
 *         bits, the state-setting macros and the scheduling entry points are
 *         transcribed from the interface with the debug/trace hooks it wraps them in
 *         removed (this port has no sched tracepoints), and the entry points are
 *         declarations with no body for the same reason as `current`: scheduling is
 *         the port's mechanism, not the header's. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SCHED_H_
#define _LINUXKPI_LINUX_SCHED_H_

#include "../../linuxkpi_core.h"

#include <linux/linkage.h>

/* [LNX] include/linux/types.h: typedef __kernel_pid_t pid_t (asm-generic/posix_types.h
 * makes __kernel_pid_t an int). Stated here because this face's task object needs the
 * interface's own spelling of the field type; an identical typedef in another face
 * header is the same type and C11 permits the repetition. */
typedef int	pid_t;

/*
 * [LNX] include/linux/sched.h:322-327: the command name length is an enum so that it
 * stays visible to BPF programs. The driver copies comm[] and sizes its own name
 * buffers with it.
 */
enum {
	TASK_COMM_LEN = 16,
};

/* [LNX] include/linux/sched.h:106-138: the bits of tsk->__state. Only the ones this
 * project's call sites name are carried; the exit_state group is part of the same
 * block and is carried with it so a reader sees the state word whole. */
#define	TASK_RUNNING			0x00000000
#define	TASK_INTERRUPTIBLE		0x00000001
#define	TASK_UNINTERRUPTIBLE		0x00000002
#define	__TASK_STOPPED			0x00000004
#define	__TASK_TRACED			0x00000008
#define	EXIT_DEAD			0x00000010
#define	EXIT_ZOMBIE			0x00000020
#define	EXIT_TRACE			(EXIT_ZOMBIE | EXIT_DEAD)
#define	TASK_PARKED			0x00000040
#define	TASK_DEAD			0x00000080
#define	TASK_WAKEKILL			0x00000100
#define	TASK_WAKING			0x00000200
#define	TASK_NOLOAD			0x00000400
#define	TASK_NEW			0x00000800

#define	TASK_KILLABLE			(TASK_WAKEKILL | TASK_UNINTERRUPTIBLE)
#define	TASK_STOPPED			(TASK_WAKEKILL | __TASK_STOPPED)
#define	TASK_TRACED			__TASK_TRACED
#define	TASK_IDLE			(TASK_UNINTERRUPTIBLE | TASK_NOLOAD)
#define	TASK_NORMAL			(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)

/*
 * The task object, cut down to the fields the compiled drivers read.
 *
 * [LNX] include/linux/sched.h: __state is `unsigned int __state;`, pid is
 * `pid_t pid;` and comm is `char comm[TASK_COMM_LEN];`. The three are kept in the
 * interface's order. A field this face does not carry is a field no compiled caller
 * touches; adding one here before a caller reads it would be a guess about the port's
 * thread bookkeeping rather than a transcription.
 */
struct task_struct {
	unsigned int	__state;
	pid_t		pid;
	char		comm[TASK_COMM_LEN];
};

/*
 * current / get_current(): [LNX] include/linux/sched.h + asm/current.h resolve these to
 * the running task's record. Here they resolve to the calling thread's task object,
 * which the port owns: the declaration is the interface's contract, the body belongs
 * to the port (it has to name the calling thread and keep one object per thread). No
 * definition is provided on purpose - a caller that reaches this must be given a real
 * implementation, and until then the linker says so.
 */
struct task_struct *lkpi_current(void);
#define	get_current()			lkpi_current()
#define	current				get_current()

/* [LNX] include/linux/sched.h:318: get_current_state() is READ_ONCE(current->__state). */
#define	get_current_state()		READ_ONCE(current->__state)

/*
 * [LNX] include/linux/sched.h:240-252, with the debug_normal_state_change() and
 * trace_set_current_state() hooks dropped (no tracepoints on this port). The interface
 * publishes the state with a store that is a barrier (smp_store_mb); this port's
 * schedulers observe the store directly, so the store is the plain READ_ONCE/WRITE_ONCE
 * pair the core carries.
 */
#define	__set_current_state(state_value)				\
	do {								\
		WRITE_ONCE(current->__state, (state_value));		\
	} while (0)

#define	set_current_state(state_value)					\
	do {								\
		WRITE_ONCE(current->__state, (state_value));		\
	} while (0)

/* [LNX] include/linux/sched.h:332-347. Declarations only: making a task wait, and
 * picking the next task to run, are port mechanisms with no first-party body yet. */
extern long schedule_timeout(long timeout);
extern long schedule_timeout_interruptible(long timeout);
extern long schedule_timeout_uninterruptible(long timeout);
asmlinkage void schedule(void);

/* [LNX] include/linux/sched.h:2028: extern int wake_up_process(struct task_struct *tsk); */
extern int wake_up_process(struct task_struct *tsk);

/* [LNX] include/linux/sched.h:412: struct sched_param { int sched_priority; }; the
 * parameter block sched_setscheduler() takes, and the policy numbers that select a
 * scheduling class ([LNX] include/uapi/linux/sched.h:124-131). The vendored thread
 * start path builds the one-field block and asks for SCHED_FIFO. */
struct sched_param {
	int	sched_priority;
};

#define	SCHED_NORMAL		0
#define	SCHED_FIFO		1
#define	SCHED_RR		2
#define	SCHED_BATCH		3
#define	SCHED_IDLE		5
#define	SCHED_DEADLINE		6
#define	SCHED_EXT		7

/* [LNX] include/linux/sched.h:1962: extern int sched_setscheduler(struct task_struct *,
 * int, const struct sched_param *); a declaration only - the policy of a task is the
 * port's scheduler state, so a caller must be given a real implementation. */
extern int sched_setscheduler(struct task_struct *, int,
			      const struct sched_param *);

#endif /* _LINUXKPI_LINUX_SCHED_H_ */
