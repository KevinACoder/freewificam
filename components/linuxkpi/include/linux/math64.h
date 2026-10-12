/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/math64.h
 * @brief   The 64-bit division and scaled multiply helpers a driver's time and
 *          rate arithmetic names.
 *
 * @design declaration-only compatibility face. The 64-bit division family is part of
 *         the core contract (components/linuxkpi/linuxkpi_core.h, next to div_u64())
 *         because the compiled set reaches it through the core rather than through
 *         this header, so it is not restated here. What this header carries is the
 *         scaled multiply-and-divide: the interface's macro over an extern helper
 *         (include/linux/math64.h:345), whose body this port states in
 *         linuxkpi_compat.c. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MATH64_H_
#define _LINUXKPI_LINUX_MATH64_H_

#include "../../linuxkpi_core.h"

/*
 * [LNX] include/linux/math64.h: the 64-bit divide family (div64_u64 at :81-84,
 * div_s64 at :159-163) now lives in the contract header
 * (components/linuxkpi/linuxkpi_core.h, next to div_u64()/div_u64_rem()), because a
 * compiled translation unit reaches the divide vocabulary through the core rather
 * than through this header. It is not restated here.
 */

/* [LNX] include/linux/math64.h:45-52, 328: the multiply-add-divide helper the
 * scaled multiply macro is defined over. The interface's generic body (the
 * 128-bit product, ~0 when the quotient does not fit a u64) belongs to the
 * kernel's own library; this port states it in linuxkpi_compat.c over the
 * compiler's 128-bit integer, which is the same product the interface's
 * __int128 arm computes. The signature is the interface's so a caller reads
 * unchanged. */
u64 mul_u64_add_u64_div_u64(u64 a, u64 b, u64 c, u64 d);

/* [LNX] include/linux/math64.h:345 */
#define	mul_u64_u64_div_u64(a, b, d)	mul_u64_add_u64_div_u64((a), (b), 0, (d))

#endif /* _LINUXKPI_LINUX_MATH64_H_ */
