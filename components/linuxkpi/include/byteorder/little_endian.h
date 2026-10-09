/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    byteorder/little_endian.h
 * @brief   The byte-order interface for a little-endian target.
 *
 * @design declaration-only compatibility face, and the one place where the face replaces a
 *         header the vendored tree carries itself. The vendored vendor driver ships
 *         copies of the byteorder headers for platforms with no kernel headers to reach; on this port
 *         the face is the kernel and the include search finds these files first, so the
 *         driver's copies are not compiled. The reason to route them here rather than let
 *         them run: they spell the conversion macros themselves (object-like nicknames
 *         over their own `__`-prefixed bodies), while this project already keeps exactly
 *         one implementation of endianness - the core header's, selected on the target's
 *         byte order - and a second spelling of the same mechanism is a design defect by
 *         the project's own rules. So this file supplies the constant-folded and
 *         `__`-prefixed names the driver's sources use, each pointing at the core's
 *         implementation, and nothing else. The target is little-endian (the project's
 *         boards and the board this driver is ported for); the conversion direction comes
 *         from the core, which is keyed on __BYTE_ORDER. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUX_BYTEORDER_LITTLE_ENDIAN_H
#define _LINUX_BYTEORDER_LITTLE_ENDIAN_H

#include "../linuxkpi_core.h"
#include "swab.h"
#include "generic.h"

#ifndef __LITTLE_ENDIAN
#define	__LITTLE_ENDIAN		1234
#endif
#ifndef __LITTLE_ENDIAN_BITFIELD
#define	__LITTLE_ENDIAN_BITFIELD
#endif

/*
 * The constant-folded forms: initialisers and switch cases use them, so they have to be
 * integer constant expressions - the swap goes through the compiler builtins, which fold.
 */
#define	__constant_htonl(x)		___constant_swab32((__u32)(x))
#define	__constant_ntohl(x)		___constant_swab32((__u32)(x))
#define	__constant_htons(x)		___constant_swab16((__u16)(x))
#define	__constant_ntohs(x)		___constant_swab16((__u16)(x))
#define	__constant_cpu_to_le64(x)	((__u64)(x))
#define	__constant_le64_to_cpu(x)	((__u64)(x))
#define	__constant_cpu_to_le32(x)	((__u32)(x))
#define	__constant_le32_to_cpu(x)	((__u32)(x))
#define	__constant_cpu_to_le16(x)	((__u16)(x))
#define	__constant_le16_to_cpu(x)	((__u16)(x))
#define	__constant_cpu_to_be64(x)	___constant_swab64((__u64)(x))
#define	__constant_be64_to_cpu(x)	___constant_swab64((__u64)(x))
#define	__constant_cpu_to_be32(x)	___constant_swab32((__u32)(x))
#define	__constant_be32_to_cpu(x)	___constant_swab32((__u32)(x))
#define	__constant_cpu_to_be16(x)	___constant_swab16((__u16)(x))
#define	__constant_be16_to_cpu(x)	___constant_swab16((__u16)(x))

/* the runtime forms the core does not already spell out */
#define	__cpu_to_le16p(p)	((__u16)cpu_to_le16(*(const __u16 *)(p)))	/* core: le16_to_cpup */
#define	__le16_to_cpup(p)	((__u16)le16_to_cpu(*(const __le16 *)(p)))
#define	__cpu_to_be16p(p)	__swab16p(p)
#define	__be16_to_cpup(p)	__swab16p(p)

/* the in-situ forms: little-endian values need no movement on this target, which is
 * exactly what the interface's own little-endian header says */
#define	__cpu_to_le16s(p)	do { } while (0)
#define	__le16_to_cpus(p)	do { } while (0)
#define	__cpu_to_le32s(p)	do { } while (0)
#define	__le32_to_cpus(p)	do { } while (0)
#define	__cpu_to_le64s(p)	do { } while (0)
#define	__le64_to_cpus(p)	do { } while (0)
#define	__cpu_to_be16s(p)	__swab16s(p)
#define	__be16_to_cpus(p)	__swab16s(p)
#define	__cpu_to_be32s(p)	__swab32s(p)
#define	__be32_to_cpus(p)	__swab32s(p)
#define	__cpu_to_be64s(p)	__swab64s(p)
#define	__be64_to_cpus(p)	__swab64s(p)

#endif /* _LINUX_BYTEORDER_LITTLE_ENDIAN_H */
