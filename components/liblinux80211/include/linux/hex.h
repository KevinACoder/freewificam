/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/hex.h
 * @brief   The hexadecimal conversion helper a driver's debug path names.
 *
 * @design declaration-only compatibility face. The interface (include/linux/
 *         hex.h) owns the byte-to-hex and hex-to-byte conversions; what the
 *         compiled set names is the string-to-bytes direction. Its body belongs to
 *         the kernel's library and has no counterpart on this port yet, so the
 *         signature is stated as the interface states it. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_HEX_H_
#define _LINUXKPI_LINUX_HEX_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/hex.h:20: `int hex2bin(u8 *dst, const char *src, size_t
 * count);` - returns 0 on success and a negative error code on a malformed
 * input. */
int hex2bin(u8 *dst, const char *src, size_t count);

#endif /* _LINUXKPI_LINUX_HEX_H_ */
