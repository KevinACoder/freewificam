/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/dmi.h
 * @brief   The system-identification surface a driver uses to recognise a board by its
 *          firmware strings.
 *
 * @design declaration-only compatibility face, stated with the interface's own
 *         no-DMI answers. This port has no SMBIOS/DMI table and cannot have one: the
 *         interface's own build without the facility defines exactly these three
 *         answers (include/linux/dmi.h, its no-facility arm: no matching system, no
 *         system string) - and a driver's board-quirk table then simply never
 *         matches, which is the truth here rather than a stub accident. The identifier
 *         records are the interface's own (include/linux/device-id/dmi.h, which the
 *         interface splits out; the face keeps them here because only this path is
 *         included) with their exact field layout, because a quirk table is data a
 *         driver submits by value.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_DMI_H_
#define _LINUXKPI_LINUX_DMI_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/device-id/dmi.h:9-34 */
enum dmi_field {
	DMI_NONE,
	DMI_BIOS_VENDOR,
	DMI_BIOS_VERSION,
	DMI_BIOS_DATE,
	DMI_BIOS_RELEASE,
	DMI_EC_FIRMWARE_RELEASE,
	DMI_SYS_VENDOR,
	DMI_PRODUCT_NAME,
	DMI_PRODUCT_VERSION,
	DMI_PRODUCT_SERIAL,
	DMI_PRODUCT_UUID,
	DMI_PRODUCT_SKU,
	DMI_PRODUCT_FAMILY,
	DMI_BOARD_VENDOR,
	DMI_BOARD_NAME,
	DMI_BOARD_VERSION,
	DMI_BOARD_SERIAL,
	DMI_BOARD_ASSET_TAG,
	DMI_CHASSIS_VENDOR,
	DMI_CHASSIS_TYPE,
	DMI_CHASSIS_VERSION,
	DMI_CHASSIS_SERIAL,
	DMI_CHASSIS_ASSET_TAG,
	DMI_STRING_MAX,
	DMI_OEM_STRING,	/* special case - will not be in dmi_ident */
};

/* [LNX] include/linux/device-id/dmi.h:37-41 */
struct dmi_strmatch {
	unsigned char slot:7;
	unsigned char exact_match:1;
	char substr[79];
};

/* [LNX] include/linux/device-id/dmi.h:43-48 */
struct dmi_system_id {
	int (*callback)(const struct dmi_system_id *);
	const char *ident;
	struct dmi_strmatch matches[4];
	void *driver_data;
};

/* [LNX] include/linux/device-id/dmi.h:7-8 */
#define	DMI_MATCH(a, b)		{ .slot = a, .substr = b }
#define	DMI_EXACT_MATCH(a, b)	{ .slot = a, .substr = b, .exact_match = 1 }

/* [LNX] include/linux/dmi.h:142-143,171: the interface's no-DMI answers. A quirk table
 * passes through unchanged and matches nothing; a caller then keeps its default. */
static __always_inline __unused int
dmi_check_system(const struct dmi_system_id *list)
{
	(void)list;
	return 0;
}

static __always_inline __unused const char *
dmi_get_system_info(int field)
{
	(void)field;
	return NULL;
}

static __always_inline __unused const struct dmi_system_id *
dmi_first_match(const struct dmi_system_id *list)
{
	(void)list;
	return NULL;
}

#endif /* _LINUXKPI_LINUX_DMI_H_ */
