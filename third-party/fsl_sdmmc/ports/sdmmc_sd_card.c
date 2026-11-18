/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdmmc_sd_card - the platform dispatch and the one SD card instance of this
 * port.
 *
 * Two faces in one file, the same split the SDIO and MMC halves use:
 *
 *   - SD_CfgInitialize is the dispatch the imported stack expects: the
 *     upstream tree keeps it in its own host/ directory, walking the SDK's
 *     whole controller list and linking each card structure to its host before
 *     allocating the staging buffer every alignment fallback inside the stack
 *     copies through.  This port has exactly one controller, so the walk
 *     collapses to a refusal of anything else - but the staging buffer is
 *     still allocated here, before the first card command.
 *
 *   - sdmmc_sd_card_start and the block calls beside it are this port's own
 *     face (sdmmc_sd.h), the one the board case is adapted to.  The
 *     configuration lives here because every value in it is a statement about
 *     this board's socket and this stack's expectations, and the case knows
 *     neither.
 *
 * @design adapter - ref upstream:third-party/fsl_sdmmc host/fsl_sdmmc.c
 *         (SD_CfgInitialize, the structure linking and the staging-buffer
 *         allocation) with the controller walk narrowed to this board's one
 *         host type, and the buffer's alignment raised to a DMA page, as the
 *         SDIO half of this port does.
 */

#include "fsl_sdmmc.h"
#include "sdmmc_port.h"
#include "sdmmc_sd.h"

/* One card per image, matching the port's shape (one installed card, one
 * host).  The descriptor is large - the CID/CSD/SCR copies and the lock the
 * stack keeps in it - so it is static rather than stack-resident. */
static sdmmc_sd_t sdmmc_sd_card_dev;
static int sdmmc_sd_card_status = kStatus_Fail;
static bool sdmmc_sd_card_up;

status_t SD_CfgInitialize(sdmmc_sd_t *sdmmc, sdmmchost_config_t *config)
{
	assert(sdmmc != NULL);
	assert(config != NULL);

	sdmmchost_t *host = &sdmmc->host;
	sd_card_t *card = &sdmmc->card;
	status_t status;

	/* link the structures the imported stack walks (the card reaches its
	 * host, the host reaches its card, and the card reaches its detect
	 * record) */
	host->config = *config;
	host->cd = &sdmmc->cardDetect;
	host->card = card;
	card->host = host;
	card->usrParam.cd = &sdmmc->cardDetect;

	/*
	 * The staging buffer.  The imported extended-transfer paths copy through
	 * it whenever the caller's buffer does not suit the controller, so it must
	 * exist before the first card command.  The alignment is a DMA page
	 * rather than the stack's default block size for the same reason the
	 * SDIO instance asks for one: a block-sized alignment satisfies a
	 * one-block copy and not a multi-block one.
	 */
	card->internalBuffer = SDMMC_OSAMemoryAlignedAllocate(
		(uint32_t)host->config.maxTransSize, SDMMC_DATA_BUFFER_ALIGN_CACHE);
	if (card->internalBuffer == NULL) {
		return kStatus_OutOfRange;
	}

	(void)memset(card->internalBuffer, 0, host->config.maxTransSize);
	card->internalBufferSize = host->config.maxTransSize;

	host->capability = 0U;

	/* This port drives one controller.  An unexpected host type is refused
	 * (and the buffer released) rather than silently left without a backend. */
	if (host->config.hostType != (uint32_t)SDMMC_PORT_SDIO_HOST_TYPE) {
		SDMMC_OSAMemoryFree(card->internalBuffer);
		card->internalBuffer = NULL;
		card->internalBufferSize = 0U;
		return kStatus_SDMMC_HostNotSupport;
	}

	status = FDWMMCHOST_SDConfig(sdmmc, config);
	if (status == kStatus_Success) {
		/* host bring-up, card detect, then CMD0/CMD8/ACMD41, CID, RCA,
		 * CSD, SCR, the 4-bit switch, the block size and the timing
		 * switch */
		status = SD_Init(card);
	}

	return status;
}

