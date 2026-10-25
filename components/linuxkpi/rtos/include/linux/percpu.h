/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/percpu.h
 * @brief   The per-CPU storage vocabulary a driver's per-queue scratch area names.
 *
 * @design compatibility face, grown per consumer. The interface's header
 *         (include/linux/percpu.h) owns the per-CPU allocator and the per-CPU
 *         accessors. This port runs a single CPU, so the interface's own
 *         CONFIG_SMP-off arms are the reachable shapes: the accessor is the
 *         identity on the pointer (no per-CPU offset to apply) and the allocation
 *         is an ordinary zeroed heap block. The block is handed back through
 *         free_percpu(), which is the matching heap release. Reference for the
 *         face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_PERCPU_H_
#define _LINUXKPI_LINUX_PERCPU_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/percpu-defs.h:256-264: the CONFIG_SMP-off arms. */
#define	per_cpu_ptr(ptr, cpu)	({ (void)(cpu); (ptr); })
#define	raw_cpu_ptr(ptr)	per_cpu_ptr((ptr), 0)
#define	this_cpu_ptr(ptr)	raw_cpu_ptr(ptr)
#define	per_cpu(var, cpu)	(*per_cpu_ptr(&(var), (cpu)))

/* [LNX] include/linux/percpu.h:150-151: the typed allocation. With one CPU the
 * per-CPU block is an ordinary zeroed heap block. */
#define	alloc_percpu(type)	((typeof(type) *)kzalloc(sizeof(type), GFP_KERNEL))

/* [LNX] include/linux/percpu.h:157: the matching release. */
static __always_inline __unused void
free_percpu(void *p)
{
	kfree(p);
}

#endif /* _LINUXKPI_LINUX_PERCPU_H_ */
