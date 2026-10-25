/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/sched/types.h
 * @brief   The CPU-time accounting record the interface's sched-types header carries.
 *
 * @design declaration-only compatibility face. The vendored platform layer includes
 *         this header by name (its comment expects `struct sched_param` here); in the
 *         interface generation this face is transcribed from (include/linux/sched/
 *         types.h) the header carries `struct task_cputime` instead, and
 *         `struct sched_param` sits in include/linux/sched.h - where this face states
 *         it, so the type is declared once, in its truth location. What is carried here
 *         is therefore the interface's exact content, transcribed field for field.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SCHED_TYPES_H_
#define _LINUXKPI_LINUX_SCHED_TYPES_H_

#include "../../../linuxkpi_core.h"

#include <linux/sched.h>

/* [LNX] include/linux/sched/types.h:19-23. No caller in the compiled set reads it yet;
 * it is here because it is the whole content of the interface header of this name. */
struct task_cputime {
	u64			stime;
	u64			utime;
	unsigned long long	sum_exec_runtime;
};

#endif /* _LINUXKPI_LINUX_SCHED_TYPES_H_ */
