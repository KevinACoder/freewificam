/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdmmc_card - the platform dispatch and the one SDIO instance of this port.
 *
 * The imported stack's own host/ directory holds a dispatch file that walks
 * the SDK's whole controller list (FSDIF, FSDMMC, FDWMMC, FDWMSHC) and links
 * each card structure to its host before allocating the staging buffer every
 * alignment fallback inside the stack copies through. This port has exactly one
 * controller, so the walk collapses to a refusal of anything else - but the
 * part that matters is kept: the staging buffer is allocated here, before any
 * card command runs, because the stack dereferences it from the first
 * capability read onwards and nothing else in the tree allocates it.
 *
 * The SDIO-specific host wiring the upstream tree never wrote (its dispatch
 * arms the FSDIF controller only) is supplied by FDWMMCHOST_SDIOConfig in
 * sdmmc_host_dwmmc.c.
 *
 * @design adapter - ref upstream:third-party/fsl_sdmmc host/fsl_sdmmc.c
 *         (SDIO_CfgInitialize, the structure linking and the staging-buffer
 *         allocation) with the controller walk narrowed to this board's one
 *         host type, and the buffer's alignment raised to a DMA page (see the
 *         note at the allocation).
 */

#include "fsl_sdmmc.h"
#include "sdmmc_port.h"

/* One card per image, matching the seam's shape (one installed backend, one
 * card). The descriptor is large - the function tables and the CIS copies the
 * imported stack fills in - so it is static rather than stack-resident. */
static sdmmc_sdio_t sdmmc_card_dev;
static bool sdmmc_card_up;

status_t SDIO_CfgInitialize(sdmmc_sdio_t *sdmmc, sdmmchost_config_t *config)
{
    assert(sdmmc != NULL);
    assert(config != NULL);

    sdmmchost_t *host       = &sdmmc->host;
    sdio_card_t *card       = &sdmmc->card;
    sd_detect_card_t *card_cd = &sdmmc->cardDetect;
    status_t status         = kStatus_Fail;

    /* link the structures the imported stack walks (the card reaches its host,
     * the host reaches its card, and both reach the card-detect record) */
    host->config     = *config;
    host->cd         = card_cd;
    card->host       = host;
    host->card       = card;
    card->usrParam.cd = card_cd;

    /*
     * The staging buffer. The imported extended-transfer paths copy through it
     * whenever the caller's buffer does not suit the controller, so it must
     * exist before the first capability read.
     *
     * The upstream SDIO arm aligns it to the default block size; this port asks
     * for a DMA page (SDMMC_DATA_BUFFER_ALIGN_CACHE, 4 Kbyte) instead. The
     * reason is this board's engine, not the stack: the controller takes its
     * descriptor buffers page by page, so a block-sized alignment satisfies a
     * one-block staging copy and not a multi-block one.
     */
    card->internalBuffer = SDMMC_OSAMemoryAlignedAllocate((uint32_t)host->config.maxTransSize,
                                                          SDMMC_DATA_BUFFER_ALIGN_CACHE);
    if (card->internalBuffer == NULL)
    {
        return kStatus_OutOfRange;
    }

    (void)memset(card->internalBuffer, 0, host->config.maxTransSize);
    card->internalBufferSize = host->config.maxTransSize;

    host->capability = 0U;

    /* This port drives one controller. An unexpected host type is refused (and
     * the buffer released) rather than silently left without a backend. */
    if (host->config.hostType != (uint32_t)kSDMMCHOST_TYPE_FDWMMC)
    {
        SDMMC_OSAMemoryFree(card->internalBuffer);
        card->internalBuffer     = NULL;
        card->internalBufferSize = 0U;
        return kStatus_SDMMC_HostNotSupport;
    }

    status = FDWMMCHOST_SDIOConfig(sdmmc, config);
    if (status == kStatus_Success)
    {
        /* host bring-up, card detect, CMD0/CMD5/CMD3/CMD7, CCCR and CIS */
        status = SDIO_Init(card);
    }

    return status;
}

