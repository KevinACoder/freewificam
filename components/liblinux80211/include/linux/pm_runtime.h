/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/pm_runtime.h
 * @brief   The runtime power-management entry points a device driver calls.
 *
 * @design declaration-only compatibility face. The interface
 *         (include/linux/pm_runtime.h) is the runtime-PM state machine's driver
 *         surface: get/put, enable/disable, allow/forbid (the "user space may not
 *         suspend this device" switch), the autosuspend helpers and the
 *         status/state queries. It also includes <linux/pm.h>, which is how a
 *         driver that names only pm_runtime.h reaches the dev_pm_ops vocabulary.
 *
 *         What brcmfmac names is the allow/forbid pair: bcmsdh.c:888 and :895
 *         forbid runtime suspend around the bus probe (the SDIO host must not be
 *         powered off under the card) and allow it again when the bus is removed.
 *         This port has no runtime PM - nothing ever tries to suspend a device,
 *         and the device record carries no runtime state beyond the one
 *         `is_prepared` flag a cfg80211 consumer reads - so the pair's bodies
 *         (linuxkpi_device.c) are empty with the interface's meaning: there is no
 *         suspend opportunity to forbid. They must not pretend to switch a state
 *         machine that is not there.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_PM_RUNTIME_H_
#define _LINUXKPI_LINUX_PM_RUNTIME_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/pm_runtime.h:12 includes <linux/pm.h>; the dev_pm_ops
 * table and the pm_sleep_ptr()/pm_ptr() annotations reach a unit through this
 * header, which is why bcmsdh.c needs no second include for them. */
#include <linux/pm.h>

/* [LNX] include/linux/pm_runtime.h:84-85: the user-space suspend permission
 * switch. */
void	pm_runtime_allow(struct device *dev);
void	pm_runtime_forbid(struct device *dev);

#endif /* _LINUXKPI_LINUX_PM_RUNTIME_H_ */
