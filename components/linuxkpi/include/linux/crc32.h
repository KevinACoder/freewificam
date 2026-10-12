/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/crc32.h
 * @brief   The reflected IEEE CRC-32 a driver computes over a firmware record.
 *
 * @design declaration-only compatibility face. The interface declares the
 *         least-significant-bit-first variant (include/linux/crc32.h:31) and
 *         makes the plain name an inline alias for it (include/linux/crc32.h:34);
 *         the body belongs to the kernel's library and has no counterpart on this
 *         port yet. Both spellings are stated, because a caller may reach either,
 *         and the alias is carried inline exactly as the interface carries it.
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_CRC32_H_
#define _LINUXKPI_LINUX_CRC32_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/crc32.h:31: the polynomial is 0xedb88320, bit order
 * least-significant-bit-first, and the function does not invert at either end. */
u32 crc32_le(u32 crc, const void *p, size_t len);

/* [LNX] include/linux/crc32.h:34-37 */
static __always_inline __unused u32
crc32(u32 crc, const void *p, size_t len)
{
	return crc32_le(crc, p, len);
}

#endif /* _LINUXKPI_LINUX_CRC32_H_ */
