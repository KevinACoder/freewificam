/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Minimal HAL configuration for the F429 SDIO low-level driver.
 *
 * The imported ST LL SDMMC driver needs the HAL types and the SDIO register
 * definitions, not the high-level HAL SD memory-card driver.  This
 * configuration selects only that low-level face: the device header (which
 * defines SDIO), the HAL definitions, and the LL SDMMC header.  The project
 * asserts are compiled out; the driver's parameter checks are structural.
 */
#ifndef STM32F4XX_HAL_CONF_H
#define STM32F4XX_HAL_CONF_H

#define USE_HAL_DRIVER
#define HAL_SD_MODULE_ENABLED

#include "stm32f4xx.h"
#include "stm32f4xx_hal_def.h"
#include "stm32f4xx_ll_sdmmc.h"

#ifndef assert_param
#define assert_param(expr) ((void)0U)
#endif

#endif /* STM32F4XX_HAL_CONF_H */
