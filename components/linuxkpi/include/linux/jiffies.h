/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/jiffies.h
 * @brief   The jiffies clock and the time-comparison helpers a driver's timers
 *          and timeouts are written against.
 *
 * @design declaration-only compatibility face. The clock itself, the jiffies
 *         conversions (msecs_to_jiffies, jiffies_to_msecs, usecs_to_jiffies,
 *         nsecs_to_jiffies) and the wrap-safe comparisons (time_after,
 *         time_before, time_after_eq, time_before_eq) are part of the core
 *         contract every file of this face includes; the tick rate there is the
 *         board's (1 kHz), so a conversion is the interface's own arithmetic.
 *         What this header adds is the two "against now" comparisons the
 *         interface builds on top of the wrap-safe pair (include/linux/jiffies.h:
 *         258, 273) and the round-up-to-a-second spelling (the interface declares
 *         it as an extern; this port has no deferred-timer wheel to round
 *         against, so it is the identity, exactly as the core's relative form
 *         is). Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_JIFFIES_H_
#define _LINUXKPI_LINUX_JIFFIES_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/jiffies.h:258 */
#define	time_is_before_jiffies(a)	time_after(jiffies, (a))
/* [LNX] include/linux/jiffies.h:273 */
#define	time_is_after_jiffies(a)	time_before(jiffies, (a))

/* [LNX] include/linux/jiffies.h:299: `unsigned long round_jiffies(unsigned long
 * j)`. The interface rounds the value up to the next second so many wakeups
 * coalesce; this port runs no deferred-timer wheel, so the rounding is the
 * identity - the same choice the core's round_jiffies_relative() records. */
#define	round_jiffies(j)		((unsigned long)(j))

#endif /* _LINUXKPI_LINUX_JIFFIES_H_ */
