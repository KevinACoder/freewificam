/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/cpu.h
 * @brief   The CPU-count and CPU-iteration vocabulary a driver's queue sizing and
 *          teardown paths name.
 *
 * @design declaration-only compatibility face. The interface's header
 *         (include/linux/cpu.h) owns the CPU hotplug and topology surface; the two
 *         shapes a compiled consumer names are the online-CPU count and the
 *         possible-CPU iterator, and both keep the interface's CONFIG_SMP-off arms
 *         (include/linux/cpumask.h:1262,1131) because this port runs a single CPU.
 *         The per-CPU accessors the same consumer names come from the interface's
 *         percpu header, included here because that is the header this consumer
 *         reaches them through. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_CPU_H_
#define _LINUXKPI_LINUX_CPU_H_

#include "../../linuxkpi_core.h"
#include <linux/percpu.h>

/* [LNX] include/linux/cpumask.h:1262 */
#define	num_online_cpus()	1U

/* [LNX] include/linux/cpumask.h:1131 */
#define	for_each_possible_cpu(cpu)	for ((cpu) = 0; (cpu) < 1; (cpu)++)

/* [LNX] include/linux/cpumask.h: the CPU mask record and the two accessors a
 * compiled consumer names. This port runs a single CPU, so the mask has one word
 * with bit 0 set as the online set, and both accessors keep the interface's
 * shapes. */
#define	NR_CPUS			1

struct cpumask {
	unsigned long	bits[1];
};
typedef struct cpumask		cpumask_t;

#define	cpumask_bits(maskp)	((maskp)->bits)

static __always_inline __unused void
cpumask_set_cpu(unsigned int cpu, struct cpumask *dstp)
{
	if (cpu < 64)
		dstp->bits[0] |= (1UL << cpu);
}

/* [LNX] include/linux/cpumask.h: the next set bit above `n`, or the first index
 * past the mask when there is none. */
static __always_inline __unused int
cpumask_next(int n, const struct cpumask *srcp)
{
	int cpu;

	for (cpu = n + 1; cpu < 64; cpu++)
		if (srcp->bits[0] & (1UL << cpu))
			return cpu;
	return (int)NR_CPUS;
}

/* [LNX] include/linux/cpumask.h:119,125: the online set, which on this port is the
 * single CPU. */
static __always_inline __unused const struct cpumask *
lkpi_cpu_online_mask(void)
{
	static const struct cpumask m = { { 1UL } };

	return &m;
}
#define	cpu_online_mask		lkpi_cpu_online_mask()

#endif /* _LINUXKPI_LINUX_CPU_H_ */
