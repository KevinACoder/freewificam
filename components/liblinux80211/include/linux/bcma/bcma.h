/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/bcma/bcma.h
 * @brief   The BCMA core identifiers and the register map they are addressed
 *          through.
 *
 * @design declaration-only compatibility face. The interface
 *         (include/linux/bcma/bcma.h) is the bcma bus driver's own header: the
 *         device/driver records, the core enumeration table and one
 *         BCMA_CORE_<name> number per core a Broadcom chip can carry. brcmfmac
 *         compiles chip.c as its own chip-table reader: it walks the cores
 *         through the bus core's ops (brcmf_buscore) and compares the core id
 *         it read against the BCMA_CORE_* numbers, without ever binding a bcma
 *         device. The numbers are therefore the whole of what this port needs
 *         from the header, and they are carried with the interface's values;
 *         the records arrive with a consumer that names one.
 *
 *         bcma_regs.h is included because the interface's header includes it
 *         (include/linux/bcma/bcma.h:12): that is how the register offsets reach
 *         a unit that names only bcma.h.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_BCMA_BCMA_H_
#define _LINUXKPI_LINUX_BCMA_BCMA_H_

#include "../../../linuxkpi_core.h"

/* [LNX] include/linux/bcma/bcma.h:9,12: the register map and the chipcommon
 * core's bits, both reached through this header on a Linux build. */
#include <linux/bcma/bcma_driver_chipcommon.h>
#include <linux/bcma/bcma_regs.h>

/* [LNX] include/linux/bcma/bcma.h:97-159, the core ids the chip table names.
 * The numbering is the backplane's (0x800 is the chipcommon core every chip
 * starts with), not a local choice. */
#define	BCMA_CORE_CHIPCOMMON	0x800	/* the chip's common core */
#define	BCMA_CORE_INTERNAL_MEM	0x80E	/* internal memory core */
#define	BCMA_CORE_80211		0x812	/* the wireless MAC core */
#define	BCMA_CORE_PMU		0x827	/* the chip's power management unit */
#define	BCMA_CORE_SDIO_DEV	0x829	/* the SDIO device core */
#define	BCMA_CORE_ARM_CM3	0x82A	/* ARM Cortex-M3 (the dongle CPU) */
#define	BCMA_CORE_PCIE2		0x83C	/* PCI Express Gen2 */
#define	BCMA_CORE_ARM_CR4	0x83E	/* ARM Cortex-R4 */
#define	BCMA_CORE_GCI		0x840	/* the GCI core */
#define	BCMA_CORE_ARM_CA7	0x847	/* ARM Cortex-A7 */
#define	BCMA_CORE_SYS_MEM	0x849	/* the system memory core */

#endif /* _LINUXKPI_LINUX_BCMA_BCMA_H_ */
