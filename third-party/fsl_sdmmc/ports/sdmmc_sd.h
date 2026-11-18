/*
 * @file   sdmmc_sd.h
 * @brief  The SD card service's face: what the layers above this port use.
 *
 * This header is deliberately free of the imported stack's headers.  The board
 * case reaches the card through this frozen interface and nothing else, so a
 * change inside the imported fsl_sdmmc SD stack does not travel into the case.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 */

#ifndef FREEWIFICAM_SDMMC_SD_H
#define FREEWIFICAM_SDMMC_SD_H

#include <stdbool.h>
#include <stdint.h>

/* The card's logical block size: SD memory cards are addressed in 512-byte
 * blocks, and this port keeps that unit all the way to the case. */
#define SDMMC_SD_SECTOR_SIZE		512U

/* Bring the one SD card up: the controller, then the imported stack's card
 * initialization ladder (CMD0/CMD8/ACMD41, CID, RCA, CSD, SCR, the 4-bit
 * switch, the block size and the timing switch).  Idempotent: a second call
 * reports the first call's result.  Returns 0 on success, negative otherwise. */
int sdmmc_sd_card_start(void);

/* Tear the card and the controller down. */
void sdmmc_sd_card_stop(void);

/* The card's geometry and the mode it ended up in - the facts the board
 * evidence lines report.  All fields are raw values the stack and the
 * controller hold; nothing here is derived. */
struct sdmmc_sd_info {
	uint32_t	blocks;		/* 512-byte blocks in the card */
	uint32_t	block_size;	/* the stack's block size (512) */
	uint32_t	clock_hz;	/* the bus clock the card runs at */
	uint32_t	width;		/* the data lines the controller was programmed with */
	uint32_t	timing;		/* the stack's bus-timing value */
	uint32_t	version;	/* the SD specification version from the SCR */
	uint32_t	flags;		/* the stack's kSD_* card flags */
};

/* Fill `out` from the live card.  Returns 0, or negative when the card is not
 * up. */
int sdmmc_sd_info_get(struct sdmmc_sd_info *out);

/* The card's identification registers as the imported stack parsed them: the
 * CID, CSD and SCR fields the evidence lines compare against the card's label
 * and the Wildfire reference's printout.  The raw register images are not kept
 * by the stack (it decodes them during initialization), so this is the decoded
 * form of the same bytes. */
struct sdmmc_sd_id {
	/* CID */
	uint8_t		manufacturer;	/* CID[127:120] */
	uint16_t	oem;		/* CID[119:104] */
	char		product[8];	/* CID[103:64], NUL-terminated */
	uint8_t		revision;	/* CID[63:56] */
	uint32_t	serial;		/* CID[55:24] */
	uint16_t	date;		/* CID[19:8] */
	/* CSD */
	uint8_t		csd_structure;	/* CSD[127:126] */
	uint32_t	csd_size;	/* CSD[73:62] */
	uint8_t		read_block_len;	/* CSD[83:80] */
	/* SCR */
	uint8_t		scr_spec;	/* SCR[59:56] */
	uint8_t		scr_bus_widths;	/* SCR[51:48] */
};

/* Fill `out` from the live card.  Returns 0, or negative when the card is not
 * up. */
int sdmmc_sd_id_get(struct sdmmc_sd_id *out);

/* Read/write `blocks` 512-byte blocks starting at `lba`.  The caller owns the
 * buffer; an aligned buffer avoids the stack's staging copy.  Returns 0, or a
 * negative value when the transfer failed or the range is outside the card. */
int sdmmc_sd_read(uint64_t lba, void *buf, uint32_t blocks);
int sdmmc_sd_write(uint64_t lba, const void *buf, uint32_t blocks);

#endif /* FREEWIFICAM_SDMMC_SD_H */
