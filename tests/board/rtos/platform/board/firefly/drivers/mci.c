/*
 * @file   mci.c
 * @brief  Phytium MCI host controller driver (mmc1 = the SDIO socket on
 *         firefly v2), ported from the Linux driver that is proven on this
 *         board.
 *
 * Ported from linux-upstream/drivers/mmc/host/firefly-mci.c at the project's
 * GT pin (63a2a8ab9cd3). Every register programme below is that driver's,
 * function for function, with the names kept close to the original so a
 * reader can diff the two side by side:
 *
 *   Linux                                  here
 *   firefly_mci_init_hw()                  mci_init_hw()
 *   firefly_mci_set_clk()                  mci_set_clock()
 *   firefly_mci_update_external_clk()      mci_update_external_clk()
 *   firefly_mci_send_cmd()                 mci_send_private_cmd()
 *   firefly_mci_cmd_prepare_raw_cmd()      mci_make_raw_cmd()
 *   firefly_mci_start_data()               mci_start_data()
 *   firefly_mci_data_sg_write_2_admc_table mci_build_descs()
 *   firefly_mci_data_sg_write_2_fifo()     mci_fill_fifo()
 *   firefly_mci_cmd_done()                 mci_wait_cmd()
 *   firefly_mci_init_adma_table()          mci_init_adma_table()
 *   firefly_mci_restart_clk()              mci_restart_clk()
 *   firefly_mci_data_xfer_done()           mci_wait_data()
 *   firefly_mci_irq()                      mci_wait_cmd() (polled, no ISR)
 *
 * What the port removes: the mmc_request/mmc_command/mmc_data structures (the
 * port's own struct mci_cmd is the whole request), the scatter-gather walk
 * (one contiguous buffer), the completion interrupt (every completion is a
 * poll of RAW_INTS + DMAC_STATUS with a wall-clock budget), the request
 * timeout timer and power management. What it must not remove is the write
 * ORDER of a data command, the clock-field encoding, or the descriptor
 * attributes - those are the three things the vendor stack got wrong and this
 * driver got right on the same silicon.
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2021-2024 Phytium Technology Co., Ltd. (original driver)
 * Copyright (c) 2026 freewificam contributors (this port)
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "arch_asm.h"
#include "cache.h"
#include "generic_timer.h"
#include "log.h"

#include "mci.h"

/* --- register offsets (firefly-mci.h) ------------------------------------- */

#define MCI_CNTRL		0x000u	/* controller config */
#define MCI_PWREN		0x004u	/* power enable */
#define MCI_CLKDIV		0x008u	/* clock divider */
#define MCI_CLKENA		0x010u	/* clock enable */
#define MCI_TMOUT		0x014u	/* timeout */
#define MCI_CTYPE		0x018u	/* card type (bus width) */
#define MCI_BLKSIZ		0x01cu	/* block size */
#define MCI_BYTCNT		0x020u	/* byte count */
#define MCI_INT_MASK		0x024u	/* interrupt mask */
#define MCI_CMDARG		0x028u	/* command argument */
#define MCI_CMD			0x02cu	/* command */
#define MCI_RESP0		0x030u
#define MCI_RESP1		0x034u
#define MCI_RESP2		0x038u
#define MCI_RESP3		0x03cu
#define MCI_MASKED_INTS		0x040u	/* masked interrupt status */
#define MCI_RAW_INTS		0x044u	/* raw interrupt status (W1C) */
#define MCI_STATUS		0x048u	/* status */
#define MCI_FIFOTH		0x04cu	/* FIFO threshold watermark */
#define MCI_CARD_DETECT		0x050u	/* card detect */
#define MCI_CARD_WRTPRT		0x054u
#define MCI_CCLK_RDY		0x058u	/* first divider ready */
#define MCI_TRAN_CARD_CNT	0x05cu
#define MCI_TRAN_FIFO_CNT	0x060u
#define MCI_DEBNCE		0x064u
#define MCI_UID			0x068u
#define MCI_VID			0x06cu
#define MCI_HWCONF		0x070u
#define MCI_UHS_REG		0x074u
#define MCI_CARD_RESET		0x078u
#define MCI_BUS_MODE		0x080u	/* IDMAC bus mode */
#define MCI_DESC_LIST_ADDRL	0x088u
#define MCI_DESC_LIST_ADDRH	0x08cu
#define MCI_DMAC_STATUS		0x090u	/* IDMAC status */
#define MCI_DMAC_INT_ENA	0x094u
#define MCI_CUR_DESC_ADDRL	0x098u
#define MCI_CUR_DESC_ADDRH	0x09cu
#define MCI_CUR_BUF_ADDRL	0x0a0u
#define MCI_CUR_BUF_ADDRH	0x0a4u
#define MCI_CARD_THRCTL		0x100u
#define MCI_UHS_REG_EXT		0x108u
#define MCI_EMMC_DDR_REG	0x10cu
#define MCI_ENABLE_SHIFT	0x110u
#define MCI_CLK_DIVIDER		0x114u	/* the doubled first divider */
#define MCI_DATA		0x200u	/* data FIFO port */

/* An undocumented vendor register Linux writes on every interrupt
 * (firefly-mci.c:1185). Kept under the same offset it uses. */
#define MCI_VENDOR_FD0		0xfd0u
/* --- command register bits ------------------------------------------------ */

#define MCI_CMD_START		(1u << 31)
#define MCI_CMD_USE_HOLD_REG	(1u << 29)
#define MCI_CMD_VOLT_SWITCH	(1u << 28)
#define MCI_CMD_CCS_EXP		(1u << 23)
#define MCI_CMD_CEATA_RD	(1u << 22)
#define MCI_CMD_UPD_CLK		(1u << 21)
#define MCI_CMD_INIT		(1u << 15)
#define MCI_CMD_STOP		(1u << 14)
#define MCI_CMD_PRV_DAT_WAIT	(1u << 13)
#define MCI_CMD_SEND_STOP	(1u << 12)
#define MCI_CMD_STRM_MODE	(1u << 11)
#define MCI_CMD_DAT_WR		(1u << 10)
#define MCI_CMD_DAT_EXP		(1u << 9)
#define MCI_CMD_RESP_CRC	(1u << 8)
#define MCI_CMD_RESP_LONG	(1u << 7)
#define MCI_CMD_RESP_EXP	(1u << 6)
/* Six bits, not five: CMD52 and CMD53 live above 31 and a 0x1f mask turns
 * them into CMD20/CMD21, which take no response - the symptom is a timeout
 * on every SDIO command while SD/eMMC commands look fine. */
#define MCI_CMD_INDX(n)		((n) & 0x3fu)

/* --- CNTRL bits ----------------------------------------------------------- */

#define MCI_CNTRL_CONTROLLER_RESET	(1u << 0)
#define MCI_CNTRL_FIFO_RESET		(1u << 1)
#define MCI_CNTRL_DMA_RESET		(1u << 2)
#define MCI_CNTRL_DRV_SHIFT_EN		(1u << 3)
#define MCI_CNTRL_INT_ENABLE		(1u << 4)
#define MCI_CNTRL_DMA_ENABLE		(1u << 5)
#define MCI_CNTRL_READ_WAIT		(1u << 6)
#define MCI_CNTRL_ABORT_READ_DATA	(1u << 8)
#define MCI_CNTRL_START_BIT_MODE	(1u << 9)
#define MCI_CNTRL_CRC_SERIAL_DATA	(1u << 10)
#define MCI_CNTRL_USE_INTERNAL_DMAC	(1u << 25)

/* --- PWREN / CLKENA / CTYPE / UHS ---------------------------------------- */

#define MCI_PWREN_ENABLE		(1u << 0)
#define MCI_CLKENA_CCLK_ENABLE		(1u << 0)
#define MCI_CLKENA_CCLK_LOW_POWER	(1u << 16)
#define MCI_EXT_CLK_ENABLE		(1u << 1)

#define MCI_BUS_1BITS			0x0u
#define MCI_BUS_4BITS			0x1u
#define MCI_BUS_8BITS			(0x1u << 16)

#define MCI_UHS_REG_VOLT		(1u << 0)
#define MCI_UHS_REG_DDR			(1u << 16)
#define MCI_EMMC_DDR_CYCLE		(1u << 0)
#define MCI_CARD_RESET_ENABLE		(1u << 0)
#define MCI_UHS_REG_EXT_MMC_VOLT	(1u << 0)
#define MCI_UHS_REG_EXT_CLK_ENA		(1u << 1)

/* --- interrupt bits ------------------------------------------------------- */

