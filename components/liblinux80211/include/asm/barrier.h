/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    asm/barrier.h
 * @brief   The memory-barrier vocabulary the interface's asm/barrier.h names.
 *
 * @design declaration-only compatibility face.  The interface (arm64) spells
 *         the barriers with DMB/DSB instructions plus the SMP/UP selection;
 *         this port runs one core and its compiler target is the same arm64,
 *         so the GCC atomic builtins are the same instructions: a full barrier
 *         is DMB ISH, a read barrier DMB ISHLD, a write barrier DMB ISHST.
 *         The interface's smp_* forms collapse to compiler barriers on a
 *         uniprocessor build, which is this port's documented model
 *         (AGENTS.md §2.1).  Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_ASM_BARRIER_H_
#define _LINUXKPI_ASM_BARRIER_H_

#include <linux/compiler.h>

#define	mb()		__sync_synchronize()
#define	rmb()		__sync_synchronize()
#define	wmb()		__sync_synchronize()
#define	dma_mb()	__sync_synchronize()
#define	io_stop_wc()	do { } while (0)

/* uniprocessor: no cross-CPU ordering to establish */
#define	smp_mb()	barrier()
#define	smp_rmb()	barrier()
#define	smp_wmb()	barrier()
#define	smp_store_mb(var, value)	do { (var) = (value); barrier(); } while (0)

#endif /* _LINUXKPI_ASM_BARRIER_H_ */
