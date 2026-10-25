/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mmc/host.h
 * @brief   The host record: the controller's bus settings and its limits.
 *
 * @design declaration-only compatibility face, grown per driver. The record
 *         shapes are the interface's own (include/linux/mmc/host.h): mmc_ios is
 *         the current bus setting a driver reads back (clock, timing, bus
 *         width, signalling voltage), and mmc_host is the controller, of which
 *         this face carries the card pointer, the bus settings, the two block
 *         limits the transfer splitter respects, and the exclusive-claim flag.
 *         The block limits are the interface's max_blk_size and max_blk_count;
 *         the engine reads them from the host record, and the host record is
 *         filled from what the backend reported about the controller.
 *         Shape reference for a record-only face:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MMC_HOST_H_
#define _LINUXKPI_LINUX_MMC_HOST_H_

#include "../../../linuxkpi_core.h"

#include <linux/device.h>
#include <linux/mmc/card.h>
#include <linux/mmc/pm.h>

/*
 * The current bus setting. A driver reads back the clock and the timing it
 * negotiated; the engine fills the whole record from what the backend reported.
 */
struct mmc_ios {
	unsigned int	clock;			/* bus clock in Hz */
	unsigned short	vdd;			/* selected voltage range */
	unsigned char	bus_mode;		/* command output mode */
	unsigned char	chip_select;		/* SPI chip select */
	unsigned char	power_mode;		/* power supply mode */
	unsigned char	bus_width;		/* data lines in use */
	unsigned char	timing;			/* timing specification */
	unsigned char	signal_voltage;		/* 3.3 V or 1.8 V */
	unsigned char	drv_type;		/* driver type */
};

#define	MMC_BUSMODE_OPENDRAIN	1
#define	MMC_BUSMODE_PUSHPULL	2

#define	MMC_CS_DONTCARE		0
#define	MMC_CS_HIGH		1
#define	MMC_CS_LOW		2

#define	MMC_POWER_OFF		0
#define	MMC_POWER_UP		1
#define	MMC_POWER_ON		2
#define	MMC_POWER_UNDEFINED	3

#define	MMC_BUS_WIDTH_1		0
#define	MMC_BUS_WIDTH_4		2
#define	MMC_BUS_WIDTH_8		3

#define	MMC_TIMING_LEGACY	0
#define	MMC_TIMING_MMC_HS	1
#define	MMC_TIMING_SD_HS	2
#define	MMC_TIMING_UHS_SDR12	3
#define	MMC_TIMING_UHS_SDR25	4
#define	MMC_TIMING_UHS_SDR50	5
#define	MMC_TIMING_UHS_SDR104	6
#define	MMC_TIMING_UHS_DDR50	7
#define	MMC_TIMING_MMC_DDR52	8
#define	MMC_TIMING_MMC_HS200	9
#define	MMC_TIMING_MMC_HS400	10

#define	MMC_SIGNAL_VOLTAGE_330	0
#define	MMC_SIGNAL_VOLTAGE_180	1
#define	MMC_SIGNAL_VOLTAGE_120	2

#define	MMC_SET_DRIVER_TYPE_B	0
#define	MMC_SET_DRIVER_TYPE_A	1
#define	MMC_SET_DRIVER_TYPE_C	2
#define	MMC_SET_DRIVER_TYPE_D	3

/*
 * The controller. One host carries at most one card in this port, which is what
 * the engine's single-card seam assumes; the card pointer is that relationship.
 */
struct mmc_host {
	struct device		*parent;	/* the controller's parent device */
	struct device		class_dev;	/* the host's own device */
	struct mmc_ios		ios;		/* the current bus setting */

	unsigned int		max_blk_size;	/* largest single block */
	unsigned int		max_blk_count;	/* largest block count in one transfer */

	bool			claimed;	/* exclusively claimed */
	struct mmc_card		*card;		/* the card attached to this host */
	unsigned int		sdio_irqs;	/* number of claimed function IRQs */
	mmc_pm_flag_t		pm_caps;	/* power-management capabilities */
	mmc_pm_flag_t		pm_flags;	/* power-management flags in effect */
};

#define	mmc_hostname(host)	dev_name(&(host)->class_dev)

#endif /* _LINUXKPI_LINUX_MMC_HOST_H_ */
