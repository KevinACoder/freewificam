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

	/* The card interrupt (DAT1). registered means the GIC handler is
	 * installed; armed means the controller's mask bit is set and the GIC
	 * line is enabled - the pair the runtime toggle moves. The callback
	 * runs in interrupt context and may only wake a thread. The counters
	 * that make the mechanism self-witnessing live in the census. */
	bool		card_irq_registered;
	bool		card_irq_armed;
	void		(*card_irq_cb)(void *);
	void		*card_irq_arg;
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

/* The completion-poll step, in microseconds: how long each completion wait
 * sleeps between readings of the controller's status register. A coarser step
 * notices a completed transfer up to one step late; a finer one spends
 * proportionally more CPU inside the wait loop. The absolute timeout budgets do
 * not change with it (they are wall-clock budgets, not iteration counts). The
 * default is the value this driver used to hard-code. */
void dwc_mmc_set_poll_us(uint32_t us);
uint32_t dwc_mmc_get_poll_us(void);

/* The step the data-stage waits use, in microseconds: the same knob as above
 * but for the two waits that cover a data stage (DTO, and the IDMAC drain that
 * trails it) instead of the command completions. Separate because the two waits
 * answer different questions: a command's completion is quantised by the step
 * on the critical path of every register access, while a data stage pays it
 * once per transfer - so the data step can be made fine without multiplying the
 * CPU spent in the many small command waits. The sibling controller port in
 * ports/fsl_sdmmc (the eMMC line) runs with its data wait an order of magnitude
 * finer than its command wait; this driver ran both at one step until that
 * comparison was measured. Default: the command step's default. */
void dwc_mmc_set_data_poll_us(uint32_t us);
uint32_t dwc_mmc_get_data_poll_us(void);

/* Run one command, with its data stage when the command carries one, and
 * return when it completed (DWC_MMC_OK) or why it did not. */
int dwc_mmc_transfer(struct dwc_mmc *hc, struct dwc_mmc_cmd *cmd);

/* Arm the card-interrupt path: install the GIC handler for this controller's
 * INTID and remember the callback (interrupt context - wake a thread and
 * nothing else). No interrupt reaches the GIC until dwc_mmc_card_irq_enable
 * sets the controller's mask bit; the pair is the runtime toggle, and the
 * default state (armed handler, disabled line) is bit-for-bit the behaviour
 * this driver had before the interrupt path existed. */
int dwc_mmc_card_irq_arm(struct dwc_mmc *hc, void (*cb)(void *), void *arg);

/* Set/clear the controller's card-interrupt mask bit and follow the GIC line
 * with it. Enable after servicing the card (this is also the pump's re-arm
 * after a cycle); disable when the card interrupt is not wanted. The ISR
 * clears the bit first thing when the card raises the line, so a level-held
 * DAT1 cannot storm the GIC while the consumer works. */
void dwc_mmc_card_irq_enable(struct dwc_mmc *hc, bool enable);

#ifdef CONFIG_LKPI_CENSUS
/* The controller's census: transfers and bytes by direction, plus the polled
 * waits' iteration counts and wall time - the number that says how much of a
 * transfer's cost sat inside this controller's own wait loops. Plain counters;
 * the suite's snapshot command reads them and prints. */
struct dwc_mmc_census {
	uint32_t	transfers;	/* every dwc_mmc_transfer call */
	uint32_t	with_data;	/* transfers that carried a data stage */
	uint32_t	errors;		/* non-OK outcomes */
	uint64_t	rx_bytes;
	uint64_t	tx_bytes;
	uint32_t	cmd_polls;	/* poll iterations of the CDONE wait */
	uint64_t	cmd_wait_ns;
	uint64_t	cmd_wait_max_ns;
	uint32_t	data_polls;	/* poll iterations of the DTO wait */
	uint64_t	data_wait_ns;
	uint64_t	data_wait_max_ns;
	uint32_t	idmac_polls;	/* poll iterations of the IDMAC drain wait */
	uint64_t	idmac_wait_ns;
	uint64_t	idmac_wait_max_ns;
	uint32_t	card_irq_entries;	/* ISR entries that carried the card interrupt */
	uint32_t	card_irq_spurious;	/* ISR entries that carried nothing this driver owns */
	/* the transfer in flight or the last one that returned: the command,
	 * its byte count and the controller's status at the end. `pending` is 1
	 * while a transfer has started and not returned. */
	uint32_t	last_cmd;
	uint32_t	last_len;
	uint32_t	last_error;
	uint32_t	last_status;
	uint32_t	pending;
};

void dwc_mmc_census_get(struct dwc_mmc_census *out);
void dwc_mmc_census_reset(void);
#endif	/* CONFIG_LKPI_CENSUS */

#endif /* FREEWIFICAM_DWC_MMC_H */