status_t sdmmc_card_start(void)
{
    sdmmchost_config_t config;
    status_t status;

    if (sdmmc_card_up)
    {
        return kStatus_Success;
    }

    (void)memset(&sdmmc_card_dev, 0, sizeof(sdmmc_card_dev));

    /*
     * The instance's configuration. Two values carry a decision:
     *
     *  - enableIrq stays false. The card's interrupt line is not wired on this
     *    board and the design brings the card up by polling; a transfer
     *    therefore runs on the polling path of the host glue, and the seam's
     *    irq_pending() reads the card's own pending word over a direct
     *    transfer rather than waiting for a line.
     *  - maxTransSize is the staging buffer's size, and the imported stack also
     *    derives the host's maximum block count from it (maxTransSize /
     *    defBlockSize), so the two stay consistent: the largest block transfer
     *    the stack will assemble is exactly what one staging copy can hold.
     *    That second role is why it is sized for the widest read a card driver
     *    makes rather than for one block: the SDIO engine splits a request into
     *    transfers of at most that block count and, when the caller asked for an
     *    increasing address, advances the address between them. A receive-FIFO
     *    read must not be split that way - the FIFO is a port, not a register
     *    window, so an advanced address reads somewhere else, and the transfer
     *    then reports success while delivering nothing for every block after the
     *    first (which a driver sees as "only the first frame of every aggregate
     *    arrives"). One transfer per receive read is what a Linux host does too,
     *    because its block count comes from the controller, not from a staging
     *    buffer.
     */
    config.hostId         = 0U;
    config.hostType       = (uint32_t)kSDMMCHOST_TYPE_FDWMMC;
    config.cardType       = (uint32_t)kSDMMCHOST_CARD_TYPE_SDIO;
    config.enableIrq      = false;
    config.enableDMA      = true;
    config.prefixedTiming = false;
    config.endianMode     = (uint32_t)kSDMMCHOST_EndianModeLittle;
    /* 16 KiB covers the widest read the RTL8188F driver asks for (its receive
     * buffer caps at 15360 bytes) and keeps the derived block count a whole
     * read: 32 blocks of 512. */
    config.maxTransSize   = 16384U;
    config.defBlockSize   = (size_t)FSL_SDMMC_DEFAULT_BLOCK_SIZE;
    config.cardClock      = SDMMC_PORT_CARD_CLOCK_HZ;
    config.isUHSCard      = false;
    config.sdioCardInt    = NULL;
    config.sdioCardIntArg = NULL;

    status = SDIO_CfgInitialize(&sdmmc_card_dev, &config);
    if (status != kStatus_Success)
    {
        /* A failed bring-up can leave the staging buffer allocated; release it
         * here rather than leave the heap short for the next attempt. The card
         * descriptor is not torn down through SDIO_Deinit, whose first step
         * takes a lock that a failed bring-up may never have created. */
        if (sdmmc_card_dev.card.internalBuffer != NULL)
        {
            SDMMC_OSAMemoryFree(sdmmc_card_dev.card.internalBuffer);
            sdmmc_card_dev.card.internalBuffer     = NULL;
            sdmmc_card_dev.card.internalBufferSize = 0U;
        }
        return status;
    }

    sdmmc_card_up = true;

    return kStatus_Success;
}

sdio_card_t *sdmmc_card(void)
{
    return sdmmc_card_up ? &sdmmc_card_dev.card : NULL;
}

void sdmmc_card_stop(void)
{
    if (!sdmmc_card_up)
    {
        return;
    }

    /* card (reset, deselect, power off, lock released) then host, then the
     * staging buffer - the order the imported stack's own deinit uses */
    SDIO_Deinit(&sdmmc_card_dev.card);
    sdmmc_card_up = false;
}
