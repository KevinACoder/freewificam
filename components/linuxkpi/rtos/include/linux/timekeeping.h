/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/timekeeping.h
 * @brief   The timestamp records a driver's clock read and cross-timestamp path
 *          name.
 *
 * @design declaration-only compatibility face. The interface's header
 *         (include/linux/timekeeping.h) owns the kernel clock reads and the
 *         cross-timestamp records. The monotonic read the core contract already
 *         carries (linuxkpi_core.h: ktime_get) is not repeated; what this header
 *         adds is the wall-clock id, the second/nanosecond record the interface
 *         builds (include/linux/time64.h), its nanosecond constructor, and the
 *         cross-timestamp record. Of that record only the members a compiled
 *         consumer names are carried, in the interface's order; the counter
 *         member it does not name is left out because the record it drags has no
 *         counterpart here. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_TIMEKEEPING_H_
#define _LINUXKPI_LINUX_TIMEKEEPING_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/uapi/asm-generic/posix_types.h: the clock id type. */
typedef int			clockid_t;

/* [LNX] include/uapi/linux/time.h:49,56 */
#define	CLOCK_REALTIME		0
#define	CLOCK_BOOTTIME		7

/* [LNX] include/linux/time64.h:19-22. The second-count type is a core scalar. */
struct timespec64 {
	time64_t	tv_sec;		/* seconds */
	long		tv_nsec;	/* nanoseconds */
};

/* [LNX] include/linux/time64.h:34 */
struct timespec64 ns_to_timespec64(s64 nsec);

/* [LNX] include/linux/timekeeping.h:340-345: the cross-timestamp record. The
 * interface also carries the counter member (struct system_counterval_t) between
 * device and sys_systime; no compiled consumer names it, and the record it drags
 * has no counterpart on this port, so it is not stated. */
struct system_device_crosststamp {
	clockid_t	clock_id;
	ktime_t		device;
	ktime_t		sys_systime;
	ktime_t		sys_monoraw;
};

#endif /* _LINUXKPI_LINUX_TIMEKEEPING_H_ */