#define MCI_INT_CD		(1u << 0)
#define MCI_INT_RE		(1u << 1)
#define MCI_INT_CMD		(1u << 2)
#define MCI_INT_DTO		(1u << 3)
#define MCI_INT_TXDR		(1u << 4)
#define MCI_INT_RXDR		(1u << 5)
#define MCI_INT_RCRC		(1u << 6)
#define MCI_INT_DCRC		(1u << 7)
#define MCI_INT_RTO		(1u << 8)
#define MCI_INT_DRTO		(1u << 9)
#define MCI_INT_HTO		(1u << 10)
#define MCI_INT_FRUN		(1u << 11)
#define MCI_INT_HLE		(1u << 12)
#define MCI_INT_SBE_BCI		(1u << 13)
#define MCI_INT_ACD		(1u << 14)
#define MCI_INT_EBE		(1u << 15)
#define MCI_INT_SDIO		(1u << 16)

/* What the command half is allowed to acknowledge. It must not touch DTO,
 * RXDR, TXDR or the data errors: those belong to mci_wait_data(), and RAW_INTS
 * is write-one-to-clear across both halves. */
#define MCI_CMD_CLEAR_BITS	(MCI_INT_CMD | MCI_INT_RTO | MCI_INT_RCRC | \
				 MCI_INT_RE | MCI_INT_HTO | MCI_INT_ACD)

/* --- STATUS --------------------------------------------------------------- */

#define MCI_STATUS_FIFO_RX	(1u << 0)
#define MCI_STATUS_FIFO_TX	(1u << 1)
#define MCI_STATUS_FIFO_EMPTY	(1u << 2)
#define MCI_STATUS_FIFO_FULL	(1u << 3)
#define MCI_STATUS_CARD_STATUS	(1u << 8)
#define MCI_STATUS_CARD_BUSY	(1u << 9)
#define MCI_STATUS_DATA_BUSY	(1u << 10)
#define MCI_STATUS_RESP_IDX_SHIFT	11
#define MCI_STATUS_RESP_IDX_MASK	(0x3fu << MCI_STATUS_RESP_IDX_SHIFT)

/* --- IDMAC ---------------------------------------------------------------- */

#define MCI_BUS_MODE_SWR	(1u << 0)
#define MCI_BUS_MODE_FB		(1u << 1)
#define MCI_BUS_MODE_DE		(1u << 7)

#define MCI_DMAC_TI		(1u << 0)
#define MCI_DMAC_RI		(1u << 1)
#define MCI_DMAC_FBE		(1u << 2)
#define MCI_DMAC_DU		(1u << 4)
#define MCI_DMAC_NIS		(1u << 8)
#define MCI_DMAC_AIS		(1u << 9)
#define MCI_DMAC_ALL		0x1ffu

#define MCI_DMAC_ENA_CES	(1u << 5)

/* --- fixed shapes --------------------------------------------------------- */

#define MCI_MAX_BD_NUM		128u
#define MCI_ADMA2_64_DESC_SZ	32u
#define MCI_DESC_MAX_LEN	4096u	/* one descriptor covers up to 4 KiB */

/* Polling budgets: wall-clock, not iteration counts. A command on a 50 MHz bus
 * completes in tens of microseconds, so the command budget covers a stalled
 * bus many times over; the data budget covers a 512 KiB block transfer at the
 * slowest card clock this port uses. */
#define MCI_CMD_BUDGET_US	100000u
#define MCI_DATA_BUDGET_US	300000u
#define MCI_RESET_BUDGET_US	50000u
#define MCI_POLL_US_DEFAULT	10u

/* The address of the IDMA (ADMA2 64-bit) descriptor. The layout is the Linux
 * driver's, field for field: attribute, two unused words, length, two unused
 * words, the buffer's two address halves, then the next descriptor's. */
struct mci_adma2_64_desc {
	volatile uint32_t	attribute;
	volatile uint32_t	non1;
	volatile uint32_t	len;
	volatile uint32_t	non2;
	volatile uint32_t	addr_lo;
	volatile uint32_t	addr_hi;
	volatile uint32_t	desc_lo;
	volatile uint32_t	desc_hi;
} __attribute__((packed, aligned(4)));

/* the attribute words, as the Linux driver writes them */
#define MCI_DESC_ATTR_OWN	(1u << 31)
#define MCI_DESC_ATTR_DIC	(1u << 1)	/* disable interrupt on completion */
#define MCI_DESC_ATTR_LD	(1u << 2)	/* last descriptor */
#define MCI_DESC_ATTR_FD	(1u << 3)	/* first descriptor */
#define MCI_DESC_ATTR_CH	(1u << 4)	/* chain to the next descriptor */
#define MCI_DESC_SINGLE		(MCI_DESC_ATTR_OWN | MCI_DESC_ATTR_LD | MCI_DESC_ATTR_FD)
#define MCI_DESC_FIRST		(MCI_DESC_ATTR_OWN | MCI_DESC_ATTR_DIC | MCI_DESC_ATTR_FD | MCI_DESC_ATTR_CH)
#define MCI_DESC_MIDDLE		(MCI_DESC_ATTR_OWN | MCI_DESC_ATTR_DIC | MCI_DESC_ATTR_CH)
#define MCI_DESC_LAST		(MCI_DESC_ATTR_OWN | MCI_DESC_ATTR_LD)

/*
 * The two masks Linux composes, reproduced bit for bit. They are what a trace
 * line's `imask=` field must show: 0x1546 for a command without data, 0x37ce
 * with a data stage. (The GT trace shows those two ORed with 0x10000, the
 * card-interrupt enable the Linux mmc core raises because the board's DTS says
 * cap-sdio-irq; this port has no DAT1 consumer, so it does not raise it.)
 */
#define MCI_CMD_INTS_MASK	(MCI_INT_RE | MCI_INT_CMD | MCI_INT_RCRC | \
				 MCI_INT_RTO | MCI_INT_HTO | MCI_INT_HLE)
#define MCI_DATA_INTS_MASK	(MCI_INT_DTO | MCI_INT_DCRC | MCI_INT_DRTO | \
				 MCI_INT_SBE_BCI)
#define MCI_CMD_ERR_INTS_MASK	(MCI_INT_RTO | MCI_INT_RCRC | MCI_INT_RE | \
				 MCI_INT_DCRC | MCI_INT_DRTO | MCI_INT_SBE_BCI | \
				 MCI_INT_EBE)
#define MCI_DATA_ERR_INTS_MASK	(MCI_INT_DRTO | MCI_INT_DCRC | MCI_INT_EBE | \
				 MCI_INT_SBE_BCI)
#define MCI_DMAC_INTS_MASK	(MCI_DMAC_FBE | MCI_DMAC_DU | MCI_DMAC_NIS | MCI_DMAC_AIS)
#define MCI_DMAC_ERR_INTS_MASK	(MCI_DMAC_FBE | MCI_DMAC_DU | MCI_DMAC_AIS)

/* the transfer shapes Linux distinguishes */
enum mci_adtc {
	MCI_ADTC_COMMON	= 0,	/* through the FIFO, CPU copies */
	MCI_ADTC_BLOCK	= 1,	/* through the IDMAC, descriptor list */
};

/*
 * The descriptor list lives in the linker's non-cacheable window: a 32-byte
 * descriptor shares a 64-byte line with its neighbours, so maintaining one by
 * hand would push a stale neighbour back over what the engine wrote. The
 * window is identity-mapped, so its virtual address is the one the engine
 * takes.
 */
static struct mci_adma2_64_desc mci_desc_pool[MCI_MAX_BD_NUM]
	__attribute__((section(".noncacheable"), aligned(64)));

static uint32_t mci_poll_us = MCI_POLL_US_DEFAULT;
static int mci_trace_on = 1;
/* Slow-motion probe: see mci_set_poll_probe() in the header. Zero disarmed;
 * a positive count makes the next data transfer print that many poll passes. */
static uint32_t mci_probe_samples = 0u;
/* Experiment knobs: extra CNTRL bits applied to every data command, and extra
 * FIFOTH bits applied with them. Both zero in normal operation. */
static uint32_t mci_cntrl_extra = 0u;
static uint32_t mci_fifoth_extra = 0u;
/* Preconditions for the same experiment: see mci_set_trial(). */
static uint32_t mci_trial_flags = 0u;

/* The controllers this board wires: mmc0 is the TF slot, mmc1 the SDIO part.
 * Both are the same IP; the numbers come from the board's device tree (mmc0
 * @0x28000000 GIC_SPI 72, mmc1 @0x28001000 GIC_SPI 73) and a GIC SPI n is
 * INTID n + 32. */
static const struct mci_cfg mci_board[] = {
	{ .base = 0x28000000UL, .index = 0u, .intid = 104 },
	{ .base = 0x28001000UL, .index = 1u, .intid = 105 },
};