int sdmmc_sd_card_start(void)
{
	sdmmchost_config_t config;

	if (sdmmc_sd_card_status == kStatus_Success) {
		return 0;
	}

	(void)memset(&sdmmc_sd_card_dev, 0, sizeof(sdmmc_sd_card_dev));

	/*
	 * The instance's configuration.  Two values carry a decision:
	 *
	 *  - enableIrq stays false: the F4 host glue is polled PIO, and a
	 *    transfer therefore runs on the polling path (the SDIO engine's
	 *    card-interrupt seam is a separate path this image does not use).
	 *  - maxTransSize is the staging buffer's size, and the imported stack
	 *    also derives the host's maximum block count from it
	 *    (maxTransSize / defBlockSize), so the two stay consistent: 16 KiB
	 *    is 32 blocks of 512 bytes, enough for one receive read of a WiFi
	 *    card and plenty for this line's single-block gate.
	 */
	config.hostId = 0U;
	config.hostType = (uint32_t)SDMMC_PORT_SDIO_HOST_TYPE;
	config.cardType = (uint32_t)kSDMMCHOST_CARD_TYPE_MICRO_SD;
	config.enableIrq = false;
	config.enableDMA = false;
	config.prefixedTiming = false;
	config.endianMode = (uint32_t)kSDMMCHOST_EndianModeLittle;
	config.maxTransSize = 16384U;
	config.defBlockSize = (size_t)FSL_SDMMC_DEFAULT_BLOCK_SIZE;
	config.cardClock = SDMMC_PORT_CARD_CLOCK_HZ;
	config.isUHSCard = false;
	config.sdioCardInt = NULL;
	config.sdioCardIntArg = NULL;

	sdmmc_sd_card_status = SD_CfgInitialize(&sdmmc_sd_card_dev, &config);
	if (sdmmc_sd_card_status != kStatus_Success) {
		/* A failed bring-up can leave the staging buffer allocated; release
		 * it here rather than leave the heap short for the next attempt.  The
		 * card descriptor is not torn down through SD_Deinit, whose first step
		 * takes a lock a failed bring-up may never have created. */
		if (sdmmc_sd_card_dev.card.internalBuffer != NULL) {
			SDMMC_OSAMemoryFree(sdmmc_sd_card_dev.card.internalBuffer);
			sdmmc_sd_card_dev.card.internalBuffer = NULL;
			sdmmc_sd_card_dev.card.internalBufferSize = 0U;
		}
		return -1;
	}

	sdmmc_sd_card_up = true;

	return 0;
}

void sdmmc_sd_card_stop(void)
{
	if (!sdmmc_sd_card_up) {
		return;
	}

	/* card (reset, deselect, power off, lock released) then host, then the
	 * staging buffer - the order the imported stack's own deinit uses */
	SD_Deinit(&sdmmc_sd_card_dev.card);
	sdmmc_sd_card_up = false;
	sdmmc_sd_card_status = kStatus_Fail;
}

int sdmmc_sd_info_get(struct sdmmc_sd_info *out)
{
	sd_card_t *card;

	if ((out == NULL) || !sdmmc_sd_card_up) {
		return -1;
	}

	card = &sdmmc_sd_card_dev.card;

	(void)memset(out, 0, sizeof(*out));
	out->blocks = card->blockCount;
	out->block_size = card->blockSize;
	out->clock_hz = card->busClock_Hz;
	out->width = (card->host != NULL) ? card->host->currBusWidth : 0U;
	out->timing = (uint32_t)card->currentTiming;
	out->version = card->version;
	out->flags = card->flags;

	return 0;
}

int sdmmc_sd_id_get(struct sdmmc_sd_id *out)
{
	sd_card_t *card;

	if ((out == NULL) || !sdmmc_sd_card_up) {
		return -1;
	}

	card = &sdmmc_sd_card_dev.card;

	(void)memset(out, 0, sizeof(*out));

	out->manufacturer = card->cid.manufacturerID;
	out->oem = card->cid.applicationID;
	(void)memcpy(out->product, card->cid.productName,
		     sizeof(card->cid.productName));
	out->product[sizeof(card->cid.productName)] = '\0';
	out->revision = card->cid.productVersion;
	out->serial = card->cid.productSerialNumber;
	out->date = card->cid.manufacturerData;

	out->csd_structure = card->csd.csdStructure;
	out->csd_size = card->csd.deviceSize;
	out->read_block_len = card->csd.readBlockLength;

	out->scr_spec = card->scr.sdSpecification;
	out->scr_bus_widths = card->scr.sdBusWidths;

	return 0;
}

int sdmmc_sd_read(uint64_t lba, void *buf, uint32_t blocks)
{
	sd_card_t *card = &sdmmc_sd_card_dev.card;

	if (!sdmmc_sd_card_up || (buf == NULL) || (blocks == 0U)) {
		return -1;
	}
	if ((lba > 0xFFFFFFFFULL) ||
	    ((lba + blocks) > (uint64_t)card->blockCount)) {
		return -1;
	}

	return (SD_ReadBlocks(card, (uint8_t *)buf, (uint32_t)lba, blocks) ==
		kStatus_Success) ? 0 : -1;
}

int sdmmc_sd_write(uint64_t lba, const void *buf, uint32_t blocks)
{
	sd_card_t *card = &sdmmc_sd_card_dev.card;

	if (!sdmmc_sd_card_up || (buf == NULL) || (blocks == 0U)) {
		return -1;
	}
	if ((lba > 0xFFFFFFFFULL) ||
	    ((lba + blocks) > (uint64_t)card->blockCount)) {
		return -1;
	}

	return (SD_WriteBlocks(card, (const uint8_t *)buf, (uint32_t)lba,
			       blocks) == kStatus_Success) ? 0 : -1;
}
