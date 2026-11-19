/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/unaligned.h
 * @brief   The unaligned little-endian load and store macros a driver's firmware
 *          record accessors are built with.
 *
 * @design declaration-only compatibility face. The get_unaligned_le16/32/64 and
 *         put_unaligned_le32/64 macros are part of the core contract every file of
 *         this face includes, so a consumer that reaches them through this header
 *         names the same declarations the core publishes. This header exists
 *         because the kernel's own spelling is what its sources include.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_UNALIGNED_H_
#define _LINUXKPI_LINUX_UNALIGNED_H_

#include "../../linuxkpi_core.h"

/*
 * [LNX] include/linux/unaligned.h:76-83: the 24-bit big-endian load. It is the
 * only big-endian form the compiled set names (brcmfmac's P2P action-frame
 * parsing reads a three-byte value with it, p2p.c:232) and it is stated as the
 * interface states it: three byte loads shifted into a u32, which is also what
 * an unaligned-safe load means on this 32-bit target.
 */
static __always_inline __unused u32
__get_unaligned_be24(const u8 *p)
{
	return (u32)p[0] << 16 | (u32)p[1] << 8 | (u32)p[2];
}

static __always_inline __unused u32
get_unaligned_be24(const void *p)
{
	return __get_unaligned_be24((const u8 *)p);
}

#endif /* _LINUXKPI_LINUX_UNALIGNED_H_ */
