/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/platform_device.h
 * @brief   The platform device/driver pair and the registration calls.
 *
 * @design declaration-only compatibility face. The interface
 *         (include/linux/platform_device.h) describes the "platform bus": the
 *         devices that are not on a discoverable bus are described statically by
 *         the board and matched by name against a driver. The records are the
 *         interface's (struct platform_device embeds its struct device;
 *         struct platform_driver embeds its struct device_driver and carries the
 *         probe/remove callbacks), carried here member for member as far as a
 *         compiled consumer names them.
 *
 *         This port has no platform bus and no statically described devices: the
 *         one device model it has is the PCI one, and the SDIO card is
 *         enumerated by the port's own engine. brcmfmac's common.c:610 asks for
 *         the platform data that way (BRCMFMAC_PDATA_NAME) and treats -ENODEV as
 *         "no platform data available", which is exactly the answer a Linux
 *         build without a matching platform device gets. The body therefore
 *         answers -ENODEV (linuxkpi_device.c) rather than success: nothing was
 *         registered, and platform_driver_unregister() has nothing to undo.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_PLATFORM_DEVICE_H_
#define _LINUXKPI_LINUX_PLATFORM_DEVICE_H_

#include "../../linuxkpi_core.h"

#include <linux/device.h>
#include <linux/device/driver.h>

struct platform_device_id;
struct module;

/* [LNX] include/linux/platform_device.h:25-42: the device record. A platform
 * driver reaches its platform data through the embedded device
 * (dev_get_platdata(&pdev->dev)); the resource/id members below the device are
 * not named by the compiled set. */
struct platform_device {
	const char		*name;
	int			id;
	bool			id_auto;
	struct device		dev;

	const struct platform_device_id	*id_entry;
};

/* [LNX] include/linux/platform_device.h:278-286: the driver record. The
 * remove callback returns void and takes the device, which is the shape
 * common.c:590 defines. */
struct platform_driver {
	int	(*probe)(struct platform_device *);
	void	(*remove)(struct platform_device *);
	void	(*shutdown)(struct platform_device *);
	struct device_driver driver;
	const struct platform_device_id *id_table;
};

/* [LNX] include/linux/platform_device.h:307,312-315: the registration pair.
 * platform_driver_probe() hands the probe callback to the bus, which matches it
 * against the devices the board described; there are none here. */
void	platform_driver_unregister(struct platform_driver *drv);
int	__platform_driver_probe(struct platform_driver *driver,
				int (*probe)(struct platform_device *),
				struct module *module, const char *modname);

#define	platform_driver_probe(drv, probe)				\
	__platform_driver_probe((drv), (probe), THIS_MODULE, KBUILD_MODNAME)

#endif /* _LINUXKPI_LINUX_PLATFORM_DEVICE_H_ */
