/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    byteorder/generic.h
 * @brief   The generic (host-order-neutral) half of the byte-order interface.
 *
 * @design declaration-only compatibility face. In the interface this header is the
 *         mechanism's centre: it defines the generic conversion macros over the
 *         `__`-prefixed primitives that an architecture header provides, and then the
 *         nickname spellings of them. This project's single implementation of that
 *         mechanism already lives in the core header (le16_to_cpu/cpu_to_be16 and the
 *         rest, selected on the target's byte order), and the vendored vendor driver's own
 *         copy of this header would restate the nicknames and defeat the core's
 *         function-like macros - so the driver's copy is shadowed by this file, whose
 *         whole content is the alias set the driver expects to find, each name pointing at
 *         the core's one implementation. The names the core already spells are not
 *         repeated; only the aliases it does not carry are added. See
 *         byteorder/little_endian.h for the full note. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUX_BYTEORDER_GENERIC_H
#define _LINUX_BYTEORDER_GENERIC_H

#include "../linuxkpi_core.h"
#include "swab.h"

/* the `__`-prefixed primitives the core does not carry (it spells the 16/32-bit ones) */
#define	__cpu_to_le64(x)	cpu_to_le64(x)
#define	__le64_to_cpu(x)	le64_to_cpu(x)
#define	__cpu_to_be16(x)	cpu_to_be16(x)
#define	__cpu_to_be32(x)	cpu_to_be32(x)
#define	__cpu_to_be64(x)	cpu_to_be64(x)
#define	__be16_to_cpu(x)	be16_to_cpu(x)
#define	__be32_to_cpu(x)	be32_to_cpu(x)
#define	__be64_to_cpu(x)	be64_to_cpu(x)

/* the pointer and in-situ forms of the same set */
#define	__cpu_to_le32p(p)	((__u32)cpu_to_le32(*(const __u32 *)(p)))
#define	__cpu_to_le64p(p)	((__u64)cpu_to_le64(*(const __u64 *)(p)))
#define	__le32_to_cpup(p)	((__u32)le32_to_cpu(*(const __le32 *)(p)))
#define	__le64_to_cpup(p)	((__u64)le64_to_cpu(*(const __le64 *)(p)))
#define	__cpu_to_be32p(p)	__swab32p(p)
#define	__cpu_to_be64p(p)	__swab64p(p)
#define	__be32_to_cpup(p)	__swab32p(p)
#define	__be64_to_cpup(p)	__swab64p(p)

#endif /* _LINUX_BYTEORDER_GENERIC_H */
