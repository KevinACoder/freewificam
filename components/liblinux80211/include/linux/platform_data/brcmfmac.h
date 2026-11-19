/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/platform_data/brcmfmac.h
 * @brief   The platform-data records a board hands the brcmfmac driver.
 *
 * @design declaration-only compatibility face, record for record. The
 *         interface (include/linux/platform_data/brcmfmac.h) is the contract between
 *         a board's platform code and the driver: the bus-type enum, the per-bus
 *         settings, the country-code translation table, the per-device record and
 *         the top-level record the platform device carries. The shapes are
 *         transcribed exactly, member for member and in the interface's order,
 *         because brcmfmac/common.c:545 memcpy()s one record (the SDIO settings)
 *         out of the platform data into its own module-parameter block: a face
 *         that reordered or widened a member would compile and then hand the
 *         driver a different layout than the one it copies.
 *
 *         On this port no board registers such a record (there is no platform
 *         bus; see linux/platform_device.h), so brcmfmac_pdata stays NULL and the
 *         driver takes its module-parameter and DMI/OF/ACPI arms. The record is
 *         carried because the driver's common.h includes this header.
 * @date    19.11.2026
 */

#ifndef _LINUX_BRCMFMAC_PLATFORM_H
#define _LINUX_BRCMFMAC_PLATFORM_H

#include "../../../linuxkpi_core.h"

/* [LNX] include/linux/platform_data/brcmfmac.h:25-27: the platform device's name
 * and the fixed size of a country-code string in the translation table. */
#define	BRCMFMAC_PDATA_NAME		"brcmfmac"
#define	BRCMFMAC_COUNTRY_BUF_SZ		4

/* [LNX] include/linux/platform_data/brcmfmac.h:51-55: the bus a device record
 * describes. The order is the interface's (the values are compared, not
 * serialised, but the enumeration starts at zero as upstream's does). */
enum brcmf_bus_type {
	BRCMF_BUSTYPE_SDIO,
	BRCMF_BUSTYPE_USB,
	BRCMF_BUSTYPE_PCIE
};

/* [LNX] include/linux/platform_data/brcmfmac.h:93-103: the SDIO bus settings a
 * board can override (txglom size, drive strength, the out-of-band interrupt,
 * and the scatter-gather alignment the host controller needs). */
struct brcmfmac_sdio_pd {
	int		txglomsz;
	unsigned int	drive_strength;
	bool		oob_irq_supported;
	unsigned int	oob_irq_nr;
	unsigned long	oob_irq_flags;
	bool		broken_sg_support;
	unsigned short	sd_head_align;
	unsigned short	sd_sgentry_align;
	void		(*reset)(void);
};

/* [LNX] include/linux/platform_data/brcmfmac.h:113-117: one row of the
 * user-space country code to firmware country code/revision translation. */
struct brcmfmac_pd_cc_entry {
	char	iso3166[BRCMFMAC_COUNTRY_BUF_SZ];
	char	cc[BRCMFMAC_COUNTRY_BUF_SZ];
	s32	rev;
};

/* [LNX] include/linux/platform_data/brcmfmac.h:126-129: the translation table. */
struct brcmfmac_pd_cc {
	int				table_size;
	struct brcmfmac_pd_cc_entry	table[];
};

/* [LNX] include/linux/platform_data/brcmfmac.h:143-153: the per-device record;
 * (id, rev, bus_type) is the key the driver matches a probed chip against. */
struct brcmfmac_pd_device {
	unsigned int		id;
	unsigned int		rev;
	enum brcmf_bus_type	bus_type;
	unsigned int		feature_disable;
	struct brcmfmac_pd_cc	*country_codes;
	union {
		struct brcmfmac_sdio_pd sdio;
	} bus;
};

/* [LNX] include/linux/platform_data/brcmfmac.h:170-175: the top-level record the
 * platform device's platform_data points at. */
struct brcmfmac_platform_data {
	void	(*power_on)(void);
	void	(*power_off)(void);
	char	*fw_alternative_path;
	int	device_count;
	struct brcmfmac_pd_device devices[];
};

#endif /* _LINUX_BRCMFMAC_PLATFORM_H */
