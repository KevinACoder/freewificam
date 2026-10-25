/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/jiffies.h
 * @brief   The jiffies clock and the time-comparison helpers a driver's timers
 *          and timeouts are written against.
 *
 * @design declaration-only compatibility face. The clock itself, the jiffies
 *         conversions (msecs_to_jiffies, jiffies_to_msecs, usecs_to_jiffies,
 *         nsecs_to_jiffies), the wrap-safe comparisons (time_after, time_before,
 *         time_after_eq, time_before_eq) and the two "against now" comparisons the
 *         interface builds on them (time_is_before_jiffies, time_is_after_jiffies)
 *         are part of the core contract every file of this face includes; the tick
 *         rate there is the board's (1 kHz), so a conversion is the interface's own
 *         arithmetic. The round-up-to-a-second spelling (round_jiffies) is the core's
 *         extern function too: this port has no deferred-timer wheel to round
 *         against, so its body is the identity. They are stated in the core rather
 *         than here because a compiled translation unit reaches the clock through
 *         the core, not through this header. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_JIFFIES_H_
#define _LINUXKPI_LINUX_JIFFIES_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_JIFFIES_H_ */
