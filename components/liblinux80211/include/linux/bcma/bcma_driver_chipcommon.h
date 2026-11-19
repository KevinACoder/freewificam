/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/bcma/bcma_driver_chipcommon.h
 * @brief   The chipcommon core's capability and control bits.
 *
 * @design declaration-only compatibility face. The interface
 *         (include/linux/bcma/bcma_driver_chipcommon.h) is the bcma bus driver's
 *         view of the chipcommon core: the register offsets, the capability bits
 *         read back from it, the PMU control bits and the SROM/OTP control bits,
 *         plus the driver records the bcma core driver binds with.
 *
 *         brcmfmac reads the chipcommon core through its own buscore ops, not
 *         through a bcma device, so the records play no part here; what chip.c
 *         names is the capability test that decides whether the chip carries an
 *         always-on buffer (AOB) in its PMU, the PMU reload bit it writes after a
 *         reset, and the OTP bits that tell it where the chip's configuration
 *         words come from. Those are the subset carried, with the interface's
 *         values. The interface's header reaches a unit through linux/bcma/bcma.h,
 *         which includes it (include/linux/bcma/bcma.h:9); the face keeps that
 *         include.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_BCMA_BCMA_DRIVER_CHIPCOMMON_H_
#define _LINUXKPI_LINUX_BCMA_BCMA_DRIVER_CHIPCOMMON_H_

#include "../../../linuxkpi_core.h"

/* [LNX] include/linux/bcma/bcma_driver_chipcommon.h:226: the extended-capability
 * bit that says the chip's PMU has an always-on buffer. */
#define	BCMA_CC_CAP_EXT_AOB_PRESENT	0x00000040

/* [LNX] include/linux/bcma/bcma_driver_chipcommon.h:273-274: the SROM control
 * bits: the OTP select and the "OTP is present" status a chip's configuration
 * words are read through. */
#define	BCMA_CC_SROM_CONTROL_OTPSEL	0x00000010
#define	BCMA_CC_SROM_CONTROL_OTP_PRESENT	0x00000020

/* [LNX] include/linux/bcma/bcma_driver_chipcommon.h:328-329: the PMU control
 * register's "reload power-on-reset values" field and value, written after a
 * core reset to bring the chip's power state back to its reset values. */
#define	BCMA_CC_PMU_CTL_RES_SHIFT	13
#define	BCMA_CC_PMU_CTL_RES_RELOAD	0x2	/* reload POR values */

#endif /* _LINUXKPI_LINUX_BCMA_BCMA_DRIVER_CHIPCOMMON_H_ */
