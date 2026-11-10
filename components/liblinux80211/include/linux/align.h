/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/align.h
 * @brief   The alignment arithmetic the interface states in vdso/align.h.
 *
 * @design declaration-only compatibility face.  The pinned header is a
 *         seven-line wrapper over <vdso/align.h>; the macros themselves are
 *         three lines of arithmetic, so the face states them directly rather
 *         than vendoring the vdso tree.  They are the interface's own
 *         spellings: ALIGN rounds up, ALIGN_DOWN rounds down, PTR_ALIGN is the
 *         pointer form and IS_ALIGNED the predicate.  The contract header does
 *         not carry them (nothing else named them before the imported stack's
 *         bitmap.h did).  Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_ALIGN_H_
#define _LINUXKPI_LINUX_ALIGN_H_

#include "../../linuxkpi_core.h"

/* @a is a power of 2 value */
#define	__ALIGN_KERNEL_MASK(x, mask)	(((x) + (mask)) & ~(mask))
#define	__ALIGN_KERNEL(x, a)						\
	__ALIGN_KERNEL_MASK((x), (typeof(x))(a) - 1)
#define	__ALIGN_MASK(x, mask)		__ALIGN_KERNEL_MASK((x), (mask))

#define	ALIGN(x, a)			__ALIGN_KERNEL((x), (a))
#define	ALIGN_DOWN(x, a)		__ALIGN_KERNEL((x) - ((a) - 1), (a))
#define	PTR_ALIGN(p, a)							\
	((typeof(p))ALIGN((unsigned long)(p), (a)))
#define	PTR_ALIGN_DOWN(p, a)						\
	((typeof(p))ALIGN_DOWN((unsigned long)(p), (a)))
#define	IS_ALIGNED(x, a)		(((x) & ((typeof(x))(a) - 1)) == 0)

#endif /* _LINUXKPI_LINUX_ALIGN_H_ */
