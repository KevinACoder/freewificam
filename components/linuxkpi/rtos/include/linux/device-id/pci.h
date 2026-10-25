/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/device-id/pci.h
 * @brief   The PCI device identifier record a driver's id table is built from.
 *
 * @design declaration-only compatibility face, grown per driver. Shape reference
 *         and discipline: components/linuxkpi/include/linux/device-id/sdio.h:1-14
 *         (the face carries exactly the interface's record, transcribed with this
 *         project's own types). The layout is the interface's own
 *         (include/linux/mod_devicetable.h:402-410): the vendor/device pair, the
 *         subsystem pair, the class triple with its mask, and one driver-private
 *         word. PCI_ANY_ID (linux/pci.h) is the "match anything" value the match
 *         fields are compared against.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_DEVICE_ID_PCI_H_
#define _LINUXKPI_LINUX_DEVICE_ID_PCI_H_

#include "../../../linuxkpi_core.h"

/*
 * The interface spells the private word kernel_ulong_t and the id fields __u32.
 * This face uses the project's fixed-width spellings; the private word keeps the
 * interface's pointer-width so a driver that stores an address in it reads back
 * the same size it wrote.
 */
struct pci_device_id {
	u32		vendor;		/* vendor id, or PCI_ANY_ID */
	u32		device;		/* device id, or PCI_ANY_ID */
	u32		subvendor;	/* subsystem vendor id, or PCI_ANY_ID */
	u32		subdevice;	/* subsystem device id, or PCI_ANY_ID */
	u32		class;		/* class/subclass/prog-if triple */
	u32		class_mask;	/* the bits of `class` that must match */
	unsigned long	driver_data;	/* private to the driver */
};

#endif /* _LINUXKPI_LINUX_DEVICE_ID_PCI_H_ */