const struct mci_cfg *mci_board_cfg(unsigned int index)
{
	if (index >= (unsigned int)(sizeof(mci_board) / sizeof(mci_board[0]))) {
		return NULL;
	}

	return &mci_board[index];
}

/* --- helpers -------------------------------------------------------------- */

static inline uint32_t mci_rd(struct mci_host *hc, uint32_t off)
{
	return *(volatile uint32_t *)(hc->cfg.base + off);
}

static inline void mci_wr(struct mci_host *hc, uint32_t off, uint32_t val)
{
	*(volatile uint32_t *)(hc->cfg.base + off) = val;
}

static void mci_set_bits(struct mci_host *hc, uint32_t off, uint32_t bits)
{
	mci_wr(hc, off, mci_rd(hc, off) | bits);
}

static void mci_clr_bits(struct mci_host *hc, uint32_t off, uint32_t bits)
{
	mci_wr(hc, off, mci_rd(hc, off) & ~bits);
}

static void mci_udelay(uint32_t usec)
{
	uint64_t freq = (uint64_t)gtimer_freq_get();
	uint64_t ticks = (freq * (uint64_t)usec) / 1000000ULL;
	uint64_t start = gtimer_count_get();

	if (ticks == 0ULL) {
		ticks = 1ULL;
	}
	while ((gtimer_count_get() - start) < ticks) {
		/* spin */
	}
}

/* The iteration budget that covers a wall-clock budget at the poll step. */
static uint32_t mci_polls_for(uint32_t budget_us)
{
	uint32_t n = budget_us / ((mci_poll_us == 0u) ? 1u : mci_poll_us);

	return (n == 0u) ? 1u : n;
}

/* --- trace ---------------------------------------------------------------- */

void mci_trace_set(int on)
{
	mci_trace_on = (on != 0);
}

#define MCI_TRACE(...) \
	do { if (mci_trace_on) LOG_ANCHOR(__VA_ARGS__); } while (0)

void mci_trace_dump(struct mci_host *hc)
{
	uint32_t off;

	for (off = 0u; off <= 0x124u; off += 4u) {
		LOG_ANCHOR("MCI RD %03x %08x\n", (unsigned)off, mci_rd(hc, off));
	}
}

/*
 * The failure snapshot. RAW_INTS is write-one-to-clear and the driver clears
 * it on the way out, so a reader that arrives after a failure sees zeros -
 * this struct is the only record of what the controller held at the instant
 * the transfer stopped being a transfer.
 */
static void mci_snapshot(struct mci_host *hc, const struct mci_cmd *cmd,
			 uint32_t rawints, uint32_t dmac)
{
	struct mci_state *st = &hc->st;

	st->last_cmd = cmd->index;
	st->last_arg = cmd->arg;
	st->rawints = rawints;
	st->masked = mci_rd(hc, MCI_MASKED_INTS);
	st->status = mci_rd(hc, MCI_STATUS);
	st->cmd = mci_rd(hc, MCI_CMD);
	st->resp0 = mci_rd(hc, MCI_RESP0);
	st->dmac = dmac;
	st->blksz = mci_rd(hc, MCI_BLKSIZ);
	st->bytcnt = mci_rd(hc, MCI_BYTCNT);
	st->int_mask = mci_rd(hc, MCI_INT_MASK);
	st->cntrl = mci_rd(hc, MCI_CNTRL);
	st->clkdiv = mci_rd(hc, MCI_CLKDIV);
	st->uhs_ext = mci_rd(hc, MCI_UHS_REG_EXT);
	st->ctype = mci_rd(hc, MCI_CTYPE);
	st->clk_divider = mci_rd(hc, MCI_CLK_DIVIDER);
	st->cur_ios_clk = hc->cur_ios_clk;
	st->bus_width = hc->width;
}

/* --- the controller's own sequences (firefly_mci_init_hw and friends) ----- */

static void mci_update_external_clk(struct mci_host *hc, uint32_t uhs_reg_value)
{
	uint32_t loop = mci_polls_for(MCI_RESET_BUDGET_US);

	mci_wr(hc, MCI_UHS_REG_EXT, 0u);
	mci_wr(hc, MCI_UHS_REG_EXT, uhs_reg_value);
	while ((mci_rd(hc, MCI_CCLK_RDY) & 0x1u) == 0u) {
		if (--loop == 0u) {
			LOG_ANCHOR("MCI: CCLK_RDY never set (uhs_ext=0x%08x)\n",
				   uhs_reg_value);
			break;
		}
		mci_udelay(1u);
	}
}

/* firefly_mci_send_cmd(): the private-command path (UPD_CLK). */
static void mci_send_private_cmd(struct mci_host *hc, uint32_t cmd, uint32_t arg)
{
	uint32_t loop;
	uint32_t data;

	mci_wr(hc, MCI_CMDARG, arg);

	loop = mci_polls_for(MCI_RESET_BUDGET_US);
	do {
		data = mci_rd(hc, MCI_STATUS);
		if ((data & MCI_STATUS_CARD_BUSY) == 0u) {
			break;
		}
		mci_udelay(mci_poll_us);
	} while (--loop != 0u);

	mci_wr(hc, MCI_CMD, MCI_CMD_START | cmd);

	loop = mci_polls_for(MCI_RESET_BUDGET_US);
	do {
		data = mci_rd(hc, MCI_CMD);
		if ((data & MCI_CMD_START) == 0u) {
			break;
		}
		mci_udelay(mci_poll_us);
	} while (--loop != 0u);
}

static void mci_reset_hw(struct mci_host *hc)
{
	uint32_t loop;

	mci_set_bits(hc, MCI_CNTRL, MCI_CNTRL_FIFO_RESET | MCI_CNTRL_DMA_RESET);

	loop = mci_polls_for(MCI_RESET_BUDGET_US);
	while ((mci_rd(hc, MCI_CNTRL) &
		(MCI_CNTRL_FIFO_RESET | MCI_CNTRL_DMA_RESET)) != 0u) {
		if (--loop == 0u) {
			LOG_ANCHOR("MCI: FIFO/DMA reset did not self-clear\n");
			break;
		}
		mci_udelay(1u);
	}
	mci_send_private_cmd(hc, MCI_CMD_UPD_CLK, 0u);
}

/* firefly_mci_restart_clk(): re-latch the external clock with the divider that
 * is already programmed. This is the step the vendor driver never restores -
 * its error path writes the 400 kHz field back and leaves the card talking at
 * a frequency nothing else knows about. */
static void mci_restart_clk(struct mci_host *hc)
{
	uint32_t clk_div;
	uint32_t uhs;

	while ((mci_rd(hc, MCI_CMD) & MCI_CMD_START) != 0u) {
		/* wait */
	}
	mci_clr_bits(hc, MCI_CLKENA, MCI_CLKENA_CCLK_ENABLE);
	clk_div = mci_rd(hc, MCI_CLKDIV);
	uhs = mci_rd(hc, MCI_UHS_REG_EXT);
	mci_wr(hc, MCI_UHS_REG_EXT, 0u);
	mci_wr(hc, MCI_UHS_REG_EXT, uhs);
	while ((mci_rd(hc, MCI_CCLK_RDY) & 0x1u) == 0u) {
		/* wait */
	}

	mci_wr(hc, MCI_CLKDIV, clk_div);
	mci_set_bits(hc, MCI_CLKENA, MCI_CLKENA_CCLK_ENABLE);
	mci_wr(hc, MCI_CMD, MCI_CMD_START | MCI_CMD_UPD_CLK);
	while ((mci_rd(hc, MCI_CMD) & MCI_CMD_START) != 0u) {
		/* wait */
	}
}

