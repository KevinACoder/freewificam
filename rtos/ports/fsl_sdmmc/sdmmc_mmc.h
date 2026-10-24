/*
 * @file   sdmmc_mmc.h
 * @brief  The eMMC card service's face: what the layers above this port use.
 *
 * This header is deliberately free of the imported stack's headers. The
 * block-device registry (ports/fatfs) reaches a storage driver through a frozen
 * interface and nothing else, so a driver that is adapted there must not drag
 * its own SDK along; this is that frozen interface for the eMMC line, in the
 * same spirit as the NVMe driver's own interface header.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 */

#ifndef FREEWIFICAM_SDMMC_MMC_H
#define FREEWIFICAM_SDMMC_MMC_H

#include <stdbool.h>
#include <stdint.h>

/* The card's logical block size: eMMC's user area is addressed in 512-byte
 * blocks, and this port keeps that unit all the way to the block-device seam. */
#define SDMMC_MMC_SECTOR_SIZE		512U

/* Bring the soldered eMMC up: the controller, then the imported stack's card
 * initialization ladder (identification, extended CSD, bus width, high-speed
 * timing). Idempotent: a second call reports the first call's result. Returns 0
 * on success and a negative value otherwise. */
int sdmmc_mmc_card_start(void);

/* Tear the card and the controller down. */
void sdmmc_mmc_card_stop(void);

/* The card's identity and the mode it ended up in - the facts the board
 * evidence lines report. All fields are the raw values the stack and the
 * controller hold; nothing here is derived. */
struct sdmmc_mmc_info {
	uint32_t	blocks;		/* 512-byte blocks in the user area */
	uint32_t	clock_hz;	/* the bus clock the card runs at */
	uint32_t	clock_asked_hz;	/* the rate the stack asked for */
	uint32_t	timing;		/* the stack's bus-timing value */
	uint32_t	width;		/* the width the stack recorded for the card */
	uint32_t	host_lines;	/* the data lines the controller was programmed with */
	uint32_t	cclk_sel;	/* the CRU CCLK_EMMC selector in effect */
	uint32_t	serial;		/* the CID's product serial number */
	uint8_t		manufacturer;	/* the CID's manufacturer ID */
	char		product[8];	/* the CID's product name, NUL-terminated */
	bool		cmd23;		/* block-count commands enabled */
};

/* Fill `out` from the live card. Returns 0, or a negative value when the card
 * is not up. */
int sdmmc_mmc_info_get(struct sdmmc_mmc_info *out);

/* The controller state behind the card, for the evidence lines that state what
 * the clock chain did rather than what the stack asked for. */
struct sdmmc_mmc_host_state {
	uint32_t	clock_asked_hz;	/* the rate the stack last asked for */
	uint32_t	clock_hz;	/* the rate the clock table resolved to */
	uint32_t	cclk_sel;	/* the CRU CCLK_EMMC selector in effect */
	unsigned int	width;		/* data lines last programmed */
	bool		present_bit;	/* the controller's own presence bit */
};

/* Fill `out` from the live controller. Returns 0, or a negative value when the
 * controller is not up. */
int sdmmc_mmc_host_state_get(struct sdmmc_mmc_host_state *out);

/* Read/write `blocks` 512-byte blocks starting at `lba`. The caller owns the
 * buffer. Returns 0, or a negative value when the transfer failed. */
int sdmmc_mmc_read(uint64_t lba, void *buf, uint32_t blocks);
int sdmmc_mmc_write(uint64_t lba, const void *buf, uint32_t blocks);

/* Push the card's own cache to its non-volatile store. */
int sdmmc_mmc_flush(void);

/* Ask the card for its status word (SEND_STATUS). Returns 0 with the raw R1
 * response in *r1, or a negative value when the card did not answer. The
 * current-state field is bits [12:9] of the word: 4 is the transfer state, and
 * anything else after a completed transfer means the card was left mid-transfer
 * - the condition a failed data stage leaves behind. */
int sdmmc_mmc_status_get(uint32_t *r1);

/* The card's identification registers as the imported stack parsed them: the
 * CID and CSD fields, and the extended-CSD bytes the evidence line compares
 * against the Linux side's `cid`/`csd`/`ext_csd` readback. The raw 128/512-byte
 * register images are not kept by the stack (it parses them at initialization),
 * so this is the decoded form of the same bytes - the fields mmc-utils prints.
 * Returns 0, or a negative value when the card is not up. */
struct sdmmc_mmc_id {
	/* CID */
	uint8_t		manufacturer;	/* CID[127:120] */
	uint16_t	oem;		/* CID[119:104] */
	char		product[7];	/* CID[103:56], NUL-terminated */
	uint8_t		revision;	/* CID[55:48] */
	uint32_t	serial;		/* CID[47:16] */
	uint8_t		date;		/* CID[15:8] */
	/* CSD */
	uint8_t		csd_structure;	/* CSD[127:126] */
	uint8_t		spec_version;	/* CSD[125:122] */
	uint32_t	device_size;	/* CSD[73:62] */
	uint8_t		read_block_len;	/* CSD[83:80] */
	/* EXT_CSD (byte offsets in the 512-byte register) */
	uint32_t	sectors;	/* [215:212] */
	uint8_t		ext_csd_rev;	/* [192] */
	uint8_t		ext_csd_structure; /* [194] */
	uint8_t		card_type;	/* [196] */
	uint8_t		bus_width;	/* [183] */
	uint8_t		hs_timing;	/* [185] */
};

int sdmmc_mmc_id_get(struct sdmmc_mmc_id *out);

#endif /* FREEWIFICAM_SDMMC_MMC_H */
