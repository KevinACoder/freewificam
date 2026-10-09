/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    asm/byteorder.h
 * @brief   The architecture's byte-order entry point: pick the endianness variant of
 *          the byte-order macros.
 *
 * @design declaration-only compatibility face, transcribed from the interface's arm64
 *         entry point (arch/arm64/include/uapi/asm/byteorder.h:16-23), which selects the
 *         big-endian variant of the byte-order macros on the big-endian ABI and the
 *         little-endian variant otherwise. The two variants already exist as first-party
 *         files (components/linuxkpi/include/byteorder/), and they are the single
 *         implementation of the conversions in this project, so this header only makes
 *         the choice the interface makes; it carries no conversion of its own. The
 *         interface spells the two paths under its linux/byteorder directory; here the
 *         component's own byteorder tree is that same directory (it is the one the
 *         include search reaches before the vendored copy), so the paths below name it
 *         directly rather than a second copy under linux/.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_ASM_BYTEORDER_H_
#define _LINUXKPI_ASM_BYTEORDER_H_

#ifdef __AARCH64EB__
#include <byteorder/big_endian.h>
#else
#include <byteorder/little_endian.h>
#endif

#endif /* _LINUXKPI_ASM_BYTEORDER_H_ */