static void mci_init_hw(struct mci_host *hc)
{
	uint32_t val;

	/* FIFOTH: MSIZE=2 (16 transfers), RX watermark 7, TX watermark 0x100 */
	mci_wr(hc, MCI_FIFOTH, ((2u & 0x7u) << 28) | ((7u & 0xfffu) << 16) | (0x100u & 0xfffu));
	/* CARD_THRCTL: read threshold 0x80, both read and write thresholds on */
	mci_wr(hc, MCI_CARD_THRCTL, 0x800001u);
	mci_clr_bits(hc, MCI_CLKENA, MCI_CLKENA_CCLK_ENABLE);
	mci_update_external_clk(hc, 0x502u);

	mci_set_bits(hc, MCI_PWREN, MCI_PWREN_ENABLE);
	mci_set_bits(hc, MCI_CLKENA, MCI_CLKENA_CCLK_ENABLE);
	mci_set_bits(hc, MCI_UHS_REG_EXT, MCI_UHS_REG_EXT_CLK_ENA);
	mci_clr_bits(hc, MCI_UHS_REG, MCI_UHS_REG_VOLT);
	mci_clr_bits(hc, MCI_EMMC_DDR_REG, MCI_EMMC_DDR_CYCLE);

	mci_reset_hw(hc);

	/* mmc1 is non-removable: hold the card out of reset permanently */
	mci_set_bits(hc, MCI_CARD_RESET, MCI_CARD_RESET_ENABLE);

	mci_wr(hc, MCI_INT_MASK, 0u);
	val = mci_rd(hc, MCI_RAW_INTS);
	mci_wr(hc, MCI_RAW_INTS, val);
	mci_wr(hc, MCI_DMAC_INT_ENA, 0u);
	val = mci_rd(hc, MCI_DMAC_STATUS);
	mci_wr(hc, MCI_DMAC_STATUS, val);

	mci_set_bits(hc, MCI_CNTRL,
		     MCI_CNTRL_INT_ENABLE | MCI_CNTRL_USE_INTERNAL_DMAC);

	mci_wr(hc, MCI_TMOUT, 0xffffffffu);

	/* The vendor's own register at 0xfd0: Linux clears it as the very first
	 * statement of firefly_mci_irq(), outside any lock and before reading
	 * RAW_INTS, so on that path it is cleared on every interrupt. The port
	 * polls instead of taking the interrupt, which means nothing would ever
	 * write it - and a sticky bit left set there holds the data path down
	 * (the symptom is a CMD53 whose R5 comes back clean, whose DATA_BUSY
	 * never clears, and whose DTO never arrives). No documentation covers
	 * this offset; the value and the place are copied from the Linux
	 * driver because that driver is the one that works on this silicon. */
	mci_wr(hc, MCI_VENDOR_FD0, 0u);
}

/* --- the descriptor list (firefly_mci_init_adma_table) -------------------- */

static void mci_adma_reset(struct mci_host *hc)
{
	uint32_t bmod = mci_rd(hc, MCI_BUS_MODE);
	uint32_t loop = mci_polls_for(MCI_RESET_BUDGET_US);

	mci_wr(hc, MCI_BUS_MODE, bmod | MCI_BUS_MODE_SWR);
	while ((mci_rd(hc, MCI_BUS_MODE) & MCI_BUS_MODE_SWR) != 0u) {
		if (--loop == 0u) {
			LOG_ANCHOR("MCI: IDMAC reset did not self-clear\n");
			break;
		}
		mci_udelay(1u);
	}
}

static void mci_init_adma_table(struct mci_host *hc)
{
	uintptr_t pool = (uintptr_t)mci_desc_pool;
	unsigned int i;

	(void)memset(mci_desc_pool, 0, sizeof(mci_desc_pool));

	for (i = 0u; i + 1u < MCI_MAX_BD_NUM; i++) {
		uintptr_t next = pool + (uintptr_t)sizeof(struct mci_adma2_64_desc) *
				 (uintptr_t)(i + 1u);

		mci_desc_pool[i].desc_lo = (uint32_t)next;
		mci_desc_pool[i].desc_hi = (uint32_t)((uint64_t)next >> 32);
		mci_desc_pool[i].attribute = 0u;
	}

	mci_adma_reset(hc);
}

/*
 * Build the chain for one contiguous buffer. Linux walks the request's
 * scatter-gather list with the same four attribute words; a single buffer is
 * the one-segment case of that walk, and the split points (4 KiB per
 * descriptor) are the controller's own maximum, not a choice.
 */
static uint32_t mci_build_descs(const struct mci_data *d)
{
	uintptr_t pool = (uintptr_t)mci_desc_pool;
	uintptr_t addr = (uintptr_t)d->buf;
	uint32_t remain = d->len;
	uint32_t n = 0u;

	while ((remain > 0u) && (n < MCI_MAX_BD_NUM)) {
		struct mci_adma2_64_desc *desc = &mci_desc_pool[n];
		uint32_t chunk = (remain > MCI_DESC_MAX_LEN) ? MCI_DESC_MAX_LEN : remain;

		desc->attribute = MCI_DESC_ATTR_OWN;
		desc->len = chunk;
		desc->addr_lo = (uint32_t)addr;
		desc->addr_hi = (uint32_t)(((uint64_t)addr) >> 32);
		desc->non1 = 0u;
		desc->non2 = 0u;

		remain -= chunk;
		addr += chunk;
		n++;

		if (remain == 0u) {
			/* last (and, for a single chunk, also first) */
			desc->attribute = (n == 1u) ? MCI_DESC_SINGLE : MCI_DESC_LAST;
			desc->desc_lo = 0u;
			desc->desc_hi = 0u;
		} else if (n == 1u) {
			desc->attribute = MCI_DESC_FIRST;
			desc->desc_lo = (uint32_t)(pool +
				(uintptr_t)(n) * (uintptr_t)sizeof(*desc));
			desc->desc_hi = (uint32_t)(((uint64_t)(pool +
				(uintptr_t)(n) * (uintptr_t)sizeof(*desc))) >> 32);
		} else {
			desc->attribute = MCI_DESC_MIDDLE;
			desc->desc_lo = (uint32_t)(pool +
				(uintptr_t)(n) * (uintptr_t)sizeof(*desc));
			desc->desc_hi = (uint32_t)(((uint64_t)(pool +
				(uintptr_t)(n) * (uintptr_t)sizeof(*desc))) >> 32);
		}
	}

	return n;
}

/* --- the command word (firefly_mci_cmd_prepare_raw_cmd) ------------------- */

static uint32_t mci_resp_field(uint32_t resp_class)
{
	switch (resp_class) {
	case MCI_RESP_SHORT_CRC:
		return 0x5u;	/* R1/R1B/R5/R6/R7: present, CRC, opcode */
	case MCI_RESP_LONG_CRC:
		return 0x7u;	/* R2: present, 136-bit, CRC */
	case MCI_RESP_SHORT:
		return 0x1u;	/* R3/R4: present, no CRC */
	default:
		return 0x0u;
	}
}

static uint32_t mci_make_raw_cmd(struct mci_host *hc, const struct mci_cmd *cmd)
{
	uint32_t raw = MCI_CMD_INDX(cmd->index) |
		       ((mci_resp_field(cmd->resp_class) & 0x7u) << 6);

	/* CMD15 leaves the card inactive; a CMD52 to the abort register is the
	 * SDIO form of the same request. Both carry the STOP bit. */
	if ((cmd->index == 15u) ||
	    ((cmd->index == 52u) && (((cmd->arg >> 9) & 0x1ffffu) == 0x06u))) {
		raw |= MCI_CMD_STOP;
	} else if (cmd->index == 11u) {	/* SD_SWITCH_VOLTAGE */
		raw |= MCI_CMD_VOLT_SWITCH;
	}

	if (hc->need_init) {
		hc->need_init = false;
		raw |= MCI_CMD_INIT;
	}

	if (cmd->data != NULL) {
		raw |= MCI_CMD_DAT_EXP;
		if (cmd->data->write) {
			raw |= MCI_CMD_DAT_WR;
		}
	}

	return raw | MCI_CMD_START;
}

/* --- clock (firefly_mci_set_clk) ------------------------------------------ */

