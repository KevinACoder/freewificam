/*
 * @file   dwc_mshc.h
 * @brief  The eMMC controller driver for this board's soldered card: bring-up,
 *         the identification and high-speed clock, SDMA commands and data.
 *
 * This controller is an SDHCI 4.20a core with the vendor area Synopsys adds for
 * high-speed eMMC work (the DLL, the auto-tuning register). This round drives it
 * in the modes the card is known to run on this board - identification at
 * 400 kHz, then high speed at the 50 MHz CRU slot - and reaches the data path
 * through the core's SDMA engine. The DLL lock path and the CMD21 tuning that
 * go with a 200 MHz eMMC bus are not carried: the high-speed timing switch the
 * card stack performs is the stack's own business, and the clock chain the DLL
 * belongs to is the next round's subject.
 *
 * One controller, one soldered card, polling transfers. The register sequences
 * follow u-boot's rockchip_sdhci.c and the kernel's sdhci-of-dwcmshc.c for this
 * same controller; the CRU/GRF write discipline (hiword write-enable, no bit
 * beyond the intended field) is this board's.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 */

#ifndef FREEWIFICAM_DWC_MSHC_H
#define FREEWIFICAM_DWC_MSHC_H

#include <stdbool.h>
#include <stdint.h>

/* Return codes: 0 is success, every failure is negative. */
#define DWC_MSHC_OK			0
#define DWC_MSHC_ERR_ARG		(-1)
#define DWC_MSHC_ERR_TIMEOUT		(-2)
#define DWC_MSHC_ERR_CMD		(-3)
#define DWC_MSHC_ERR_DATA		(-4)
#define DWC_MSHC_ERR_NO_CARD		(-5)
#define DWC_MSHC_ERR_NOT_READY		(-6)

/* The controller's coordinates on this SoC. intid is the card interrupt's
 * number in this project's CMSIS numbering (the SoC's GIC SPI 19); it is
 * recorded for the interrupt round this port does not run. */
struct dwc_mshc_cfg {
	uintptr_t	base;
	int		intid;
};

#define DWC_MSHC_EMMC_BASE		0xfe310000UL
#define DWC_MSHC_EMMC_INTID		51

const struct dwc_mshc_cfg *dwc_mshc_board_cfg(unsigned int index);

/* The data stage of one command. The buffer must be word-aligned with a length
 * that is a multiple of four: the engine takes word addresses and lengths, and
 * the layer above owns that rule. */
struct dwc_mshc_data {
	uint8_t		*buf;
	uint32_t	len;
	uint32_t	blksz;
	uint32_t	blkcnt;
	bool		write;		/* true: memory to card */
};

/* One command, with or without a data stage. resp_crc asks for the response
 * CRC to be checked, which R3 and R4 responses do not carry; the command index
 * check follows the CRC and is skipped for 136-bit responses, where the
 * specification has no index field. */
struct dwc_mshc_cmd {
	uint32_t	index;
	uint32_t	arg;
	bool		resp;
	bool		resp_long;
	bool		resp_crc;
	uint32_t	response[4];	/* filled in when resp is set */
	const struct dwc_mshc_data *data;
};

/* The instance. Fields after the configuration are the driver's own record:
 * what it last programmed, and what the case's evidence lines read back. */
struct dwc_mshc {
	struct dwc_mshc_cfg cfg;
	uint32_t	src_hz;		/* the CRU source the card clock divides */
	uint32_t	clock_hz;	/* the card clock last asked for */
	uint32_t	achieved_hz;	/* the rate the clock table resolved to */
	uint32_t	cclk_sel;	/* the CCLK mux selector last written */
	unsigned int	width;		/* data lines last programmed */
	uint32_t	clk_ctrl;	/* CLOCK_CTRL as last written */
	bool		ready;
};

/* The census the suite's evidence lines read: transfers and bytes by direction,
 * how many of them were block-count commands (CMD23), how many hit an SDMA
 * boundary, and how many failed. Plain counters, never reset by the driver. */
struct dwc_mshc_census {
	uint32_t	transfers;
	uint32_t	with_data;
	uint32_t	cmd23;
	uint32_t	errors;
	uint32_t	boundary_events;
	uint64_t	rx_bytes;
	uint64_t	tx_bytes;
};

/* Bring the controller up: CRU gates/reset/clock mux, the eMMC pin group, the
 * controller soft reset, the identification clock at 400 kHz. The card is
 * soldered, so bring-up does not gate on the controller's card-presence bit -
 * dwc_mshc_card_present() reads that bit out for the evidence lines instead. */
int dwc_mshc_init(struct dwc_mshc *hc, const struct dwc_mshc_cfg *cfg);

/* Release the controller: drop the card clock and forget the instance. */
void dwc_mshc_deinit(struct dwc_mshc *hc);

/* Controller soft reset plus the re-programming that follows it (used to
 * recover from an error state). */
int dwc_mshc_reset(struct dwc_mshc *hc);

/* Set the card clock. Returns the rate the card clock actually runs at in Hz,
 * or 0 when the requested rate cannot be produced with the sources this round
 * carries (anything above the 50 MHz slot). */
uint32_t dwc_mshc_set_clock(struct dwc_mshc *hc, uint32_t hz);

/* Set the bus width in signal lines: 1, 4 or 8. */
void dwc_mshc_set_bus_width(struct dwc_mshc *hc, unsigned int width);

/* Whether the controller reports a card on the socket. */
bool dwc_mshc_card_present(const struct dwc_mshc *hc);

/* Whether the card is holding DAT0 low, which is how an eMMC signals that it is
 * still programming. */
bool dwc_mshc_card_busy(const struct dwc_mshc *hc);

/* The command/data/interrupt registers' raw values, for an evidence line. */
void dwc_mshc_read_state(const struct dwc_mshc *hc, uint32_t *pstate,
			 uint32_t *normal_int, uint32_t *error_int);

/* Run one command, with its data stage when the command carries one. Returns
 * DWC_MSHC_OK when it completed, or the reason it did not. */
int dwc_mshc_transfer(struct dwc_mshc *hc, struct dwc_mshc_cmd *cmd);

void dwc_mshc_census_get(struct dwc_mshc_census *out);
void dwc_mshc_census_reset(void);

#endif /* FREEWIFICAM_DWC_MSHC_H */
