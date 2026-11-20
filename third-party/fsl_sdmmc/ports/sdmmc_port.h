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

/*
 * Which controller the SDIO card sits on. An image drives exactly one, and the
 * two are different beasts: the rk3568 board's DesignWare MMC block (dwmmc)
 * and the firefly boards' Phytium MCI block (mci). The card bring-up asks
 * SDIO_CfgInitialize for a host type, and the platform dispatch refuses any
 * type it does not have a host glue for - so this names the one glue the image
 * carries. A build overrides it with
 * -DSDMMC_PORT_SDIO_HOST_TYPE=kSDMMCHOST_TYPE_FIREFLY_MCI.
 */
/*
 * There is deliberately NO default. Every controller this port has a glue for
 * exports the same three symbols (SDMMCHOST_Init, FDWMMCHOST_SDIOConfig,
 * FDWMMCHOST_CardIntEnable), so an image that picks the wrong one does not fail
 * to link - it links the other platform's controller code and then behaves like
 * the wrong board. A default here is exactly how that happens silently, so the
 * build fails instead: the image's build must name the controller it carries.
 */
#ifndef SDMMC_PORT_SDIO_HOST_TYPE
#error "SDMMC_PORT_SDIO_HOST_TYPE is not set: name the controller this image's host glue drives (kSDMMCHOST_TYPE_FDWMMC, kSDMMCHOST_TYPE_FIREFLY_MCI, ...)"
#endif

/*
 * Which controller of the family the SDIO card sits on. The board dispatch
 * takes this as the controller index: on firefly v2 the SDIO part is on the
 * second MCI instance (mmc1 @0x28001000) while mmc0 is the TF slot, and
 * pointing the bring-up at the wrong one still answers CMD0 (the slot holds a
 * card) and then fails CMD5 for the reason the wrong card would - it is not an
 * SDIO card. An image overrides it with
 * -DSDMMC_PORT_SDIO_HOST_ID=1.
 */
#ifndef SDMMC_PORT_SDIO_HOST_ID
#define SDMMC_PORT_SDIO_HOST_ID		0U
#endif

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

/* Register the card-interrupt upcall before sdmmc_card_start(): when one is
 * set, the card's user parameters carry an sdio_card_int_t, the imported stack
 * hands it to the host (SDIO_HostInit -> SDMMCHOST_CardIntInit), and the host
 * glue calls it from its controller ISR. That context wakes a thread and
 * nothing else. Without a upcall the bring-up runs the polling path it always
 * ran, bit for bit; NULL clears the request. */
void sdmmc_card_int_set(void (*cb)(void *), void *arg);

/* Turn the card interrupt on or off at the host: set/clear the controller's
 * mask bit and follow the GIC line with it. Enable after the card has been
 * serviced - the SDIO engine's pump calls this as its re-arm after a cycle;
 * disable to fall back to the polling pump. A no-op for a host that was never
 * given a card-interrupt upcall. */
void FDWMMCHOST_CardIntEnable(sdmmchost_t *host, bool enable);

/* The controller state this port's host glue keeps, or NULL when the card is
 * not up. Exposed for the board case's evidence lines (the register fields, the
 * counters and the failure snapshot), not for the stack. */
struct mci_host;
struct mci_host *sdmmc_port_mci_host(void);

/* The SDIO half of the host glue: capability flags, the card's user parameters
 * and the source clock. The upstream tree carries this for its FSDIF controller
 * only; this port supplies the equivalent for the DesignWare MMC controller.
 * Declared here because the platform dispatch calls it. */
status_t FDWMMCHOST_SDIOConfig(sdmmc_sdio_t *sdmmc, sdmmchost_config_t *config);

/* Install this port as the SDIO layer's backend: the seam implementation in
 * lkpi_sdio_fsl.c maps the seven seam operations onto the card service above and
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