uint32_t mci_set_clock(struct mci_host *hc, uint32_t hz)
{
	uint32_t div = 0xffu;
	uint32_t drv = 0u;
	uint32_t sample = 0u;
	uint32_t cmd_reg;
	uint32_t cur_cmd_index;
	uint32_t first_uhs_div;
	uint32_t tmp_ext_reg;
	uint32_t mci_cmd_bits = MCI_CMD_UPD_CLK;

	cmd_reg = mci_rd(hc, MCI_CMD);
	cur_cmd_index = cmd_reg & 0x3fu;

	if (cur_cmd_index == 11u) {	/* SD_SWITCH_VOLTAGE */
		mci_cmd_bits |= MCI_CMD_VOLT_SWITCH;
	}

	if (hz == 0u) {
		hc->cur_ios_clk = 0u;
		mci_clr_bits(hc, MCI_CLKENA, MCI_CLKENA_CCLK_ENABLE);
		mci_send_private_cmd(hc, mci_cmd_bits, 0u);
		mci_clr_bits(hc, MCI_UHS_REG_EXT, MCI_UHS_REG_EXT_CLK_ENA);
		return 0u;
	}

	if (hc->cur_ios_clk == hz) {
		return hz;
	}

	/* The external-clock field picks the FIRST divider: 0x102 = div 2 for
	 * anything at or above 25 MHz, 0x502 = div 6 for the 400 kHz
	 * identification clock, 0x302 = div 4 in between. */
	if (hz >= 25000000u) {
		tmp_ext_reg = 0x102u;
	} else if (hz == 400000u) {
		tmp_ext_reg = 0x502u;
	} else {
		tmp_ext_reg = 0x302u;
	}

	mci_update_external_clk(hc, tmp_ext_reg);
	mci_clr_bits(hc, MCI_CLKENA, MCI_CLKENA_CCLK_ENABLE);
	mci_send_private_cmd(hc, mci_cmd_bits, 0u);

	first_uhs_div = 1u + ((tmp_ext_reg >> 8) & 0xffu);
	div = (uint32_t)(MCI_SRC_CLK_HZ / (2u * first_uhs_div * hz));

	if (div > 2u) {
		sample = div / 2u + 1u;
		drv = sample - 1u;
		mci_wr(hc, MCI_CLKDIV, (sample << 16) | (drv << 8) | (div & 0xffu));
	} else if (div == 2u) {
		drv = 0u;
		sample = 1u;
		mci_wr(hc, MCI_CLKDIV, (drv << 8) | (sample << 16) | (div & 0xffu));
	}

	/* DDR / HS400 / UHS-DDR50 need the serial CRC path; the SDIO part on
	 * this board is SDR only, so this is always the clear branch (kept
	 * because it is part of the sequence that produced the GT). */
	mci_clr_bits(hc, MCI_CNTRL, MCI_CNTRL_CRC_SERIAL_DATA);
	mci_clr_bits(hc, MCI_CNTRL, MCI_CNTRL_DRV_SHIFT_EN);

	if (div >= 2u) {
		mci_wr(hc, MCI_CLK_DIVIDER, (2u * (div & 0xffu)) & 0xffffu);
	}

	mci_set_bits(hc, MCI_CLKENA, MCI_CLKENA_CCLK_ENABLE);
	mci_send_private_cmd(hc, mci_cmd_bits, 0u);

	hc->cur_ios_clk = hz;

	MCI_TRACE("MCI CLK ios=%u div=%u uhs_ext=%08x clkdiv=%08x clkdiv2=%08x "
		  "clkena=%08x\n", (unsigned)hz, (unsigned)div,
		  mci_rd(hc, MCI_UHS_REG_EXT), mci_rd(hc, MCI_CLKDIV),
		  mci_rd(hc, MCI_CLK_DIVIDER), mci_rd(hc, MCI_CLKENA));

	/* What the divider chain actually produces, which is not always the
	 * request: the stack's card-clock bookkeeping wants the achieved rate
	 * (its mode ladder steps down to it). */
	if (div == 0u) {
		return hz;
	}

	return (uint32_t)(MCI_SRC_CLK_HZ / (2u * first_uhs_div * div));
}

void mci_set_bus_width(struct mci_host *hc, unsigned int width)
{
	uint32_t val;

	switch (width) {
	case 1u:
		val = MCI_BUS_1BITS;
		break;
	case 4u:
		val = MCI_BUS_4BITS;
		break;
	case 8u:
		val = MCI_BUS_8BITS;
		break;
	default:
		val = MCI_BUS_4BITS;
		break;
	}

	hc->width = width;
	mci_wr(hc, MCI_CTYPE, val);
}

void mci_power(struct mci_host *hc, bool on)
{
	if (on) {
		hc->need_init = true;
		mci_wr(hc, MCI_PWREN, 1u);
	} else {
		mci_wr(hc, MCI_PWREN, 0u);
	}
}

bool mci_card_present(const struct mci_host *hc)
{
	(void)hc;

	/* mmc1 is non-removable (the DTS says so), so the slot always reads
	 * occupied. There is nothing to poll: a dead socket shows up as a
	 * command that never answers, not as an absent card. */
	return true;
}

/* --- completion ----------------------------------------------------------- */

/*
 * The completion waits, in the two pieces Linux keeps them:
 * firefly_mci_cmd_done() (firefly-mci.c:754-822) and
 * firefly_mci_data_xfer_done() (:1021-1079) are separate handlers on separate
 * event sets, and their conditions are not the same one.
 *
 * The command half. Linux treats the command as done the moment CMD latches,
 * even when RTO latched with it, and only reads cmd->error out of the cases
 * where CMD did NOT arrive (:805). The controller latches both bits on a card
 * that answered late, and the response register is valid in that case, so
 * "CMD or error" is not the same question as "CMD and no error". Measured on
 * this board: the first CMD53 of every boot reports ev=00000004 (CMD, no RTO)
 * under the split, where the single-condition wait reported -2 on the same
 * event because DTO had not arrived yet - it never would, and the wait ended
 * by budget instead of by event.
 *
 * The data half. DTO is the completion; the data errors (DRTO/DCRC/EBE/SBE)
 * end it early. RAW_INTS is write-one-to-clear and the driver clears it on the
 * way out, so each half snapshots the registers BEFORE clearing: after the
 * clear they read back zero.
 */
static void mci_wait_idle(struct mci_host *hc)
{
	uint32_t loop = mci_polls_for(MCI_CMD_BUDGET_US);
	uint32_t waits = 0u;

	/* The same two conditions firefly_mci_request() waits for, with the same
	 * consequence: Linux waits and then submits anyway, it does not fail the
	 * request. Keeping that shape means this gate cannot turn a would-be
	 * success into a failure - it only removes the case where a command is
	 * written into a controller that is still busy with the last one. */
	do {
		if ((mci_rd(hc, MCI_STATUS) & MCI_STATUS_CARD_BUSY) == 0u) {
			break;
		}
		mci_udelay(mci_poll_us);
		waits++;
	} while (--loop != 0u);

	loop = mci_polls_for(MCI_CMD_BUDGET_US);
	while ((mci_rd(hc, MCI_CMD) & MCI_CMD_START) != 0u) {
		if (--loop == 0u) {
			break;
		}
		mci_udelay(mci_poll_us);
		waits++;
	}

	/* Only a gate that actually had to wait is worth a line: a clean boot
	 * says nothing here. */
	if (waits != 0u) {
		hc->st.gate_waits++;
		hc->st.gate_last = waits;
	}
}

static int mci_wait_cmd(struct mci_host *hc, const struct mci_cmd *cmd)
{
	uint32_t loop = mci_polls_for(MCI_CMD_BUDGET_US);
	uint32_t rawints = 0u;
	uint32_t dmac = 0u;

	do {
		rawints = mci_rd(hc, MCI_RAW_INTS);

		/* CMD is the completion, the same way Linux takes it. A card
		 * that answers after its response window has both CMD and RTO
		 * latched, and its R5 is in RESP0 either way. */
		if ((rawints & MCI_INT_CMD) != 0u) {
			mci_snapshot(hc, cmd, rawints, 0u);
			hc->st.cmd_ev = rawints;
			mci_wr(hc, MCI_INT_MASK, 0u);
			/*
			 * Clear ONLY the command half's own bits. RAW_INTS is
			 * write-one-to-clear and shared with the data half: the
			 * card answers a CMD53 with CMD and DTO latched in the
			 * same register read, and clearing the whole word here
			 * throws DTO away before mci_wait_data() ever looks.
			 * Measured: the command half logged cmd_ev=0000000c
			 * (CMD|DTO) and the data half then sat out its full
			 * budget on data_ev=00000000, turning a completed
			 * transfer into "data wait timed out".
			 */
			mci_wr(hc, MCI_RAW_INTS, rawints & MCI_CMD_CLEAR_BITS);
			return MCI_OK;
		}

		if ((rawints & (MCI_INT_RTO | MCI_INT_RCRC | MCI_INT_RE |
				MCI_INT_HTO)) != 0u) {
			mci_snapshot(hc, cmd, rawints, 0u);
			hc->st.cmd_ev = rawints;
			mci_wr(hc, MCI_INT_MASK, 0u);
			mci_wr(hc, MCI_RAW_INTS, rawints & MCI_CMD_CLEAR_BITS);
			if ((rawints & MCI_INT_RTO) != 0u) {
				return MCI_ERR_TIMEOUT;
			}
			return (rawints & MCI_INT_RE) ? MCI_ERR_CMD
						      : MCI_ERR_DATA;
		}

		mci_udelay(mci_poll_us);
	} while (--loop != 0u);

	rawints = mci_rd(hc, MCI_RAW_INTS);
	dmac = 0u;
	mci_snapshot(hc, cmd, rawints, dmac);
	hc->st.cmd_ev = rawints;

	LOG_ANCHOR("MCI: CMD%u command wait timed out rawints=%08x status=%08x\n",
		   (unsigned)cmd->index, rawints, mci_rd(hc, MCI_STATUS));
	mci_wr(hc, MCI_INT_MASK, 0u);
	mci_wr(hc, MCI_RAW_INTS, rawints & 0xffffeu);

	return MCI_ERR_TIMEOUT;
}

