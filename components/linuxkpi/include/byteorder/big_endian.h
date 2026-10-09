/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    byteorder/big_endian.h
 * @brief   The byte-order interface for a big-endian target.
 *
 * @design declaration-only compatibility face, the mirror of byteorder/little_endian.h
 *         (read that file's note for why the vendored driver's own copies are shadowed).
 *         On a big-endian target the conversion direction is the opposite of the
 *         little-endian file's, and the target's byte order is a compile-time property of
 *         the face, not of this header: the core header keys its conversions on
 *         __BYTE_ORDER, and this file provides the names, never a second implementation.
 *         No board of this project is big-endian today; the file exists so the interface
 *         a source includes is the same one on either kind of target. Reference for the
 *         face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUX_BYTEORDER_BIG_ENDIAN_H
#define _LINUX_BYTEORDER_BIG_ENDIAN_H

#include "../linuxkpi_core.h"
#include "swab.h"
#include "generic.h"

#ifndef __BIG_ENDIAN
#define	__BIG_ENDIAN		4321
#endif
#ifndef __BIG_ENDIAN_BITFIELD
#define	__BIG_ENDIAN_BITFIELD
#endif

/* constant-folded forms: on a big-endian target the LE conversions move bytes and the
 * BE ones are the identity */
#define	__constant_htonl(x)		((__u32)(x))
#define	__constant_ntohl(x)		((__u32)(x))
#define	__constant_htons(x)		((__u16)(x))
#define	__constant_ntohs(x)		((__u16)(x))
#define	__constant_cpu_to_le64(x)	___constant_swab64((__u64)(x))
#define	__constant_le64_to_cpu(x)	___constant_swab64((__u64)(x))
#define	__constant_cpu_to_le32(x)	___constant_swab32((__u32)(x))
#define	__constant_le32_to_cpu(x)	___constant_swab32((__u32)(x))
#define	__constant_cpu_to_le16(x)	___constant_swab16((__u16)(x))
#define	__constant_le16_to_cpu(x)	___constant_swab16((__u16)(x))
#define	__constant_cpu_to_be64(x)	((__u64)(x))
#define	__constant_be64_to_cpu(x)	((__u64)(x))
#define	__constant_cpu_to_be32(x)	((__u32)(x))
#define	__constant_be32_to_cpu(x)	((__u32)(x))
#define	__constant_cpu_to_be16(x)	((__u16)(x))
#define	__constant_be16_to_cpu(x)	((__u16)(x))

/* runtime forms the core does not already spell out */
#define	__cpu_to_le16p(p)	__swab16p(p)
#define	__le16_to_cpup(p)	__swab16p(p)
#define	__cpu_to_be16p(p)	((__u16)be16_to_cpu(*(const __be16 *)(p)))
#define	__be16_to_cpup(p)	((__u16)be16_to_cpu(*(const __be16 *)(p)))

/* in-situ forms: the big-endian values need no movement on this target */
#define	__cpu_to_le16s(p)	__swab16s(p)
#define	__le16_to_cpus(p)	__swab16s(p)
#define	__cpu_to_le32s(p)	__swab32s(p)
#define	__le32_to_cpus(p)	__swab32s(p)
#define	__cpu_to_le64s(p)	__swab64s(p)
#define	__le64_to_cpus(p)	__swab64s(p)
#define	__cpu_to_be16s(p)	do { } while (0)
#define	__be16_to_cpus(p)	do { } while (0)
#define	__cpu_to_be32s(p)	do { } while (0)
#define	__be32_to_cpus(p)	do { } while (0)
#define	__cpu_to_be64s(p)	do { } while (0)
#define	__be64_to_cpus(p)	do { } while (0)

#endif /* _LINUX_BYTEORDER_BIG_ENDIAN_H */
