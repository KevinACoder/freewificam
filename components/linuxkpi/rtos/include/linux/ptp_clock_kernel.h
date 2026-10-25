/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/ptp_clock_kernel.h
 * @brief   The PTP hardware-clock description and registration calls a driver's
 *          clock path names.
 *
 * @design declaration-only compatibility face. The interface's header
 *         (include/linux/ptp_clock_kernel.h) owns the clock-description record,
 *         its callback set and the registration calls. The record is a public
 *         shape a driver fills member by member; what is carried here are the
 *         members a compiled consumer names, in the interface's order, with the
 *         callback signatures transcribed from the interface. The registration
 *         calls have no PTP core on this port, so the interface's own
 *         !IS_ENABLED(CONFIG_PTP_1588_CLOCK) arm is carried: register answers NULL,
 *         unregister answers 0, index answers -1 - which is exactly the interface's
 *         answer on a kernel built without the clock core, and the driver's PTP
 *         path is unreachable on this line (no pps/PHC source is registered).
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_PTP_CLOCK_KERNEL_H_
#define _LINUXKPI_LINUX_PTP_CLOCK_KERNEL_H_

#include "../../linuxkpi_core.h"
#include <linux/timekeeping.h>

struct device;
struct module;

/* [LNX] include/linux/ptp_clock_kernel.h:18 */
#define	PTP_CLOCK_NAME_LEN	32

/* [LNX] include/linux/ptp_clock_kernel.h:229 */
struct ptp_clock;

/* [LNX] include/linux/ptp_clock_kernel.h:193-231: the clock-description record,
 * carrying the members the compiled set names in the interface's order. */
struct ptp_clock_info {
	struct module	*owner;
	char		name[PTP_CLOCK_NAME_LEN];
	s32		max_adj;
	int		(*adjfine)(struct ptp_clock_info *ptp, long scaled_ppm);
	int		(*adjtime)(struct ptp_clock_info *ptp, s64 delta);
	int		(*gettime64)(struct ptp_clock_info *ptp,
				     struct timespec64 *ts);
	int		(*getcrosststamp)(struct ptp_clock_info *ptp,
					  struct system_device_crosststamp *cts);
	int		(*settime64)(struct ptp_clock_info *p,
				     const struct timespec64 *ts);
};

/* [LNX] include/linux/ptp_clock_kernel.h:345,354,372 (the core entries) and
 * :445-460 (the !CONFIG_PTP_1588_CLOCK arm, which is what this port carries: no
 * PTP clock core exists here, so a registration has no object to return and the
 * driver's iwl_mvm_ptp_init/remove path is unreachable on this line). */
static __always_inline __unused struct ptp_clock *
ptp_clock_register(struct ptp_clock_info *info, struct device *parent)
{
	(void)info;
	(void)parent;
	return NULL;
}

static __always_inline __unused int
ptp_clock_unregister(struct ptp_clock *ptp)
{
	(void)ptp;
	return 0;
}

static __always_inline __unused int
ptp_clock_index(struct ptp_clock *ptp)
{
	(void)ptp;
	return -1;
}

#endif /* _LINUXKPI_LINUX_PTP_CLOCK_KERNEL_H_ */