/*
 * The data half. `probe_samples` makes the next data wait print its first N
 * poll passes verbatim - see mci_set_poll_probe() in the header for which
 * question each field answers. The time series is taken before any clear, and
 * only on the first data transfer after the probe is armed, so one trace is
 * one transfer's shape and not a mixture.
 */
static int mci_wait_data(struct mci_host *hc, const struct mci_cmd *cmd, bool idmac)
{
	uint32_t loop = mci_polls_for(MCI_DATA_BUDGET_US);
	uint32_t rawints = 0u;
	uint32_t dmac = 0u;
	uint32_t probe_left = mci_probe_samples;
	uint32_t probe_index = 0u;

	if (probe_left != 0u) {
		mci_probe_samples = 0u;
	}

	do {
		rawints = mci_rd(hc, MCI_RAW_INTS);
		dmac = idmac ? mci_rd(hc, MCI_DMAC_STATUS) : 0u;

		if (probe_left != 0u) {
			LOG_ANCHOR("MCI P%02u raw=%08x st=%08x cmd=%08x dmac=%08x "
				   "blk=%08x byt=%08x\n",
				   (unsigned)probe_index, rawints,
				   mci_rd(hc, MCI_STATUS), mci_rd(hc, MCI_CMD),
				   dmac, mci_rd(hc, MCI_BLKSIZ),
				   mci_rd(hc, MCI_BYTCNT));
			probe_index++;
			probe_left--;
		}

		if ((rawints & MCI_DATA_ERR_INTS_MASK) != 0u) {
			mci_snapshot(hc, cmd, rawints, dmac);
			hc->st.data_ev = rawints;
			mci_wr(hc, MCI_INT_MASK, 0u);
			mci_wr(hc, MCI_RAW_INTS, rawints & 0xffffeu);
			if (idmac) {
				mci_wr(hc, MCI_DMAC_INT_ENA, 0u);
				mci_wr(hc, MCI_DMAC_STATUS, dmac);
			}
			if ((rawints & MCI_INT_DRTO) != 0u) {
				return MCI_ERR_TIMEOUT;
			}
			return MCI_ERR_DATA;
		}

		if (idmac && ((dmac & MCI_DMAC_ERR_INTS_MASK) != 0u)) {
			mci_snapshot(hc, cmd, rawints, dmac);
			mci_wr(hc, MCI_INT_MASK, 0u);
			mci_wr(hc, MCI_RAW_INTS, rawints & 0xffffeu);
			mci_wr(hc, MCI_DMAC_INT_ENA, 0u);
			mci_wr(hc, MCI_DMAC_STATUS, dmac);
			return MCI_ERR_DATA;
		}

		if ((rawints & MCI_INT_DTO) != 0u) {
			hc->st.data_ev = rawints;
			/*
			 * The IDMAC's RI is a hint, not a condition.
			 *
			 * The first port waited for DMAC_STATUS.RI before
			 * accepting a completed read, on the reasoning that the
			 * FIFO-to-memory drain trails DTO. Measured on this
			 * board, RI is set for exactly one poll pass and then
			 * reads back zero: the SD control on mmc0 shows
			 * "P00 ... dmac=00006000" followed by "P01 ...
			 * dmac=00000000", while the data itself has already
			 * crossed. Waiting for a bit the hardware has already
			 * consumed turns a completed transfer into a timeout,
			 * which is exactly what every CMD53 and every CMD17 did
			 * before the split.
			 */
			mci_snapshot(hc, cmd, rawints, dmac);
			mci_wr(hc, MCI_INT_MASK, 0u);
			mci_wr(hc, MCI_RAW_INTS, rawints & 0xffffeu);
			if (idmac) {
				mci_wr(hc, MCI_DMAC_INT_ENA, 0u);
				mci_wr(hc, MCI_DMAC_STATUS, dmac);
			}
			return MCI_OK;
		}

		mci_udelay(mci_poll_us);
	} while (--loop != 0u);

	rawints = mci_rd(hc, MCI_RAW_INTS);
	dmac = idmac ? mci_rd(hc, MCI_DMAC_STATUS) : 0u;
	mci_snapshot(hc, cmd, rawints, dmac);
	hc->st.data_ev = rawints;

	LOG_ANCHOR("MCI: CMD%u data wait timed out rawints=%08x dmac=%08x "
		   "status=%08x\n", (unsigned)cmd->index, rawints, dmac,
		   mci_rd(hc, MCI_STATUS));
	mci_wr(hc, MCI_INT_MASK, 0u);
	mci_wr(hc, MCI_RAW_INTS, rawints & 0xffffeu);
	if (idmac) {
		mci_wr(hc, MCI_DMAC_INT_ENA, 0u);
		mci_wr(hc, MCI_DMAC_STATUS, dmac);
	}

	return MCI_ERR_TIMEOUT;
}

/* firefly_mci_cmd_done(): read the response out of the FIFO's shadow. */
static void mci_read_response(struct mci_host *hc, struct mci_cmd *cmd)
{
	if (cmd->resp_class == MCI_RESP_NONE) {
		return;
	}

	if (cmd->resp_class == MCI_RESP_LONG_CRC) {
		/* the controller presents a 136-bit response most significant
		 * word first */
		cmd->response[3] = mci_rd(hc, MCI_RESP0);
		cmd->response[2] = mci_rd(hc, MCI_RESP1);
		cmd->response[1] = mci_rd(hc, MCI_RESP2);
		cmd->response[0] = mci_rd(hc, MCI_RESP3);
	} else {
		cmd->response[0] = mci_rd(hc, MCI_RESP0);
		cmd->response[1] = 0u;
		cmd->response[2] = 0u;
		cmd->response[3] = 0u;
	}
}

/* firefly_mci_data_sg_write_2_fifo(): the FIFO side of a COMMON transfer. For
 * a write the words go in before the command is issued (the controller drains
 * them as the card clocks); Linux sets DAT_WR in the command register first,
 * which is what tells the FIFO which direction it is filling. */
static void mci_fill_fifo(struct mci_host *hc, const struct mci_data *d)
{
	const uint8_t *src = d->buf;
	uint32_t words = (d->len + 3u) / 4u;
	uint32_t i;

	for (i = 0u; i < words; i++) {
		uint32_t v = 0u;
		uint32_t n = d->len - (i * 4u);
		uint32_t k;

		if (n > 4u) {
			n = 4u;
		}
		for (k = 0u; k < n; k++) {
			v |= (uint32_t)src[(i * 4u) + k] << (8u * k);
		}
		mci_wr(hc, MCI_DATA, v);
	}
}

static void mci_drain_fifo(struct mci_host *hc, const struct mci_data *d)
{
	uint8_t *dst = d->buf;
	uint32_t words = (d->len + 3u) / 4u;
	uint32_t i;

	for (i = 0u; i < words; i++) {
		uint32_t v = mci_rd(hc, MCI_DATA);
		uint32_t n = d->len - (i * 4u);
		uint32_t k;

		if (n > 4u) {
			n = 4u;
		}
		for (k = 0u; k < n; k++) {
			dst[(i * 4u) + k] = (uint8_t)(v >> (8u * k));
		}
	}
}

/*
 * firefly_mci_start_data(): the submission order, which is the single most
 * expensive thing to get wrong on this controller.
 *
 *   1. clear RAW_INTS (0xffffe - every bit this driver owns)
 *   2. FIFO+DMA soft reset, wait for it to self-clear
 *   3. USE_INTERNAL_DMAC per the transfer shape (BLOCK: set, COMMON: clear)
 *   4. clear INT_ENABLE (the reset above left it in an unknown state)
 *   5. build the command word / descriptor list / pre-fill the FIFO
 *   6. arm INT_MASK (+ DMAC_INT_ENA, BUS_MODE.DE, DESC_LIST_ADDR on BLOCK)
 *   7. BLKSIZ / BYTCNT
 *   8. raise INT_ENABLE
 *   9. CMDARG, barrier, CMD
 */
static int mci_start_data(struct mci_host *hc, struct mci_cmd *cmd,
			  enum mci_adtc type)
{
	const struct mci_data *d = cmd->data;
	uint32_t rawcmd;
	uint32_t loop = mci_polls_for(MCI_RESET_BUDGET_US);

	if ((mci_trial_flags & MCI_TRIAL_NO_FD0) == 0u) {
		mci_wr(hc, MCI_VENDOR_FD0,
		       ((mci_trial_flags & MCI_TRIAL_FD0_ONES) != 0u) ?
		       0xffffffffu : 0u);
	}
	mci_wr(hc, MCI_RAW_INTS, 0xffffeu);

