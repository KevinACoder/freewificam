/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/bcm47xx_nvram.h
 * @brief   The BCM47xx board NVRAM reader.
 *
 * @design this is the interface's own header (include/linux/bcm47xx_nvram.h)
 *         transcribed at its CONFIG_BCM47XX_NVRAM-off shape. The interface
 *         declares two arms: with CONFIG_BCM47XX_NVRAM the functions read the
 *         board variables out of a BCM47xx SoC's flash-resident NVRAM; without
 *         it the same names are `static inline` answers that say "this platform
 *         has no such NVRAM" - get_contents() returns NULL and
 *         release_contents() does nothing.
 *
 *         brcmfmac's firmware.c:557 calls bcm47xx_nvram_get_contents() as one of
 *         the three places a missing NVRAM file can come from (the firmware/NVRAM
 *         file, the board NVRAM, the EFI variable). The STM32F429 is not a
 *         BCM47xx SoC and has no such flash window, and this port's face states
 *         CONFIG_BCM47XX_NVRAM off, so the interface's answer is the NULL one:
 *         the driver falls through to its optional-file arm, which is what a
 *         Linux build of this driver on any non-BCM47xx board does. Nothing is
 *         invented to make the call look successful.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_BCM47XX_NVRAM_H_
#define _LINUXKPI_LINUX_BCM47XX_NVRAM_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/bcm47xx_nvram.h:43-48, the CONFIG-off arm's two names. */
static inline char *
bcm47xx_nvram_get_contents(size_t *val_len)
{
	(void)val_len;

	return NULL;
}

static inline void
bcm47xx_nvram_release_contents(char *nvram)
{
	(void)nvram;
}

#endif /* _LINUXKPI_LINUX_BCM47XX_NVRAM_H_ */
