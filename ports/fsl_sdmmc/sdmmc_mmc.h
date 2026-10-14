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

#endif /* FREEWIFICAM_SDMMC_MMC_H */
