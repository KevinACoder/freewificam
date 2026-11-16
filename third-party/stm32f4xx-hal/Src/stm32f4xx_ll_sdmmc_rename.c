/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Build the imported ST LL SDMMC source with its SDIO_Init symbol under a
 * private name.  The imported fsl card service exports SDIO_Init too (the
 * card-level init), so a plain compile of both files collides at link time.
 * The vendor source is byte-identical; this wrapper adds only the rename.
 */
#define SDIO_Init STM32F4_SDIO_LL_Init
#include "stm32f4xx_ll_sdmmc.c"
