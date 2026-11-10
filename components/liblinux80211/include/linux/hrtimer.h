/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/hrtimer.h
 * @brief   The high-resolution timer entry points; the record is in
 *          hrtimer_types.h.
 *
 * @design declaration-only compatibility face.  The record and enumerations
 *         are linux/hrtimer_types.h's (the interface's split); the entry points
 *         are the ones the compiled stack names, and their bodies are S1's.
 *         hrtimer_setup() is the 7.x spelling (callback + clock + mode).
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_HRTIMER_H_
#define _LINUXKPI_LINUX_HRTIMER_H_

#include "../../linuxkpi_core.h"
#include <linux/hrtimer_types.h>

void	hrtimer_setup(struct hrtimer *timer,
		      enum hrtimer_restart (*function)(struct hrtimer *),
		      clockid_t clock_id, enum hrtimer_mode mode);
void	hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
			       u64 delta_ns, const enum hrtimer_mode mode);
int	hrtimer_cancel(struct hrtimer *timer);
bool	hrtimer_is_queued(struct hrtimer *timer);
ktime_t	hrtimer_expires_remaining(const struct hrtimer *timer);

#endif /* _LINUXKPI_LINUX_HRTIMER_H_ */
