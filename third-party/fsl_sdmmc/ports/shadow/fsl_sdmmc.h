/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2020 NXP
 * Copyright (c) 2026 freewificam contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal first-party shadow of the SDK's sdmmc dispatch header, narrowed to
 * the instances this port builds. The upstream header pulls in the SD, MMC and
 * SDIO card types behind configuration macros; this port builds the SDIO
 * instance (the WiFi line) and the MMC one (the storage line), and the SD
 * instance is not carried.
 */

#ifndef FSL_SDMMC_H_SHADOW
#define FSL_SDMMC_H_SHADOW

#include "fsl_sdmmc_host.h"

#if defined(CONFIG_FSL_SDMMC_ENABLE_SDIO)
#include "fsl_sdio.h"
#endif

#if defined(CONFIG_FSL_SDMMC_ENABLE_MMC)
#include "fsl_mmc.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_FSL_SDMMC_ENABLE_MMC)
/*!@brief sdmmc (MMC) instance */
typedef struct _sdmmc_mmc_
{
    sdmmchost_t host;
    mmc_card_t card;
} sdmmc_mmc_t;

/*! @brief Configure and initialize host controller for the MMC (eMMC) card. */
status_t MMC_CfgInitialize(sdmmc_mmc_t *sdmmc, sdmmchost_config_t *config);
#endif

#if defined(CONFIG_FSL_SDMMC_ENABLE_SDIO)
/*!@brief sdmmc (SDIO) instance */
typedef struct _sdmmc_sdio_
{
    sdmmchost_t host;
    sdio_card_t card;
    sd_detect_card_t cardDetect;
    sd_io_voltage_t ioVoltage;
    sdio_card_int_t sdioInt;
} sdmmc_sdio_t;

/*! @brief Configure and initialize host controller for the SDIO card. */
status_t SDIO_CfgInitialize(sdmmc_sdio_t *sdmmc, sdmmchost_config_t *config);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FSL_SDMMC_H_SHADOW */