	mci_set_bits(hc, MCI_CNTRL, MCI_CNTRL_FIFO_RESET | MCI_CNTRL_DMA_RESET);
	while ((mci_rd(hc, MCI_CNTRL) &
		(MCI_CNTRL_FIFO_RESET | MCI_CNTRL_DMA_RESET)) != 0u) {
		if (--loop == 0u) {
			LOG_ANCHOR("MCI: data reset did not self-clear (CMD%u)\n",
				   (unsigned)cmd->index);
			mci_snapshot(hc, cmd, 0u, 0u);
			return MCI_ERR_TIMEOUT;
		}
		mci_udelay(1u);
	}

	if ((mci_trial_flags & MCI_TRIAL_BUSMODE) != 0u) {
		mci_wr(hc, MCI_BUS_MODE, 0x200u);
	}

	if (type == MCI_ADTC_BLOCK) {
		mci_set_bits(hc, MCI_CNTRL, MCI_CNTRL_USE_INTERNAL_DMAC);
	} else if ((mci_trial_flags & MCI_TRIAL_DMAC_ON) != 0u) {
		mci_set_bits(hc, MCI_CNTRL, MCI_CNTRL_USE_INTERNAL_DMAC);
	} else {
		mci_clr_bits(hc, MCI_CNTRL, MCI_CNTRL_USE_INTERNAL_DMAC);
	}
	ARCH_DSB_ST();

	mci_clr_bits(hc, MCI_CNTRL, MCI_CNTRL_INT_ENABLE);

	rawcmd = mci_make_raw_cmd(hc, cmd);

	if (type == MCI_ADTC_BLOCK) {
		uint32_t descs = mci_build_descs(d);

		if (descs == 0u) {
			LOG_ANCHOR("MCI: CMD%u descriptor chain empty (len=%u)\n",
				   (unsigned)cmd->index, (unsigned)d->len);
			return MCI_ERR_ARG;
		}
		/* descriptors live in the non-cacheable window: this barrier
		 * is the whole of the "make them visible" step */
		ARCH_DSB_ST();
		hc->st.idmac_stages++;
	} else if (d->write) {
		/* firefly_mci_data_sg_write_2_fifo's first statement: set the
		 * DAT_WR bit so the FIFO knows it is being filled, then push the
		 * words in before the command goes out. A read takes the other
		 * direction and is drained after DTO. */
		hc->st.fifo_stages++;
		mci_wr(hc, MCI_CMD, MCI_CMD_DAT_WR);
		mci_fill_fifo(hc, d);
	} else {
		hc->st.fifo_stages++;
	}

	mci_set_bits(hc, MCI_INT_MASK, MCI_CMD_INTS_MASK | MCI_DATA_INTS_MASK);

	if (type == MCI_ADTC_BLOCK) {
		uintptr_t pool = (uintptr_t)mci_desc_pool;

		mci_set_bits(hc, MCI_DMAC_INT_ENA, MCI_DMAC_INTS_MASK);
		/* Enable the IDMAC */
		mci_set_bits(hc, MCI_BUS_MODE, MCI_BUS_MODE_DE);
		mci_wr(hc, MCI_DESC_LIST_ADDRL, (uint32_t)pool);
		mci_wr(hc, MCI_DESC_LIST_ADDRH, (uint32_t)(((uint64_t)pool) >> 32));
	}

	mci_wr(hc, MCI_BLKSIZ, d->blksz);
	mci_wr(hc, MCI_BYTCNT, d->blkcnt * d->blksz);

	/* experiment knobs: applied on top of the port's own programming, and
	 * only when armed, so a normal run is bit-for-bit the port */
	if (mci_cntrl_extra != 0u) {
		mci_set_bits(hc, MCI_CNTRL, mci_cntrl_extra);
	}
	if (mci_fifoth_extra != 0u) {
		mci_wr(hc, MCI_FIFOTH,
		       0x20070100u | mci_fifoth_extra);
	}

	mci_set_bits(hc, MCI_CNTRL, MCI_CNTRL_INT_ENABLE);
	mci_wr(hc, MCI_CMDARG, cmd->arg);
	ARCH_DSB_ST();

	MCI_TRACE("MCI CMD host=%u c=%02u arg=%08x raw=%08x d=%c blksz=%u blkcnt=%u "
		  "bytcnt=%08x clkdiv=%08x ctype=%08x tmout=%08x imask=%08x "
		  "dma=%d adtc=%d\n",
		  (unsigned)hc->cfg.index, (unsigned)cmd->index, (unsigned)cmd->arg, rawcmd,
		  d->write ? 'w' : 'r', (unsigned)d->blksz, (unsigned)d->blkcnt,
		  mci_rd(hc, MCI_BYTCNT), mci_rd(hc, MCI_CLKDIV),
		  mci_rd(hc, MCI_CTYPE), mci_rd(hc, MCI_TMOUT),
		  mci_rd(hc, MCI_INT_MASK), 1, (int)type);

	/* the CPU's view of the buffer, in the direction the device needs */
	if (d->write) {
		cache_dcache_flush((uintptr_t)d->buf, (unsigned long)d->len);
	} else {
		cache_dcache_invalidate((uintptr_t)d->buf, (unsigned long)d->len);
	}
	ARCH_DSB_ST();

	/* Poison the response registers first: if the card never answers, RESP0
	 * must read back the poison rather than the previous command's R5.
	 * Without this the trace cannot tell "the card was silent" from "the
	 * controller latched a response the driver never looked at". */
	mci_wr(hc, MCI_RESP0, 0xdeadbeefu);
	mci_wr(hc, MCI_RESP1, 0xdeadbeefu);
	mci_wr(hc, MCI_RESP2, 0xdeadbeefu);
	mci_wr(hc, MCI_RESP3, 0xdeadbeefu);

	mci_wr(hc, MCI_CMD, rawcmd);

	{
		int ret = mci_wait_cmd(hc, cmd);

		if (ret == MCI_OK) {
			ret = mci_wait_data(hc, cmd, type == MCI_ADTC_BLOCK);
		}

		/* INT_MASK and the engine's enables go back to nothing: a
		 * completion left armed would make the next command's wait
		 * read the previous one's event. */
		mci_clr_bits(hc, MCI_INT_MASK, MCI_CMD_INTS_MASK | MCI_DATA_INTS_MASK);
		if (type == MCI_ADTC_BLOCK) {
			mci_clr_bits(hc, MCI_DMAC_INT_ENA, MCI_DMAC_INTS_MASK);
			mci_clr_bits(hc, MCI_BUS_MODE, MCI_BUS_MODE_DE);
		}

		if (ret != MCI_OK) {
			hc->st.errors++;
			/* The event word is the two halves ORed, which is what
			 * the single read in Linux's ISR sees when both arrive
			 * between handler entries: CMD from the command half,
			 * DTO/RXDR/DRTO from the data half. A trace whose ev
			 * lost the command bits would read as "the card never
			 * answered" when it answered and the phase after it
			 * stalled. */
			MCI_TRACE("MCI DONE c=%02u ev=%08x cmd_ev=%08x "
				  "data_ev=%08x err=%d resp0=%08x\n",
				  (unsigned)cmd->index,
				  hc->st.cmd_ev | hc->st.data_ev,
				  hc->st.cmd_ev, hc->st.data_ev, ret,
				  hc->st.resp0);
			return ret;
		}

		mci_read_response(hc, cmd);

		if (!d->write) {
			if (type == MCI_ADTC_BLOCK) {
				cache_dcache_invalidate((uintptr_t)d->buf,
							(unsigned long)d->len);
			} else {
				mci_drain_fifo(hc, d);
			}
		}

		MCI_TRACE("MCI DONE c=%02u ev=%08x cmd_ev=%08x data_ev=%08x "
			  "err=0 resp0=%08x\n",
			  (unsigned)cmd->index,
			  hc->st.cmd_ev | hc->st.data_ev,
			  hc->st.cmd_ev, hc->st.data_ev,
			  (cmd->resp_class != MCI_RESP_NONE) ? cmd->response[0] : 0u);
	}

	return MCI_OK;
}

