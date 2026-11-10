/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/hrtimer_types.h
 * @brief   The high-resolution timer record and its enumerations, split from
 *          linux/hrtimer.h the way the interface splits them.
 *
 * @design declaration-only compatibility face with a fixed-size record.  The
 *         interface's struct hrtimer is an rb-tree node, an expiry, a callback
 *         and a base pointer; this port routes hrtimers onto the same OS timer
 *         service the ordinary timers use (the component's callout shell), so
 *         the record here is opaque storage large enough for that shell and the
 *         bookkeeping, and the bodies are S1's.  The real include/net/cfg80211.h
 *         embeds the record by value in struct wiphy_hrtimer_work; the
 *         interface's include chain reaches this header through
 *         include/linux/sched.h and the timer headers, which this face's
 *         include/linux/ethtool.h carries (cfg80211's first include).
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_HRTIMER_TYPES_H_
#define _LINUXKPI_LINUX_HRTIMER_TYPES_H_

#include "../../linuxkpi_core.h"
#include <linux/ktime.h>

/* [LNX] include/linux/hrtimer.h: what a callback asks the timer subsystem to do. */
enum hrtimer_restart {
	HRTIMER_NORESTART,
	HRTIMER_RESTART,
};

/* [LNX] include/uapi/linux/time.h + include/linux/time.h: the clock ids the
 * wiphy hrtimer work names (CLOCK_MONOTONIC is in linux/timekeeping.h). */
#ifndef CLOCK_MONOTONIC
#define	CLOCK_MONOTONIC		1
#endif
#ifndef CLOCK_BOOTTIME
#define	CLOCK_BOOTTIME		7
#endif

/* [LNX] include/linux/hrtimer.h: the mode the setup/start calls take.  The
 * interface spells it as an enum (and derives the pin/soft bits from it); the
 * port carries the enumerators the compiled stack names. */
enum hrtimer_mode {
	HRTIMER_MODE_ABS		= 0x00,
	HRTIMER_MODE_REL		= 0x01,
	HRTIMER_MODE_PINNED		= 0x02,
	HRTIMER_MODE_SOFT		= 0x04,
	HRTIMER_MODE_HARD		= 0x08,
	HRTIMER_MODE_ABS_PINNED		= 0x00 | 0x02,
	HRTIMER_MODE_REL_PINNED		= 0x01 | 0x02,
	HRTIMER_MODE_ABS_SOFT		= 0x00 | 0x04,
	HRTIMER_MODE_REL_SOFT		= 0x01 | 0x04,
	HRTIMER_MODE_ABS_PINNED_SOFT	= 0x00 | 0x02 | 0x04,
	HRTIMER_MODE_REL_PINNED_SOFT	= 0x01 | 0x02 | 0x04,
	HRTIMER_MODE_ABS_HARD		= 0x00 | 0x08,
	HRTIMER_MODE_REL_HARD		= 0x01 | 0x08,
	HRTIMER_MODE_ABS_PINNED_HARD	= 0x00 | 0x02 | 0x08,
	HRTIMER_MODE_REL_PINNED_HARD	= 0x01 | 0x02 | 0x08,
};

/* [LNX] include/linux/hrtimer.h: the record.  Opaque storage here: the
 * component's callout shell (14 words, linuxkpi_impl.h) plus two bookkeeping
 * words.  A caller only embeds it and passes its address, so the size is the
 * contract, not the layout. */
struct hrtimer {
	u64	_storage[9];
} __aligned(8);

#endif /* _LINUXKPI_LINUX_HRTIMER_TYPES_H_ */
