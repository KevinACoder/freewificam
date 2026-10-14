/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdmmc_mmc_card - the platform dispatch and the one eMMC instance of this
 * port.
 *
 * Two faces in one file, and the split is the same one the SDIO half of the
 * port uses:
 *
 *   - MMC_CfgInitialize is the dispatch the imported stack expects: the
 *     upstream tree keeps it in its own host/ directory, walking the SDK's
 *     whole controller list and linking each card structure to its host before
 *     allocating the staging buffer every alignment fallback inside the stack
 *     copies through. This port has exactly one controller, so the walk
 *     collapses to a refusal of anything else - but the part that matters is
 *     kept, because nothing else in the tree allocates that buffer.
 *
 *   - sdmmc_mmc_card_start and the block calls beside it are this port's own
 *     face (sdmmc_mmc.h), the one the block-device registry is adapted to. The
 *     configuration lives here rather than in the registry because every value
 *     in it is a statement about this board's socket and this stack's
 *     expectations, and the registry knows neither.
 *
 * @design adapter - ref upstream:third-party/fsl_sdmmc host/fsl_sdmmc.c
 *         (MMC_CfgInitialize, the structure linking and the staging-buffer
 *         allocation) with the controller walk narrowed to this board's one
 *         host type, and the buffer's alignment raised to a DMA page, as the
 *         SDIO instance of this port does.
 */

#include "fsl_sdmmc.h"
#include "sdmmc_port.h"
#include "sdmmc_mmc.h"

/* One card per image, matching the block-device seam's shape (one installed
 * backend, one card). The descriptor is large - the CID/CSD/EXT_CSD copies and
 * the lock the stack keeps in it - so it is static rather than stack-resident. */
static sdmmc_mmc_t sdmmc_mmc_card_dev;
static int sdmmc_mmc_card_status = kStatus_Fail;
static bool sdmmc_mmc_card_up;

status_t MMC_CfgInitialize(sdmmc_mmc_t *sdmmc, sdmmchost_config_t *config)
{
	assert(sdmmc != NULL);
	assert(config != NULL);

	sdmmchost_t *host = &sdmmc->host;
	mmc_card_t *card = &sdmmc->card;
	status_t status = kStatus_Fail;

	/* link the structures the imported stack walks (the card reaches its
	 * host and the host reaches its card) */
	host->config = *config;
	card->host = host;
	host->card = card;

	/*
	 * The staging buffer. The imported extended-transfer paths copy through it
	 * whenever the caller's buffer does not suit the controller, so it must
	 * exist before the first card command. The alignment is a DMA page rather
	 * than the stack's default block size for the same reason the SDIO
	 * instance asks for one: this board's engines take page-granular buffer
	 * addresses, and a block-sized alignment satisfies a one-block copy and
	 * not a multi-block one.
	 */
	card->internalBuffer = SDMMC_OSAMemoryAlignedAllocate((uint32_t)host->config.maxTransSize,
							      SDMMC_DATA_BUFFER_ALIGN_CACHE);
	if (card->internalBuffer == NULL) {
		return kStatus_OutOfRange;
	}

	(void)memset(card->internalBuffer, 0, host->config.maxTransSize);
	card->internalBufferSize = host->config.maxTransSize;

	host->capability = 0U;

	/* This port drives one controller. An unexpected host type is refused (and
	 * the buffer released) rather than silently left without a backend. */
	if (host->config.hostType != (uint32_t)kSDMMCHOST_TYPE_FDWMSHC) {
		SDMMC_OSAMemoryFree(card->internalBuffer);
		card->internalBuffer = NULL;
		card->internalBufferSize = 0U;
		return kStatus_SDMMC_HostNotSupport;
	}

	status = FDWMSHCHOST_MMCConfig(sdmmc, config);
	if (status == kStatus_Success) {
		/* host bring-up, then the card's ladder: CMD0/CMD1/CMD2/CMD3/CMD9,
		 * CMD7, extended CSD, block size, the bus-width switch and the
		 * high-speed timing switch */
		status = MMC_Init(card);
	}

	return status;
}

