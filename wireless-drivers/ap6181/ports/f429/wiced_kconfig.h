/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wiced_kconfig.h - the profile's Kconfig switches, as the vendor driver sees
 * them.
 *
 * Nothing in this build defines the vendor's build-time switches on the command
 * line: the generated configuration header is the single source of truth, and
 * this file is force-included ahead of every vendor source so the mapping is
 * applied once, in one place, to the whole driver.  Changing a Kconfig option
 * therefore changes one header, and the dependency files rebuild exactly the
 * units that read it.
 */

#ifndef AP6181_WICED_KCONFIG_H
#define AP6181_WICED_KCONFIG_H

#include "freewificam_config.h"

/*
 * The CMSIS device header (the vendor's, used unchanged by the SDIO host) asks
 * the application to name its device family; this is the F429/439 line the
 * board's MCU belongs to.
 */
#ifndef STM32F429_439xx
#define STM32F429_439xx 1
#endif

/* The vendor's StdPeriph build names this to have the device header pull in the
 * peripheral headers (through stm32f4xx_conf.h). */
#ifndef USE_STDPERIPH_DRIVER
#define USE_STDPERIPH_DRIVER 1
#endif

/*
 * This image carries the WLAN firmware only: there is no DCT and no bootloader,
 * and the 43362 computes the PMK in firmware, so the host side of WICED's
 * crypto is not compiled in either.  Both are properties of the port, not
 * things a profile chooses.
 */
#define WICED_DISABLE_BOOTLOADER
#define FIRMWARE_WITH_PMK_CALC_SUPPORT

/*
 * Out-of-band interrupt: the module's GPIO0 is wired to PA0 and WWD drives it
 * as a level interrupt.  The option exists for boards where that line is not
 * connected; there WWD is told to keep the MCU's powersave off, which makes it
 * poll the bus instead of waiting for the line.
 */
#if defined(CONFIG_WICED_AP6181_NO_OOB) && (CONFIG_WICED_AP6181_NO_OOB == 1)
#define WICED_DISABLE_MCU_POWERSAVE
#endif

/* WWD's own diagnostics (WPRINT_*) on the port's console. */
#if defined(CONFIG_WICED_AP6181_LOG) && (CONFIG_WICED_AP6181_LOG == 1)
#define WICED_VERBOSE
#ifndef WPRINT_ENABLE_WWD_ERROR
#define WPRINT_ENABLE_WWD_ERROR
#endif
#ifndef WPRINT_ENABLE_WWD_DEBUG
#define WPRINT_ENABLE_WWD_DEBUG
#endif
#ifndef WPRINT_ENABLE_WWD_INFO
#define WPRINT_ENABLE_WWD_INFO
#endif
#endif

#endif /* AP6181_WICED_KCONFIG_H */
