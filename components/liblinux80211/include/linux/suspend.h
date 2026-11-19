/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/suspend.h
 * @brief   The system-sleep face: the include a driver writes when it has
 *          suspend/resume callbacks.
 *
 * @design declaration-only compatibility face. The interface (include/linux/suspend.h)
 *         carries the system-wide sleep states (PM_SUSPEND_*), the notifier
 *         chain a driver can subscribe to, and the validity/states helpers.
 *
 *         This port has no system sleep: an image runs until it is reset, and
 *         the platform's power transitions are the board's relay, not a kernel
 *         state (driver_config.h states CONFIG_PM off, so the driver's own
 *         CONFIG_PM_SLEEP arms are closed). The one compiled unit that includes
 *         this header - brcmfmac/bcmsdh.c - names no symbol from it: its
 *         suspend/resume callbacks reach the device model through
 *         DEFINE_SIMPLE_DEV_PM_OPS (linux/pm.h) and its freezer code is behind
 *         `IS_ENABLED(CONFIG_PM_SLEEP)`. The header is present as the include the
 *         driver writes and carries no declaration of its own; the declarations
 *         arrive when a compiled consumer names one.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_SUSPEND_H_
#define _LINUXKPI_LINUX_SUSPEND_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/suspend.h includes <linux/pm.h>; the interface's own
 * include chain is what makes DEFINE_SIMPLE_DEV_PM_OPS and pm_sleep_ptr reach a
 * unit that names only suspend.h. */
#include <linux/pm.h>

#endif /* _LINUXKPI_LINUX_SUSPEND_H_ */