/* firefly_mci_start_command(): a command with no data stage. */
static int mci_start_command(struct mci_host *hc, struct mci_cmd *cmd)
{
	uint32_t rawcmd = mci_make_raw_cmd(hc, cmd);
	int ret;

	mci_wr(hc, MCI_VENDOR_FD0, 0u);
	mci_wr(hc, MCI_RAW_INTS, 0xffffeu);
	mci_set_bits(hc, MCI_INT_MASK, MCI_CMD_INTS_MASK);
	mci_wr(hc, MCI_CMDARG, cmd->arg);
	ARCH_DSB_ST();

	MCI_TRACE("MCI CMD host=%u c=%02u arg=%08x raw=%08x d=- clkdiv=%08x ctype=%08x "
		  "tmout=%08x imask=%08x\n",
		  (unsigned)hc->cfg.index, (unsigned)cmd->index, (unsigned)cmd->arg, rawcmd,
		  mci_rd(hc, MCI_CLKDIV), mci_rd(hc, MCI_CTYPE),
		  mci_rd(hc, MCI_TMOUT), mci_rd(hc, MCI_INT_MASK));

	/* Poison the response registers first: if the card never answers, RESP0
	 * must read back the poison rather than the previous command's R5.
	 * Without this the trace cannot tell "the card was silent" from "the
	 * controller latched a response the driver never looked at". */
	mci_wr(hc, MCI_RESP0, 0xdeadbeefu);
	mci_wr(hc, MCI_RESP1, 0xdeadbeefu);
	mci_wr(hc, MCI_RESP2, 0xdeadbeefu);
	mci_wr(hc, MCI_RESP3, 0xdeadbeefu);

	mci_wr(hc, MCI_CMD, rawcmd);

	ret = mci_wait_cmd(hc, cmd);
	mci_clr_bits(hc, MCI_INT_MASK, MCI_CMD_INTS_MASK);

	if (ret != MCI_OK) {
		hc->st.errors++;
		MCI_TRACE("MCI DONE c=%02u ev=%08x err=%d resp0=%08x\n",
			  (unsigned)cmd->index, hc->st.rawints, ret, hc->st.resp0);
		return ret;
	}

	mci_read_response(hc, cmd);

	MCI_TRACE("MCI DONE c=%02u ev=%08x err=0 resp0=%08x\n",
		  (unsigned)cmd->index, hc->st.rawints,
		  (cmd->resp_class != MCI_RESP_NONE) ? cmd->response[0] : 0u);

	return MCI_OK;
}

/* Should this data command go through the IDMAC? Linux's rule: at least one
 * full block, DMA enabled, and one of the block/IO-extended opcodes. */
static bool mci_wants_idmac(const struct mci_cmd *cmd)
{
	if (cmd->data == NULL) {
		return false;
	}
	if (cmd->data->len < 512u) {
		return false;
	}

	switch (cmd->index) {
	case 17u:	/* READ_SINGLE_BLOCK */
	case 18u:	/* READ_MULTIPLE_BLOCK */
	case 24u:	/* WRITE_BLOCK */
	case 25u:	/* WRITE_MULTIPLE_BLOCK */
	case 53u:	/* SD_IO_RW_EXTENDED */
		return true;
	default:
		return false;
	}
}

int mci_transfer(struct mci_host *hc, struct mci_cmd *cmd)
{
	int ret;

	if ((hc == NULL) || (cmd == NULL)) {
		return MCI_ERR_ARG;
	}
	if (!hc->ready) {
		return MCI_ERR_NOT_READY;
	}

	/*
	 * The entry gate, from firefly_mci_request() in the Linux driver: no
	 * command is submitted while the card still reports busy or the previous
	 * command has not cleared its START bit.  Linux waits for both before
	 * *every* request, and the port had neither, so a failed request left
	 * the controller dirty for the next one - which is what turns one early
	 * failure into a run of CMD53s that never even get a response.
	 */
	if ((mci_trial_flags & MCI_TRIAL_NO_GATE) == 0u) {
		mci_wait_idle(hc);
	} else {
		hc->st.gate_skipped++;
	}

	hc->st.transfers++;
	if (cmd->index == 52u) {
		hc->st.cmd52++;
	} else if (cmd->index == 53u) {
		hc->st.cmd53++;
	}

	if (cmd->data != NULL) {
		hc->st.data_stages++;
		ret = mci_start_data(hc, cmd,
				     mci_wants_idmac(cmd) ? MCI_ADTC_BLOCK
							  : MCI_ADTC_COMMON);
	} else {
		ret = mci_start_command(hc, cmd);
	}

	hc->st.last_err = ret;

	return ret;
}

/* --- lifecycle ------------------------------------------------------------ */

int mci_init(struct mci_host *hc, const struct mci_cfg *cfg)
{
	if ((hc == NULL) || (cfg == NULL) || (cfg->base == 0u)) {
		return MCI_ERR_ARG;
	}

	(void)memset(hc, 0, sizeof(*hc));
	hc->cfg = *cfg;

	mci_init_adma_table(hc);
	mci_init_hw(hc);

	/* power up and wait for the ramp before the stack's first command */
	mci_power(hc, true);
	mci_udelay(10000u);

	hc->ready = true;

	LOG_ANCHOR("MCI: init base=0x%lx pid=%d cntrl=%08x fifoth=%08x thrctl=%08x "
		   "pren=%08x uhs_ext=%08x clkdiv=%08x cclk_rdy=%08x\n",
		   (unsigned long)cfg->base, cfg->intid,
		   mci_rd(hc, MCI_CNTRL), mci_rd(hc, MCI_FIFOTH),
		   mci_rd(hc, MCI_CARD_THRCTL), mci_rd(hc, MCI_PWREN),
		   mci_rd(hc, MCI_UHS_REG_EXT), mci_rd(hc, MCI_CLKDIV),
		   mci_rd(hc, MCI_CCLK_RDY));

	return MCI_OK;
}

void mci_deinit(struct mci_host *hc)
{
	if (hc == NULL) {
		return;
	}

	mci_wr(hc, MCI_PWREN, 0u);
	mci_clr_bits(hc, MCI_CLKENA, MCI_CLKENA_CCLK_ENABLE);
	mci_clr_bits(hc, MCI_UHS_REG_EXT, MCI_UHS_REG_EXT_CLK_ENA);
	mci_clr_bits(hc, MCI_UHS_REG, MCI_UHS_REG_VOLT);
	mci_wr(hc, MCI_INT_MASK, 0u);
	mci_wr(hc, MCI_DMAC_INT_ENA, 0u);
	hc->ready = false;
}

/*
 * firefly_mci_hw_reset()'s card-facing half: a 5 us low pulse on the
 * controller's card-reset line, then 300 us for the card to come back. Linux
 * runs this on every rescan (mmc_hw_reset_for_init) after power-up and before
 * the first command, and on this board it is not optional: without the pulse
 * the part on mmc1 does not answer CMD5 at all - it times out at any clock,
 * which is exactly what this port measured before the pulse was carried over.
 */
void mci_reset_card(struct mci_host *hc)
{
	uint32_t loop = mci_polls_for(MCI_RESET_BUDGET_US);

	mci_set_bits(hc, MCI_CNTRL, MCI_CNTRL_FIFO_RESET | MCI_CNTRL_DMA_RESET);
	while ((mci_rd(hc, MCI_CNTRL) &
		(MCI_CNTRL_FIFO_RESET | MCI_CNTRL_DMA_RESET)) != 0u) {
		if (--loop == 0u) {
			LOG_ANCHOR("MCI: reset did not self-clear before card pulse\n");
			break;
		}
		mci_udelay(1u);
	}

	mci_clr_bits(hc, MCI_CARD_RESET, MCI_CARD_RESET_ENABLE);
	mci_udelay(5u);
	mci_set_bits(hc, MCI_CARD_RESET, MCI_CARD_RESET_ENABLE);
	mci_udelay(300u);

	MCI_TRACE("MCI CARD_RESET pulse done card_reset=%08x\n",
		  mci_rd(hc, MCI_CARD_RESET));
}

int mci_reset(struct mci_host *hc)
{
	if ((hc == NULL) || !hc->ready) {
		return MCI_ERR_NOT_READY;
	}

	mci_adma_reset(hc);
	mci_reset_hw(hc);
	mci_restart_clk(hc);

	return MCI_OK;
}

void mci_state_get(const struct mci_host *hc, struct mci_state *out)
{
	if ((hc == NULL) || (out == NULL)) {
		return;
	}

	*out = hc->st;
}

void mci_set_poll_us(uint32_t us)
{
	if (us != 0u) {
		mci_poll_us = us;
	}
}

void mci_set_poll_probe(unsigned int samples)
{
	mci_probe_samples = (uint32_t)samples;
}

void mci_set_cntrl_extra(uint32_t set_bits, uint32_t extra_fifoth_bits)
{
	mci_cntrl_extra = set_bits;
	mci_fifoth_extra = extra_fifoth_bits;
}

void mci_set_trial(uint32_t flags)
{
	mci_trial_flags = flags;
}

uint32_t mci_reg_read(const struct mci_host *hc, uint32_t off)
{
	if (hc == NULL) {
		return 0u;
	}

	return *(volatile uint32_t *)(hc->cfg.base + off);
}

void mci_reg_write(struct mci_host *hc, uint32_t off, uint32_t val)
{
	if (hc == NULL) {
		return;
	}

	*(volatile uint32_t *)(hc->cfg.base + off) = val;
}
