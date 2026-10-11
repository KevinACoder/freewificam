/*
 * @file   dwc_mmc.h
 * @brief  DesignWare Mobile Storage Host driver for this board's sdmmc0.
 *
 * One controller, one card, polling transfers. The card clock comes from the
 * CRU (gated, released and muxed by this driver) and the data stage runs
 * through the controller's internal DMA engine (IDMAC), which is what makes the
 * four-byte length discipline at the layer above necessary: the engine takes
 * buffer addresses and lengths in words.
 *
 * The 1.8 V signalling path (CMD11 voltage switch, UHS-I modes, tuning) is not
 * carried: this board's card slot is wired for 3.3 V only, and the SDIO stack
 * above never reaches for it while the host advertises no 1.8 V capability.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 */

#ifndef FREEWIFICAM_DWC_MMC_H
#define FREEWIFICAM_DWC_MMC_H

#include <stdbool.h>
#include <stdint.h>

/* return codes (0 is success; every failure is negative) */
#define DWC_MMC_OK			0
#define DWC_MMC_ERR_ARG			(-1)
#define DWC_MMC_ERR_TIMEOUT		(-2)
#define DWC_MMC_ERR_CMD			(-3)
#define DWC_MMC_ERR_DATA		(-4)
#define DWC_MMC_ERR_NO_CARD		(-5)
#define DWC_MMC_ERR_NOT_READY		(-6)

/* the controller's coordinates on this SoC */
struct dwc_mmc_cfg {
	uintptr_t	base;	/* register base */
	unsigned int	index;	/* 0 = sdmmc0, 1 = sdmmc1: which CRU bits apply */
	int		intid;	/* GIC INTID; recorded for the interrupt round, unused
				 * while this port polls */
};

/* The controllers this board wires up. sdmmc0 is the socket the SDIO WiFi part
 * sits in; its interrupt number is the SoC's (GIC SPI 98) and is carried even
 * though this port polls, so the interrupt round does not have to re-derive
 * it. */
#define DWC_MMC0_BASE		0xfe2b0000UL
#define DWC_MMC0_INTID		130

/* The board's controller table, indexed by instance. Returns NULL for an
 * index this board does not wire. */
const struct dwc_mmc_cfg *dwc_mmc_board_cfg(unsigned int index);

/* the data stage of one command */
struct dwc_mmc_data {
	uint8_t		*buf;		/* must be word-aligned */
	uint32_t	len;		/* total bytes; must be a multiple of 4 */
	uint32_t	blksz;
	uint32_t	blkcnt;
	bool		write;		/* true: memory to card */
};

/* one command, with or without a data stage */
struct dwc_mmc_cmd {
	uint32_t	index;
	uint32_t	arg;
	bool		resp;		/* a response is expected */
	bool		resp_long;	/* 136-bit response */
	bool		resp_crc;	/* check the response CRC */
	bool		send_init;	/* initialization command (CMD0) */
	uint32_t	response[4];	/* filled in when resp is set */
	const struct dwc_mmc_data *data;	/* NULL: no data stage */
};

struct dwc_mmc {
	struct dwc_mmc_cfg cfg;
	uint32_t	src_hz;		/* CRU source the card clock divides */
	uint32_t	clock_hz;	/* the card clock last asked for */
	uint32_t	last_cmd;	/* the command word last written, for a dump */
	unsigned int	width;		/* data lines last programmed */
	bool		ready;
};

/* Bring the controller up: CRU gate/reset/source, soft reset, FIFO threshold,
 * card power, the 400 kHz identification clock. Fails with
 * DWC_MMC_ERR_NO_CARD when the slot reads empty. */
int dwc_mmc_init(struct dwc_mmc *hc, const struct dwc_mmc_cfg *cfg);

/* Release the controller: drop the card clock and power and forget the
 * instance. */
void dwc_mmc_deinit(struct dwc_mmc *hc);

/* Controller soft reset plus the register re-programming that follows it
 * (used to recover from an error state). */
int dwc_mmc_reset(struct dwc_mmc *hc);

/* Set the card clock, choosing a CRU source and divider. Returns the rate the
 * card actually runs at in Hz, or 0 when the source could not be applied. */
uint32_t dwc_mmc_set_clock(struct dwc_mmc *hc, uint32_t hz);

/* Set the bus width in signal lines: 1, 4 or 8. The caller maps its own width
 * vocabulary onto these three values. */
void dwc_mmc_set_bus_width(struct dwc_mmc *hc, unsigned int width);

/* Whether the slot reads as occupied. */
bool dwc_mmc_card_present(const struct dwc_mmc *hc);

/*
 * Bring-up bisection switch, and only that: it makes the data path follow the
 * board-proven reference implementation exactly, by dropping the two steps this
 * driver adds over it (the IDMAC software reset before arming, and the poll
 * demand after) and by stopping the glue layer from checking the R5 response
 * CRC. It exists to separate "one of our own additions broke the data phase"
 * from "the data phase never starts for a reason that predates them", and goes
 * away once the data path is settled.
 */
void dwc_mmc_ref_data_path_set(bool on);
bool dwc_mmc_ref_data_path_get(void);

/* Run one command, with its data stage when the command carries one, and
 * return when it completed (DWC_MMC_OK) or why it did not. */
int dwc_mmc_transfer(struct dwc_mmc *hc, struct dwc_mmc_cmd *cmd);

#endif /* FREEWIFICAM_DWC_MMC_H */
