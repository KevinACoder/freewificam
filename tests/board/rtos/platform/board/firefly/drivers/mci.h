/*
 * @file   mci.h
 * @brief  Phytium MCI host controller driver for this board's SDIO socket
 *         (mmc1 @ 0x28001000), ported from the Linux driver that is proven on
 *         this exact board.
 *
 * Source of the sequence and the register map: linux-upstream's
 * drivers/mmc/host/firefly-mci.{c,h} at the project's GT pin (63a2a8ab9cd3),
 * the driver that brings mmc1 up under Linux on firefly v2 (rtw88_8821cs
 * enumerates, scans, associates and passes traffic there). The port keeps the
 * register programme of that driver - the clock-field encoding, the IDMAC
 * descriptor shape, the submission order of a data command, the command-word
 * encoding - and drops only what belongs to the Linux mmc core: the request
 * queue, block-layer scatter-gather, the completion interrupt and power
 * management.
 *
 * Why port it rather than keep the vendor stack's FSDIF driver: on this board
 * the vendor driver answers CMD52 and has never answered CMD53, while the
 * Linux driver on the same silicon, same card, same clock point does both. The
 * difference is not the card, so the driver is the variable worth removing.
 *
 * Polled by design: every completion is a read of RAW_INTS / DMAC_STATUS plus
 * a wall-clock budget. The interrupt line (GIC INTID 105 for mmc1) is recorded
 * but not enabled, because a single consumer of the status word is exactly
 * what the 2026-10-29 ko round proved sufficient.
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2021-2024 Phytium Technology Co., Ltd. (original driver)
 * Copyright (c) 2026 freewificam contributors (this port)
 */

#ifndef FREEWIFICAM_MCI_H
#define FREEWIFICAM_MCI_H

#include <stdbool.h>
#include <stdint.h>

/* return codes (0 is success; every failure is negative) */
#define MCI_OK			0
#define MCI_ERR_ARG		(-1)
#define MCI_ERR_TIMEOUT		(-2)	/* no completion inside the budget */
#define MCI_ERR_CMD		(-3)	/* response error / no response */
#define MCI_ERR_DATA		(-4)	/* data-phase error */
#define MCI_ERR_NO_CARD		(-5)
#define MCI_ERR_NOT_READY	(-6)
/* The controller stopped answering: a register read back all ones. A bus that
 * reads 0xffffffff is not reporting "every event happened", which is what the
 * driver's masks would otherwise conclude - it is reporting nothing at all,
 * because the peripheral is gone from the bus (clock stopped, controller held
 * in reset) and the interconnect returns the floating value. Every wait that
 * sees this must stop rather than interpret it. */
#define MCI_ERR_DEAD		(-7)

/*
 * The response class, in the vocabulary the command register encodes (the
 * port's own, not Linux's MMC_RSP_* bits): a response may be present, may
 * carry a CRC, and may be 136 bits long. The driver turns the class into the
 * RESP_EXP/RESP_CRC/RESP_LONG bits of the command word exactly where Linux
 * does, so a trace line can be diffed against the Linux one.
 */
#define MCI_RESP_NONE		0u	/* no response (CMD0, CMD52 write probe) */
#define MCI_RESP_SHORT_CRC	1u	/* R1/R5/R6: 48-bit response with CRC */
#define MCI_RESP_LONG_CRC	2u	/* R2: 136-bit response with CRC */
#define MCI_RESP_SHORT		3u	/* R3/R4: 48-bit response without CRC */

/* the data stage of one command */
struct mci_data {
	uint8_t		*buf;		/* the CPU side of the transfer */
	uint32_t	len;		/* total bytes: blksz * blkcnt */
	uint32_t	blksz;
	uint32_t	blkcnt;
	bool		write;		/* true: memory to card */
};

/* one command, with or without a data stage */
struct mci_cmd {
	uint32_t	index;
	uint32_t	arg;
	uint32_t	resp_class;	/* MCI_RESP_* */
	uint32_t	response[4];	/* filled in when a response is expected */
	const struct mci_data *data;	/* NULL: no data stage */
};

