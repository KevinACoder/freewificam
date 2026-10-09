/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    byteorder/swab.h
 * @brief   The byte-swap primitives the interface's byte-order headers build on.
 *
 * @design declaration-only compatibility face. The vendored vendor driver ships its own
 *         copies of the interface's byte-order headers (its own byteorder directory)
 *         for platforms that have no kernel headers to reach; on this port the
 *         face is the kernel, so the names resolve here instead - the include search finds
 *         the face first - and the swap primitives are spelled once, on the compiler's own
 *         byte-swap builtins, which is the same implementation the core header uses for
 *         its conversion macros. One implementation of the endianness mechanism, reached
 *         under both spellings; see byteorder/little_endian.h for the full note.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUX_BYTEORDER_SWAB_H
#define _LINUX_BYTEORDER_SWAB_H

#include "../linuxkpi_core.h"

#define	___constant_swab16(x)	((__u16)__builtin_bswap16((__u16)(x)))
#define	___constant_swab32(x)	((__u32)__builtin_bswap32((__u32)(x)))
#define	___constant_swab64(x)	((__u64)__builtin_bswap64((__u64)(x)))

#define	__swab16(x)		((__u16)__builtin_bswap16((__u16)(x)))
#define	__swab32(x)		((__u32)__builtin_bswap32((__u32)(x)))
#define	__swab64(x)		((__u64)__builtin_bswap64((__u64)(x)))

/* the pointer forms: swap the value the pointer refers to */
#define	__swab16p(p)		((__u16)__builtin_bswap16(*(const __u16 *)(p)))
#define	__swab32p(p)		((__u32)__builtin_bswap32(*(const __u32 *)(p)))
#define	__swab64p(p)		((__u64)__builtin_bswap64(*(const __u64 *)(p)))

/* the in-situ forms: swap the value the pointer refers to, in place */
#define	__swab16s(p)		do { *(p) = __swab16(*(p)); } while (0)
#define	__swab32s(p)		do { *(p) = __swab32(*(p)); } while (0)
#define	__swab64s(p)		do { *(p) = __swab64(*(p)); } while (0)

/* the constant-folding spellings the interface exposes for initialisers */
#define	swab16(x)		__swab16(x)
#define	swab32(x)		__swab32(x)
#define	swab64(x)		__swab64(x)

#endif /* _LINUX_BYTEORDER_SWAB_H */
