/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mmc/pm.h
 * @brief   The power-management flags the SDIO interface publishes.
 *
 * @design declaration-only compatibility face. The flag type and values are the
 *         interface's own (include/linux/mmc/pm.h:4-8): a bitmask of what a host
 *         can keep across a system suspend. A driver reads it through
 *         sdio_get_host_pm_caps() and sets it through sdio_set_host_pm_flags().
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MMC_PM_H_
#define _LINUXKPI_LINUX_MMC_PM_H_

typedef unsigned int mmc_pm_flag_t;

#define	MMC_PM_KEEP_POWER	(1u << 0)	/* keep card power across suspend */
#define	MMC_PM_WAKE_SDIO_IRQ	(1u << 1)	/* wake the host on an SDIO IRQ */
#define	MMC_PM_IGNORE_PM_NOTIFY	(1u << 2)	/* ignore the pm notify */

#endif /* _LINUXKPI_LINUX_MMC_PM_H_ */
