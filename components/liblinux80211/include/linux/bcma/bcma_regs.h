/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/bcma/bcma_regs.h
 * @brief   The BCMA (Broadcom AMBA) backplane register offsets a core's bring-up
 *          reads and writes.
 *
 * @design declaration-only compatibility face. The interface
 *         (include/linux/bcma/bcma_regs.h) is the register map shared by the
 *         bcma bus core and the chip-specific code that pokes a core's wrapper
 *         directly; what brcmfmac's chip.c names is the two registers a core is
 *         reset through - the IO control register (clock force / clock enable)
 *         and the reset control register - so that is the subset carried here,
 *         with the interface's values.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_BCMA_BCMA_REGS_H_
#define _LINUXKPI_LINUX_BCMA_BCMA_REGS_H_

#include "../../../linuxkpi_core.h"

/* [LNX] include/linux/bcma/bcma_regs.h:29-34: the wrapper's IO control
 * register. FGC forces the gated clocks on and CLK asks for them to stay on,
 * which is the state chip.c waits for after it releases a core from reset. */
#define	BCMA_IOCTL		0x0408		/* IO control */
#define	 BCMA_IOCTL_CLK		0x0001
#define	 BCMA_IOCTL_FGC		0x0002

/* [LNX] include/linux/bcma/bcma_regs.h:41-42: the wrapper's reset control
 * register; the core is held in reset while the bit is set. */
#define	BCMA_RESET_CTL		0x0800
#define	 BCMA_RESET_CTL_RESET	0x0001

#endif /* _LINUXKPI_LINUX_BCMA_BCMA_REGS_H_ */