/* the controller's coordinates on this SoC (from the board's device tree) */
struct mci_cfg {
	uintptr_t	base;
	unsigned int	index;		/* 0 = mmc0 (TF slot), 1 = mmc1 (SDIO) */
	int		intid;		/* GIC INTID, recorded; the port polls */
};

/* The controllers the board wires. Both are the same IP; mmc1 carries the
 * SDIO part and mmc0 the TF slot, so the table is indexed by instance. */
const struct mci_cfg *mci_board_cfg(unsigned int index);

/*
 * The last transfer's evidence. A failed command's raw status is a
 * write-one-to-clear register that the driver clears on the way out, so a
 * reader that arrives afterwards sees zeros - the snapshot taken at the
 * failure instant is the only record of what the controller held then.
 */
struct mci_state {
	uint32_t	transfers;
	uint32_t	errors;
	uint32_t	cmd52;
	uint32_t	cmd53;
	uint32_t	data_stages;
	uint32_t	idmac_stages;
	uint32_t	fifo_stages;
	uint32_t	last_cmd;
	uint32_t	last_arg;
	int		last_err;
	uint32_t	rawints;	/* snapshot at the last completion/error */
	uint32_t	cmd_ev;		/* the command half's RAW_INTS */
	uint32_t	data_ev;	/* the data half's RAW_INTS */
	uint32_t	masked;
	uint32_t	status;
	uint32_t	cmd;
	uint32_t	resp0;
	uint32_t	dmac;
	uint32_t	blksz;
	uint32_t	bytcnt;
	uint32_t	int_mask;
	uint32_t	cntrl;
	uint32_t	clkdiv;
	uint32_t	uhs_ext;
	uint32_t	ctype;
	uint32_t	clk_divider;
	uint32_t	cur_ios_clk;	/* the clock the driver last programmed */
	uint32_t	bus_width;
	/* The entry gate: how many requests had to wait for the controller, and
	 * the largest wait seen. Zero on a clean boot, which is the point of
	 * counting instead of printing. */
	uint32_t	gate_waits;
	uint32_t	gate_last;
	uint32_t	gate_skipped;
};

/* Per-controller state. One controller, one card, transfers serialised by the
 * stack above, so a single instance is the whole requirement. */
struct mci_host {
	struct mci_cfg	cfg;
	uint32_t	cur_ios_clk;
	unsigned int	width;
	bool		need_init;	/* the next command carries SEND_INIT */
	bool		ready;
	struct mci_state st;
};

/* Board fact, kept next to the driver that divides it: this SoC feeds both MCI
 * instances from sysclk_1200mhz (the device tree's clock-names "mci_clk").
 * There is no clock driver to ask, so the number is a constant here. */
#define MCI_SRC_CLK_HZ		1200000000UL

/* Bring the controller up: FIFO threshold, card threshold control, the
 * external-clock field, power, the resets, the interrupt enables, the timeout
 * register - the sequence of firefly_mci_init_hw(). Does NOT set a card clock:
 * the stack asks for 400 kHz before its first command, exactly as the Linux
 * mmc core does on power-up. */
int mci_init(struct mci_host *hc, const struct mci_cfg *cfg);

/* Release the controller: drop card power and the card clock. */
void mci_deinit(struct mci_host *hc);

/* Controller reset plus the re-programming that follows it (the recovery step
 * used when a transfer failed). */
int mci_reset(struct mci_host *hc);
/* firefly_mci_hw_reset()'s card-facing half: pulse the controller's card-reset
 * line. The part on this board does not answer CMD5 until it has seen it. */
void mci_reset_card(struct mci_host *hc);

/* Set the card clock. Returns the rate now programmed, or 0 when the request
 * could not be applied. The encoding is the Linux driver's: the external
 * clock field selects the first divider, CLKDIV holds the second divider with
 * its sample/drive taps, and CLK_DIVIDER carries the doubled first divider. */
uint32_t mci_set_clock(struct mci_host *hc, uint32_t hz);

/* Set the data lines: 1, 4 or 8 (a count of lines, not an enumeration). */
void mci_set_bus_width(struct mci_host *hc, unsigned int width);