int sdmmc_mmc_card_start(void)
{
	sdmmchost_config_t config;

	if (sdmmc_mmc_card_status == kStatus_Success) {
		return 0;
	}

	(void)memset(&sdmmc_mmc_card_dev, 0, sizeof(sdmmc_mmc_card_dev));

	/*
	 * The instance's configuration. Three values carry a decision:
	 *
	 *  - enableIrq stays false: the design brings this card up by polling,
	 *    and a transfer therefore runs on the polling path of the host glue.
	 *  - maxTransSize is the staging buffer's size, and the imported stack
	 *    also derives the host's maximum block count from it
	 *    (maxTransSize / defBlockSize), so the two stay consistent: the
	 *    largest block transfer the stack will assemble is exactly what one
	 *    staging copy can hold. 64 KiB is 128 blocks of 512 bytes - enough
	 *    that a sequential transfer's command overhead is a rounding error
	 *    while the staging copy stays a small part of the image's heap.
	 *  - cardClock is this socket's ceiling and, through the capability
	 *    declaration, what keeps the stack's ladder at high speed: the CRU
	 *    source this port selects for it is the 50 MHz slot.
	 */
	config.hostId = 0U;
	config.hostType = (uint32_t)kSDMMCHOST_TYPE_FDWMSHC;
	config.cardType = (uint32_t)kSDMMCHOST_CARD_TYPE_EMMC;
	config.enableIrq = false;
	config.enableDMA = true;
	config.prefixedTiming = false;
	config.endianMode = (uint32_t)kSDMMCHOST_EndianModeLittle;
	config.maxTransSize = 65536U;
	config.defBlockSize = (size_t)FSL_SDMMC_DEFAULT_BLOCK_SIZE;
	config.cardClock = SDMMC_PORT_MMC_CARD_CLOCK_HZ;
	config.isUHSCard = false;
	config.sdioCardInt = NULL;
	config.sdioCardIntArg = NULL;

	sdmmc_mmc_card_status = MMC_CfgInitialize(&sdmmc_mmc_card_dev, &config);
	if (sdmmc_mmc_card_status != kStatus_Success) {
		/* A failed bring-up can leave the staging buffer allocated; release it
		 * here rather than leave the heap short for the next attempt. The card
		 * descriptor is not torn down through MMC_Deinit, whose first step
		 * takes a lock a failed bring-up may never have created. */
		if (sdmmc_mmc_card_dev.card.internalBuffer != NULL) {
			SDMMC_OSAMemoryFree(sdmmc_mmc_card_dev.card.internalBuffer);
			sdmmc_mmc_card_dev.card.internalBuffer = NULL;
			sdmmc_mmc_card_dev.card.internalBufferSize = 0U;
		}
		return -1;
	}

	sdmmc_mmc_card_up = true;

	return 0;
}

void sdmmc_mmc_card_stop(void)
{
	if (!sdmmc_mmc_card_up) {
		return;
	}

	/* card (reset, deselect, power off, lock released) then host, then the
	 * staging buffer - the order the imported stack's own deinit uses */
	MMC_Deinit(&sdmmc_mmc_card_dev.card);
	sdmmc_mmc_card_up = false;
	sdmmc_mmc_card_status = kStatus_Fail;
}

int sdmmc_mmc_info_get(struct sdmmc_mmc_info *out)
{
	mmc_card_t *card;
	struct sdmmc_mmc_host_state hc_state;

	if ((out == NULL) || !sdmmc_mmc_card_up) {
		return -1;
	}

	card = &sdmmc_mmc_card_dev.card;

	(void)memset(out, 0, sizeof(*out));
	out->blocks = card->userPartitionBlocks;
	out->clock_hz = card->busClock_Hz;
	out->timing = (uint32_t)card->busTiming;
	out->width = (uint32_t)card->busWidth;
	out->cmd23 = card->enablePreDefinedBlockCount;
	out->manufacturer = card->cid.manufacturerID;
	out->serial = card->cid.productSerialNumber;
	(void)memcpy(out->product, card->cid.productName, sizeof(card->cid.productName));
	out->product[sizeof(card->cid.productName)] = '\0';

	if (sdmmc_mmc_host_state_get(&hc_state) == 0) {
		out->clock_asked_hz = hc_state.clock_asked_hz;
		out->clock_hz = hc_state.clock_hz;
		out->cclk_sel = hc_state.cclk_sel;
		out->host_lines = hc_state.width;
	}

	return 0;
}

int sdmmc_mmc_read(uint64_t lba, void *buf, uint32_t blocks)
{
	if (!sdmmc_mmc_card_up || (buf == NULL) || (blocks == 0U)) {
		return -1;
	}

	return (MMC_ReadBlocks(&sdmmc_mmc_card_dev.card, (uint8_t *)buf,
			       (uint32_t)lba, blocks) == kStatus_Success) ? 0 : -1;
}

int sdmmc_mmc_write(uint64_t lba, const void *buf, uint32_t blocks)
{
	if (!sdmmc_mmc_card_up || (buf == NULL) || (blocks == 0U)) {
		return -1;
	}

	return (MMC_WriteBlocks(&sdmmc_mmc_card_dev.card, (const uint8_t *)buf,
				(uint32_t)lba, blocks) == kStatus_Success) ? 0 : -1;
}

int sdmmc_mmc_flush(void)
{
	if (!sdmmc_mmc_card_up) {
		return -1;
	}

	return (MMC_FlushCache(&sdmmc_mmc_card_dev.card) == kStatus_Success) ? 0 : -1;
}

int sdmmc_mmc_status_get(uint32_t *r1)
{
	sdmmchost_transfer_t content = { 0 };
	sdmmchost_cmd_t command = { 0 };

	if ((r1 == NULL) || !sdmmc_mmc_card_up) {
		return -1;
	}

	/* The card layer's own status command, sent through the same host face
	 * every other command goes through (the stack keeps its status helper
	 * private, so the command is built here). */
	command.index = 13U;		/* SEND_STATUS */
	command.argument = sdmmc_mmc_card_dev.card.relativeAddress << 16U;
	command.responseType = kCARD_ResponseTypeR1;
	content.command = &command;

	if (SDMMCHOST_TransferFunction(&sdmmc_mmc_card_dev.host, &content) != kStatus_Success) {
		return -1;
	}

	*r1 = command.response[0];

	return 0;
}
