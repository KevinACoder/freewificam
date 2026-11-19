/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/property.h
 * @brief   The firmware/device property accessors a driver names.
 *
 * @design declaration-only compatibility face. The interface
 *         (include/linux/property.h) is the accessor set a driver uses to read
 *         the values a device tree, ACPI table or DMI match attached to a
 *         device (device_property_read_*, device_property_present, the fwnode
 *         entry points).
 *
 *         This port has no device tree and no ACPI (brcmfmac's driver_config.h
 *         states CONFIG_OF/CONFIG_ACPI off, so the driver's of.c and acpi.c are
 *         not in the compiled set), and the one compiled unit that includes this
 *         header - brcmfmac/core.c - names no accessor from it: its
 *         device-property reads went with the OF/ACPI arms. The header is
 *         therefore present as the include the driver writes, carrying no
 *         declarations of its own. A property accessor arrives when a compiled
 *         consumer names one, with the port's answer stated at its site.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_PROPERTY_H_
#define _LINUXKPI_LINUX_PROPERTY_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_PROPERTY_H_ */
