/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdmmc_port.h - the port's own entry points over the imported SDIO/MMC stack.
 *
 * The imported units (components/fsl_sdmmc/) are consumed byte-faithfully and
 * are never edited, so everything this port adds to them lives here and in the
 * .c files beside it: the platform dispatch the upstream tree keeps in its own
 * host/ directory (SDIO_CfgInitialize for the WiFi socket, MMC_CfgInitialize
 * for the soldered eMMC), the host glue for this board's controllers
 * (SDMMCHOST_Init and the ops table), and the wiring the upstream host glue
 * does not carry at all.
 *
 * The two card types live in separate files (sdmmc_card.c, sdmmc_mmc_card.c)
 * and separate host glues (sdmmc_host_dwmmc.c, sdmmc_host_dwcmshc.c), because
 * each image drives exactly one of them: the stack's own contract is one
 * SDMMCHOST_Init per image, and a build that carried both would link two.
 *
 * @design ports & adapters, the adapter side: this header is the boundary the
 * seam implementation (lkpi_sdio_fsl.c) and the block-device registry
 * (ports/fatfs) reach the imported stack through.
 * Shape reference: the upstream tree's host/fsl_sdmmc.c (the platform dispatch)
 * and host/fdwmmc/fsl_hc_fdwmmc.c (the controller glue), split the same way.
 */

#ifndef SDMMC_PORT_H
#define SDMMC_PORT_H

#include "fsl_sdmmc.h"

/* The card clock this port brings the SDIO card up at: 50 MHz, the high-speed
 * mode an SDIO WiFi part runs at on 3.3 V signalling. */
#define SDMMC_PORT_CARD_CLOCK_HZ	50000000U

/* The ceiling this port gives the eMMC card: 50 MHz, the CRU's 50 MHz clock
 * slot, which is the rate the card's high-speed timing runs at here. The stack
 * rounds its per-mode targets down to this, which is also what keeps its mode
 * ladder at high speed rather than reaching for a rate this port's clock chain
 * does not produce yet. */
#define SDMMC_PORT_MMC_CARD_CLOCK_HZ	50000000U

/*
 * The two card types are declared behind the same keys the stack's own
 * configuration uses, because an image carries exactly one of them and the
 * other one's types are not even visible without its header: a build that
 * compiled both declarations would not compile at all.
 */
#if defined(CONFIG_FSL_SDMMC_ENABLE_SDIO)
/* Bring the one SDIO card up: configure the host for it, run the imported
 * enumeration, and leave the card descriptor readable through sdmmc_card().
 * Returns kStatus_Success or an imported-stack status; the seam implementation
 * translates that into the seam's own error family. */
status_t sdmmc_card_start(void);

/* Tear the card down (card then host), releasing the staging buffer. */
void sdmmc_card_stop(void);

/* The card descriptor of the instance sdmmc_card_start() brought up, or NULL
 * before that call. Its fields are the imported stack's, read-only to callers. */
sdio_card_t *sdmmc_card(void);

/* The SDIO half of the host glue: capability flags, the card's user parameters
 * and the source clock. The upstream tree carries this for its FSDIF controller
 * only; this port supplies the equivalent for the DesignWare MMC controller.
 * Declared here because the platform dispatch calls it. */
status_t FDWMMCHOST_SDIOConfig(sdmmc_sdio_t *sdmmc, sdmmchost_config_t *config);

/* Install this port as the SDIO layer's backend: the seam implementation in
 * lkpi_sdio_fsl.c maps the six seam operations onto the card service above and
 * absorbs the host's length discipline. Call once, before the first
 * lkpi_sdio_be_card_init(). */
void lkpi_sdio_fsl_install(void);
#endif /* CONFIG_FSL_SDMMC_ENABLE_SDIO */

#if defined(CONFIG_FSL_SDMMC_ENABLE_MMC)
/* The MMC half of the host glue, the same shape for the eMMC controller: the
 * capability flags, the card's user parameters (8-bit bus, high speed, the
 * fixed 1.8 V IO window, block-count commands) and the source clock. Declared
 * here because the platform dispatch calls it. */
status_t FDWMSHCHOST_MMCConfig(sdmmc_mmc_t *sdmmc, sdmmchost_config_t *config);
#endif /* CONFIG_FSL_SDMMC_ENABLE_MMC */

#endif /* SDMMC_PORT_H */
