/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdmmc_port.h - the port's own entry points over the imported SDIO stack.
 *
 * The imported units (components/fsl_sdmmc/) are consumed byte-faithfully and
 * are never edited, so everything this port adds to them lives here and in the
 * three .c files beside it: the platform dispatch the upstream tree keeps in
 * its own host/ directory (SDIO_CfgInitialize), the host glue for this board's
 * controller (SDMMCHOST_Init and the ops table), and the SDIO wiring the
 * upstream host glue does not carry at all.
 *
 * @design ports & adapters, the adapter side: this header is the boundary the
 * seam implementation (lkpi_sdio_fsl.c) reaches the imported stack through.
 * Shape reference: the upstream tree's host/fsl_sdmmc.c (the platform dispatch)
 * and host/fdwmmc/fsl_hc_fdwmmc.c (the controller glue), split the same way.
 */

#ifndef SDMMC_PORT_H
#define SDMMC_PORT_H

#include "fsl_sdmmc.h"

/* The card clock this port brings the card up at: 50 MHz, the high-speed mode
 * an SDIO WiFi part runs at on 3.3 V signalling. */
#define SDMMC_PORT_CARD_CLOCK_HZ	50000000U

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

#endif /* SDMMC_PORT_H */
