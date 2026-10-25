/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/device-id/sdio.h
 * @brief   The SDIO device identifier record a driver's id table is built from.
 *
 * @design declaration-only compatibility face, grown per driver. Shape reference
 *         and discipline: components/linuxkpi/include/linux/usb.h:1-20 (the
 *         face carries exactly the interface's record, transcribed with this
 *         project's own types). The layout is the interface's own
 *         (include/linux/device-id/sdio.h:12-19): class, vendor, device and one
 *         driver-private word. SDIO_ANY_ID is the "match anything" value all
 *         three match fields are compared against.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_DEVICE_ID_SDIO_H_
#define _LINUXKPI_LINUX_DEVICE_ID_SDIO_H_

#include "../../../linuxkpi_core.h"

#define	SDIO_ANY_ID	(~0)

/*
 * The interface spells the private word kernel_ulong_t and its match fields
 * __u8/__u16. This face uses the project's fixed-width spellings; the private
 * word keeps the interface's pointer-width so a driver that stores an address in
 * it reads back the same size it wrote.
 */
struct sdio_device_id {
	u8		class;		/* standard interface, or SDIO_ANY_ID */
	u16		vendor;		/* vendor, or SDIO_ANY_ID */
	u16		device;		/* device, or SDIO_ANY_ID */
	unsigned long	driver_data;	/* private to the driver */
};

#endif /* _LINUXKPI_LINUX_DEVICE_ID_SDIO_H_ */
