/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/bcd.h
 * @brief   Binary-coded-decimal conversion under the name a driver includes them.
 *
 * @design declaration-only compatibility face with first-party backing inlines. Linux's
 *         header splits each conversion into a compile-time form and a runtime helper;
 *         this face keeps the same two-form macro so a driver's `bcd2bin()` on a constant
 *         still folds at compile time, and provides the runtime helpers as first-party
 *         `static inline` functions instead of the upstream extern declarations, because
 *         this build has no libc to link them from. The arithmetic is upstream's
 *         (include/linux/bcd.h): the low nibble plus ten times the high nibble.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_LINUX_BCD_H_
#define _LINUXKPI_LINUX_BCD_H_

#include <linux/types.h>
#include <linux/compiler.h>

#define	const_bcd2bin(x)	(((x) & 0x0f) + ((x) >> 4) * 10)
#define	const_bin2bcd(x)	((((x) / 10) << 4) + (x) % 10)
#define	const_bcd_is_valid(x)	(((x) & 0x0f) < 10 && ((x) >> 4) < 10)

static __always_inline __unused unsigned
_bcd2bin(unsigned char val)
{

	return (val & 0x0f) + (val >> 4) * 10;
}

static __always_inline __unused unsigned char
_bin2bcd(unsigned val)
{

	return ((val / 10) << 4) + val % 10;
}

#define	bcd2bin(x)					\
		(__builtin_constant_p((u8)(x)) ?	\
		const_bcd2bin(x) :			\
		_bcd2bin(x))

#define	bin2bcd(x)					\
		(__builtin_constant_p((u8)(x)) ?	\
		const_bin2bcd(x) :			\
		_bin2bcd(x))

#define	bcd_is_valid(x)		const_bcd_is_valid(x)

#endif /* _LINUXKPI_LINUX_BCD_H_ */