/* Card power. On, that raises PWREN and arms SEND_INIT on the next command -
 * the same two things Linux's MMC_POWER_UP does. */
void mci_power(struct mci_host *hc, bool on);

/* Whether the slot reads as occupied (mmc1 is non-removable: always true, but
 * the pin is read so a dead slot is visible in the evidence). */
bool mci_card_present(const struct mci_host *hc);

/* Run one command, with its data stage when the command carries one, and
 * return when it completed (MCI_OK) or why it did not. */
int mci_transfer(struct mci_host *hc, struct mci_cmd *cmd);

/* The per-command trace ("MCI CMD/DONE/CLK/IOS"), the same field shapes the
 * Linux lab patch prints, so the two traces diff field by field. On by
 * default: a boot's whole point is the command sequence. */
void mci_trace_set(int on);

/* Print the controller's register file 0x000..0x124 as one line per offset
 * ("MCI RD %03x %08x"), the shape the Linux register dump uses. */
void mci_trace_dump(struct mci_host *hc);

/* The evidence the case reports. */
void mci_state_get(const struct mci_host *hc, struct mci_state *out);

/* Poll step for the completion waits, microseconds (default: 10). */
void mci_set_poll_us(uint32_t us);

/*
 * Slow-motion probe. With a count armed, the next transfer that has a data
 * stage logs its first `samples` poll iterations verbatim - RAW_INTS, STATUS,
 * CMD, DMAC_STATUS - instead of only its final state. The final state alone
 * cannot tell "the card never drove the bus" from "the controller never
 * started the phase": both leave the same timeout behind. The time series can:
 * whether DATA_BUSY rises at all, whether CMD_START clears, and whether the
 * FIFO level moves are three different questions with three different answers
 * in that trace. Count 0 disarms.
 */
void mci_set_poll_probe(unsigned int samples);

/* Raw register access, for a probe that needs an offset the driver does not
 * model. Diagnostics only; nothing in the driver's own paths uses these. */
uint32_t mci_reg_read(const struct mci_host *hc, uint32_t off);
void mci_reg_write(struct mci_host *hc, uint32_t off, uint32_t val);

/*
 * Free bits set into CNTRL and FIFOTH for the transfers that follow, and the
 * wait bit that is normally left alone. This exists for one experiment: the
 * data phase of a CMD53 never starts on this board (STATUS reads DATA_BUSY=1,
 * FIFO empty, no DTO) while every command field matches the Linux GT that does
 * start it. The candidate causes are all in CNTRL's read/wait behaviour and in
 * the FIFO threshold, and a sweep over them in one boot beats one hypothesis
 * per boot. Set to 0 to restore the port's own programming.
 */
void mci_set_cntrl_extra(uint32_t set_bits, uint32_t extra_fifoth_bits);

/*
 * Preconditions for the same experiment, one flag each. CNTRL's own bits were
 * swept and every variant failed identically, so the remaining candidates are
 * the things the port does around a data command that Linux does differently:
 *
 *   MCI_TRIAL_NO_FD0    do not touch the vendor register at 0xfd0 at all
 *   MCI_TRIAL_FD0_ONES  write all-ones there instead of zero
 *   MCI_TRIAL_DMAC_ON   keep USE_INTERNAL_DMAC set even for a byte-mode
 *                       transfer (Linux clears it; the GT's idle dump has it
 *                       set, so which state the data engine wants is an open
 *                       question rather than a settled one)
 *   MCI_TRIAL_BUSMODE   leave BUS_MODE at the GT's idle value 0x200
 */
#define MCI_TRIAL_NO_FD0	(1u << 0)
#define MCI_TRIAL_FD0_ONES	(1u << 1)
#define MCI_TRIAL_DMAC_ON	(1u << 2)
#define MCI_TRIAL_BUSMODE	(1u << 3)
/* The control for the entry gate in mci_transfer(): with it set the gate is
 * skipped, which is exactly the pre-gate behaviour, so one image can measure
 * both sides of that change in the same boot. */
#define MCI_TRIAL_NO_GATE	(1u << 4)
void mci_set_trial(uint32_t flags);

#endif /* FREEWIFICAM_MCI_H */
