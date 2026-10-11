/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/device/driver.h
 * @brief   The driver half of the device model: the driver record a bus driver
 *          embeds and the shutdown callback ride on it.
 *
 * @design declaration-only compatibility face, transcribed from the interface's
 *         split-out device-model header (include/linux/device/driver.h, reached through
 *         include/linux/device.h), which is where `struct device_driver` lives in the
 *         interface generation this face follows. What the compiled set names is the
 *         driver's name and its shutdown callback: a bus driver record embeds this
 *         structure and a driver fills `.driver.shutdown`, and the interface's
 *         device-information path reads the name of the driver a device is bound to.
 *         The interface's record also carries the bus, the owner module, the probe /
 *         remove / suspend / resume callbacks, the match tables and the private
 *         pointer; those are not named and drag in headers (bus types, match tables,
 *         power management) with no caller behind them here, so they arrive together
 *         with a unit that names them. The two carried members keep their interface
 *         types and order. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_DEVICE_DRIVER_H_
#define _LINUXKPI_LINUX_DEVICE_DRIVER_H_

#include "../../../linuxkpi_core.h"

struct device;

/* [LNX] include/linux/device/driver.h:100-139 */
struct device_driver {
	const char		*name;

	void (*shutdown) (struct device *dev);
};

#endif /* _LINUXKPI_LINUX_DEVICE_DRIVER_H_ */
