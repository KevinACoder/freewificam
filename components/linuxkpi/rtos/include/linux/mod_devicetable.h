/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mod_devicetable.h
 * @brief   The device-identity table vocabulary a driver's module description
 *          reaches through this name.
 *
 * @design declaration-only compatibility face. The interface's header carries the
 *         identity records of every bus a module may be matched on (PCI, platform,
 *         OF, ACPI, and the rest); this project states the bus records a compiled
 *         driver actually names under the bus-specific headers it splits them into
 *         (components/linuxkpi/include/linux/device-id/pci.h and its siblings), so
 *         this header is the include-path spelling of that set rather than a second
 *         copy. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MOD_DEVICETABLE_H_
#define _LINUXKPI_LINUX_MOD_DEVICETABLE_H_

#include "../../linuxkpi_core.h"
#include <linux/device-id/pci.h>

#endif /* _LINUXKPI_LINUX_MOD_DEVICETABLE_H_ */
