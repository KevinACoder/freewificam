/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/math64.h
 * @brief   The 64-bit division and scaled multiply helpers a driver's time and
 *          rate arithmetic names.
 *
 * @design declaration-only compatibility face. The 64-bit-by-64-bit divide is the
 *         interface's own inline shape (include/linux/math64.h:81, the
 *         BITS_PER_LONG == 64 arm), so it is stated as an inline here; the scaled
 *         multiply-and-divide is the interface's macro over an extern helper
 *         (include/linux/math64.h:345), whose body belongs to the kernel's
 *         library. The interface's own inline divide is carried, the extern
 *         helper is declared, and the macro composes them exactly as the
 *         interface does. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MATH64_H_
#define _LINUXKPI_LINUX_MATH64_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/math64.h:81-84 */
static __always_inline __unused u64
div64_u64(u64 dividend, u64 divisor)
{
	return dividend / divisor;
}

/* [LNX] include/linux/math64.h:45-52, 328: the multiply-add-divide helper the
 * scaled multiply macro is defined over. The interface's generic body (the
 * 128-bit product, ~0 when the quotient does not fit a u64) belongs to the
 * kernel's own library and has no counterpart on this port yet; the signature is
 * the interface's so a caller reads unchanged. */
u64 mul_u64_add_u64_div_u64(u64 a, u64 b, u64 c, u64 d);

/* [LNX] include/linux/math64.h:345 */
#define	mul_u64_u64_div_u64(a, b, d)	mul_u64_add_u64_div_u64((a), (b), 0, (d))

#endif /* _LINUXKPI_LINUX_MATH64_H_ */
