/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/kernel.h
 * @brief   The small arithmetic and diagnostic helpers Linux code reaches for that
 *          do not belong to one subsystem.
 *
 * @design declaration-only compatibility face, grown per consumer: only the helpers
 *         the compiled drivers use are here, with the interface's own definitions
 *         (include/linux/kernel.h, include/linux/math.h). Nothing OS-specific is
 *         declared here, which is what lets the file stay portable across the project's
 *         targets. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_KERNEL_H_
#define _LINUXKPI_LINUX_KERNEL_H_

#include "../../linuxkpi_core.h"

/* Round x up to the next multiple of y (y a power of two in every caller here). */
#define	roundup(x, y)	((((x) + ((y) - 1)) / (y)) * (y))

/*
 * abs(): the driver calls it (the CFO and gain paths). A platform whose own C
 * surface already declares abs - the board's freestanding minilibc - defines
 * LKPI_ABS_PLATFORM_PROVIDED and the definition here retires, because a static
 * definition after the platform's declaration is not legal C. Both bodies are
 * the same function.
 */
#ifndef LKPI_ABS_PLATFORM_PROVIDED
static inline int abs(int x)
{
	return x < 0 ? -x : x;
}
#endif

#endif /* _LINUXKPI_LINUX_KERNEL_H_ */
