/*
 * @file   dwc_mshc.c
 * @brief  The eMMC controller driver for this board: CRU clock/reset, the pin
 *         group, SDHCI command issue, the SDMA data path, polling.
 *
 * The register sequences follow the two drivers that run this same controller
 * and this same card on this board - u-boot's rockchip_sdhci.c (whose eMMC path
 * u-boot's own OTA writes exercise at high speed) and the kernel's
 * sdhci-of-dwcmshc.c - with this SoC's CRU and GRF coordinates. Three of their
 * conventions are load-bearing and are called out where they live below: the
 * clock must be re-programmed with the card clock stopped and the DLL bypassed
 * below 100 MHz, the command index check does not apply to 136-bit responses,
 * and an SDMA transfer must be continued at the next boundary rather than
 * assumed to have reached the end of its buffer.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 *
 * Register and field values derived from the board lab's standalone eMMC
 * controller driver (BSD-3-Clause, commit 45d03100 of that tree); the
 * sequences here are this project's rewrite of them against its own mmio,
 * cache, timer and log services.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "log.h"

#include "arch_asm.h"
#include "cache.h"
#include "generic_timer.h"
#include "mmio.h"

#include "dwc_mshc.h"
#include "dwc_mshc_dma.h"
#include "dwc_mshc_regs.h"

/*
 * Polling budgets, in wall-clock microseconds: a coarser step notices a
 * completed command later, a finer one spends more CPU in the wait loop, and
 * neither changes the budget. The step is this driver's own; the budgets are
 * the older BSP driver's (1 s for a command, 3 s for a data stage, 50 ms for a
 * self-clearing reset step).
 */
#define DWC_MSHC_POLL_US		100U
#define DWC_MSHC_CMD_BUDGET_US		1000000U
#define DWC_MSHC_DATA_BUDGET_US		3000000U
#define DWC_MSHC_RESET_BUDGET_US	50000U

/* How long the card is given after a clock change before the next command. */
#define DWC_MSHC_CLOCK_SETTLE_US	1000U

/* The identification clock's target; the 375 kHz source serves it without the
 * controller's divider. */
#define DWC_MSHC_INIT_CLOCK_HZ		400000U

/* The rate at and above which the controller's high-speed bit and the UHS
 * selector are programmed: the card's high-speed timing switch happens at the
 * 50 MHz ask, and everything the stack asks for before it stays below this. */
#define DWC_MSHC_HIGH_SPEED_MIN_HZ	25000000U

/* The board's controller table. One entry: the soldered eMMC. */
static const struct dwc_mshc_cfg dwc_mshc_board[] = {
	{ .base = DWC_MSHC_EMMC_BASE, .intid = DWC_MSHC_EMMC_INTID },
};

static struct dwc_mshc_census dwc_mshc_census_state;

const struct dwc_mshc_cfg *dwc_mshc_board_cfg(unsigned int index)
{
	if (index >= (unsigned)(sizeof(dwc_mshc_board) / sizeof(dwc_mshc_board[0]))) {
		return NULL;
	}

	return &dwc_mshc_board[index];
}

/* --- small helpers -------------------------------------------------------- */

/* Microsecond busy-wait on the architectural counter, in the same shape the
 * other board drivers use: a requested delay always costs at least one tick. */
static void dwc_mshc_udelay(uint32_t usec)
{
	uint64_t freq = (uint64_t)gtimer_freq_get();
	uint64_t ticks = (freq * (uint64_t)usec) / 1000000ULL;
	uint64_t start;

	if (ticks == 0ULL) {
		ticks = 1ULL;
	}

	start = gtimer_count_get();
	while ((gtimer_count_get() - start) < ticks) {
		/* spin */
	}
}

static uint32_t dwc_mshc_polls_for(uint32_t budget_us)
{
	uint32_t n = budget_us / DWC_MSHC_POLL_US;

	return (n == 0U) ? 1U : n;
}

/* Rockchip CRU/GRF registers carry a hiword write-enable: one masked write
 * changes the named field and leaves its neighbours alone. */
static void dwc_mshc_cru_write(uintptr_t reg, uint32_t mask, uint32_t val)
{
	mmio_wr32(reg, ((mask & 0xffffu) << 16) | (val & mask));
}

static void dwc_mshc_int_status_clear(struct dwc_mshc *hc)
{
	uintptr_t base = hc->cfg.base;

	mmio_wr16(base + DWC_MSHC_NORMAL_INT_STAT, (uint16_t)DWC_MSHC_INT_ALL);
	mmio_wr16(base + DWC_MSHC_ERROR_INT_STAT, (uint16_t)DWC_MSHC_EINT_ALL);
}

/* --- the eMMC pin group --------------------------------------------------- */

/*
 * The pin group the kernel's DT describes for this socket: the eight data
 * lines, the command line and the clock at alternate function 1 with pull-ups
 * and drive strength level 2. Two pins of that group are deliberately not
 * written:
 *   - C6 (data strobe) stays at its reset state - the DT only claims it for
 *     the HS400 strobe this round does not use;
 *   - C7 (card reset output) is left alone entirely: on this board that pin is
 *     shared with the flash controller's D2 line and the card's reset is not
 *     wired to it, which is why the kernel's device tree for this board drops
 *     the emmc_rstnout group.
 * The boot firmware has already configured these pins for its own eMMC use, so
 * these writes are idempotent on a normal boot chain; they are here so the
 * driver does not depend on that.
 */
static void dwc_mshc_pinctrl_init(void)
{
	uintptr_t grf = DWC_MSHC_GRF_BASE;
	uint32_t pull_b = 0x55U << 8;		/* B4..B7 pull-up */
	uint32_t pull_c = 0x555U;		/* C0..C5 pull-up */

	/* iomux: four bits per pin, B4..B7 / C0..C3 / C4..C6 */
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1B_IOMUX_H, (0xffffu << 16) | 0x1111U);
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1C_IOMUX_L, (0xffffu << 16) | 0x1111U);
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1C_IOMUX_H, (0x0fffu << 16) | 0x0111U);

	/* pull: two bits per pin */
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1B_PULL, (0xff00u << 16) | pull_b);
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1C_PULL, (0x0fffu << 16) | pull_c);

	/* drive strength: eight bits per pin, two pins per register, level 2 */
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1B_DS_2, 0x3f3f0707U);
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1B_DS_3, 0x3f3f0707U);
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1C_DS_0, 0x3f3f0707U);
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1C_DS_1, 0x3f3f0707U);
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1C_DS_2, 0x3f3f0707U);
	/* C6 only: the C7 half of this register belongs to the pin this driver
	 * leaves alone. */
	mmio_wr32(grf + DWC_MSHC_GRF_GPIO1C_DS_3, 0x00000007U | (0x3fu << 16));
}

/* --- CRU: gates, reset release, clock source ------------------------------ */

static void dwc_mshc_cru_clock_init(struct dwc_mshc *hc)
{
	/* Gate bits read 1 when the clock is off, so enabling writes 0; the soft
	 * resets are released the same way. The clock starts on the 24 MHz source
	 * for identification and the bus clock on its 200 MHz slot, which is what
	 * the boot firmware leaves behind. */
	dwc_mshc_cru_write(DWC_MSHC_CRU_BASE + DWC_MSHC_CRU_CLKGATE_CON9,
			   DWC_MSHC_CRU_EMMC_GATE_ALL, 0U);
	dwc_mshc_cru_write(DWC_MSHC_CRU_BASE + DWC_MSHC_CRU_SOFTRST_CON7,
			   DWC_MSHC_CRU_EMMC_SOFTRST_ALL, 0U);
	dwc_mshc_cru_write(DWC_MSHC_CRU_BASE + DWC_MSHC_CRU_CLKSEL_CON28,
			   DWC_MSHC_CRU_CCLK_MUX_MASK | DWC_MSHC_CRU_BCLK_MUX_MASK,
			   ((uint32_t)DWC_MSHC_CRU_CCLK_SEL_24M << DWC_MSHC_CRU_CCLK_MUX_SHIFT) |
				   ((uint32_t)DWC_MSHC_CRU_BCLK_SEL_200M << DWC_MSHC_CRU_BCLK_MUX_SHIFT));

	hc->src_hz = 24000000U;
	hc->cclk_sel = DWC_MSHC_CRU_CCLK_SEL_24M;
	hc->achieved_hz = 24000000U;
}

/* Pick the CRU source and the controller divider for a target card clock.
 *
 * The divider divides by 2 * FREQ_SEL, and FREQ_SEL 0 bypasses it, so the
 * achievable rates from a source are that source and its halves. The divider is
 * chosen to land at or below the requested rate (a card's clock is a ceiling,
 * not a target): the older BSP driver divided down and could exceed a request
 * that fell between two achievable rates.
 *
 * Returns 0 for a target this round cannot serve - anything above the 50 MHz
 * source. The 200 MHz clock chain (the DLL and the tuning that goes with it) is
 * the next round's subject, and refusing here is what keeps a future capability
 * claim from silently running at the wrong rate. */
static uint32_t dwc_mshc_select_clock(struct dwc_mshc *hc, uint32_t target,
				      uint32_t *freq_sel)
{
	uint32_t sel;
	uint32_t src;
	uint32_t fsel;

	if (target <= 750000U) {
		sel = DWC_MSHC_CRU_CCLK_SEL_375K;
		src = 375000U;
	} else if (target <= 24000000U) {
		sel = DWC_MSHC_CRU_CCLK_SEL_24M;
		src = 24000000U;
	} else if (target <= 52000000U) {
		sel = DWC_MSHC_CRU_CCLK_SEL_50M;
		src = 50000000U;
	} else {
		return 0U;
	}

	if (src <= target) {
		fsel = 0U;
	} else {
		fsel = (src + (2U * target) - 1U) / (2U * target);
	}

	dwc_mshc_cru_write(DWC_MSHC_CRU_BASE + DWC_MSHC_CRU_CLKSEL_CON28,
			   DWC_MSHC_CRU_CCLK_MUX_MASK,
			   sel << DWC_MSHC_CRU_CCLK_MUX_SHIFT);

	hc->src_hz = src;
	hc->cclk_sel = sel;
	*freq_sel = fsel;

	return (fsel == 0U) ? src : (src / (2U * fsel));
}

/*
 * The DLL below 100 MHz: it stays in bypass, and the phase registers are put
 * back to their reset arrangement. The sequence is u-boot's and the kernel's,
 * register for register; the 100 MHz-and-above branch (DLL lock, then the
 * tap programming the lock value feeds) is not carried this round.
 */
static void dwc_mshc_dll_bypass(uintptr_t base)
{
	/* disable the DLL and reset the clock phase */
	mmio_wr32(base + DWC_MSHC_DLL_CTRL, 0U);
	/* the command conflict check stays off below 100 MHz, as in both
	 * reference drivers */
	mmio_wr8(base + DWC_MSHC_HOST_CTRL3, 0U);
	mmio_wr32(base + DWC_MSHC_DLL_CTRL, DWC_MSHC_DLL_BYPASS | DWC_MSHC_DLL_START);
	mmio_wr32(base + DWC_MSHC_DLL_RXCLK, DWC_MSHC_DLL_RXCLK_ORI_GATE);
	mmio_wr32(base + DWC_MSHC_DLL_TXCLK, 0U);
	mmio_wr32(base + DWC_MSHC_DLL_CMDOUT, 0U);
	mmio_wr32(base + DWC_MSHC_DLL_STRBIN,
		  DWC_MSHC_DLL_STRBIN_DELAY_EN | DWC_MSHC_DLL_STRBIN_DELAY_SEL |
			  ((uint32_t)DWC_MSHC_DLL_STRBIN_DELAY_16 << DWC_MSHC_DLL_STRBIN_DELAY_SHIFT));
}

/* Wait for the command and data paths to go idle before touching the clock. */
static int dwc_mshc_wait_inhibit_clear(uintptr_t base)
{
	uint32_t loop = dwc_mshc_polls_for(DWC_MSHC_CMD_BUDGET_US);

	while ((mmio_rd32(base + DWC_MSHC_PRESENT_STATE) &
		(DWC_MSHC_PS_CMD_INHIBIT | DWC_MSHC_PS_DAT_INHIBIT)) != 0U) {
		if (--loop == 0U) {
			LOG_ERROR("dwc_mshc: cmd/dat inhibit stuck (pstate=0x%x)\n",
				  mmio_rd32(base + DWC_MSHC_PRESENT_STATE));
			return DWC_MSHC_ERR_TIMEOUT;
		}
		dwc_mshc_udelay(DWC_MSHC_POLL_US);
	}

	return DWC_MSHC_OK;
}

/* --- bring-up ------------------------------------------------------------- */

static int dwc_mshc_soft_reset(struct dwc_mshc *hc, uint8_t bits)
{
	uintptr_t base = hc->cfg.base;
	uint32_t loop = dwc_mshc_polls_for(DWC_MSHC_RESET_BUDGET_US);

	mmio_wr8(base + DWC_MSHC_SW_RESET, bits);
	while ((mmio_rd8(base + DWC_MSHC_SW_RESET) & bits) != 0U) {
		if (--loop == 0U) {
			LOG_ERROR("dwc_mshc: soft reset 0x%x did not self-clear\n", bits);
			return DWC_MSHC_ERR_TIMEOUT;
		}
		dwc_mshc_udelay(DWC_MSHC_POLL_US);
	}

	return DWC_MSHC_OK;
}

/* The controller state a soft reset does not restore, in the order the kernel's
 * reset hook and the reference drivers program it. */
static void dwc_mshc_post_reset(struct dwc_mshc *hc)
{
	uintptr_t base = hc->cfg.base;

	/* the internal clock has to be enabled again after a reset, or the
	 * controller never reaches its identification clock */
	mmio_wr32(base + DWC_MSHC_MISC_CON, DWC_MSHC_MISC_INTCLK_EN);
	/* no command-conflict check on this board */
	mmio_wr8(base + DWC_MSHC_HOST_CTRL3, 0U);
	/* SDMA, one data line until the card's bus width is switched */
	mmio_wr8(base + DWC_MSHC_HOST_CTRL1, DWC_MSHC_HC1_DMA_SEL_SDMA);
	/* this socket's IO rail is fixed at 1.8 V, so the signalling level is
	 * declared once here and never changes */
	mmio_wr16(base + DWC_MSHC_HOST_CTRL2, DWC_MSHC_HC2_SIGNALING_1_8V);
	/* the maximum data timeout counter */
	mmio_wr8(base + DWC_MSHC_TIMEOUT_CTRL, 0x0eU);
	/* 3.3 V card supply, bus power on */
	mmio_wr8(base + DWC_MSHC_POWER_CTRL,
		 DWC_MSHC_PWR_BUS_VOLTAGE_3_3V | DWC_MSHC_PWR_BUS_POWER);
	/* this socket is an eMMC: the vendor control's CARD_IS_EMMC bit. u-boot
	 * and the kernel set it only on their HS400 paths; both the older BSP
	 * driver for this board and the standalone port that ran this card at
	 * high speed set it from bring-up, and the bit's remaining fields (the
	 * card reset output) are left alone because this board does not wire the
	 * card's reset line. */
	mmio_wr16(base + DWC_MSHC_EMMC_CTRL, 1U);

	/* no interrupt signalling: this port polls */
	dwc_mshc_int_status_clear(hc);
	mmio_wr16(base + DWC_MSHC_NORMAL_INT_EN, (uint16_t)DWC_MSHC_INT_ALL);
	mmio_wr16(base + DWC_MSHC_ERROR_INT_EN, (uint16_t)DWC_MSHC_EINT_ALL);
	mmio_wr16(base + DWC_MSHC_NORMAL_INT_EN_SIG, 0U);
	mmio_wr16(base + DWC_MSHC_ERROR_INT_EN_SIG, 0U);
}

int dwc_mshc_init(struct dwc_mshc *hc, const struct dwc_mshc_cfg *cfg)
{
	int ret;
	uint32_t achieved;

	if ((hc == NULL) || (cfg == NULL)) {
		return DWC_MSHC_ERR_ARG;
	}

	(void)memset(hc, 0, sizeof(*hc));
	hc->cfg = *cfg;

	dwc_mshc_cru_clock_init(hc);
	dwc_mshc_pinctrl_init();

	ret = dwc_mshc_soft_reset(hc, (uint8_t)DWC_MSHC_SW_RESET_ALL);
	if (ret != DWC_MSHC_OK) {
		return ret;
	}

	dwc_mshc_post_reset(hc);

	/* the card is powered by the rail the reset released; give it its
	 * ramp-up before the first command */
	dwc_mshc_udelay(10000U);

	achieved = dwc_mshc_set_clock(hc, DWC_MSHC_INIT_CLOCK_HZ);
	if (achieved == 0U) {
		LOG_ERROR("dwc_mshc: identification clock refused\n");
		return DWC_MSHC_ERR_NOT_READY;
	}

	/* the first command after the clock comes up is lost if it is issued
	 * immediately (measured on this board by the older BSP driver), so the
	 * clock is given its settle time here rather than in every transfer */
	dwc_mshc_udelay(DWC_MSHC_CLOCK_SETTLE_US * 2U);

	hc->ready = true;

	return DWC_MSHC_OK;
}

void dwc_mshc_deinit(struct dwc_mshc *hc)
{
	uintptr_t base;

	if (hc == NULL) {
		return;
	}

	base = hc->cfg.base;
	mmio_wr16(base + DWC_MSHC_CLOCK_CTRL, 0U);
	mmio_wr32(base + DWC_MSHC_DLL_CTRL, 0U);
	hc->ready = false;
}

int dwc_mshc_reset(struct dwc_mshc *hc)
{
	int ret;

	ret = dwc_mshc_soft_reset(hc, (uint8_t)DWC_MSHC_SW_RESET_ALL);
	if (ret != DWC_MSHC_OK) {
		return ret;
	}

	dwc_mshc_post_reset(hc);

	return DWC_MSHC_OK;
}

/* --- clock ---------------------------------------------------------------- */

uint32_t dwc_mshc_set_clock(struct dwc_mshc *hc, uint32_t hz)
{
	uintptr_t base = hc->cfg.base;
	uint32_t fsel = 0U;
	uint32_t achieved;
	uint32_t ctrl;
	uint32_t loop;
	int ret;

	if (!hc->ready && (hz != DWC_MSHC_INIT_CLOCK_HZ)) {
		/* bring-up programs the identification clock before the instance
		 * is marked ready; anything else waits for that */
		return 0U;
	}

	if (hz == 0U) {
		ctrl = mmio_rd16(base + DWC_MSHC_CLOCK_CTRL);
		mmio_wr16(base + DWC_MSHC_CLOCK_CTRL, (uint16_t)(ctrl & ~(uint32_t)DWC_MSHC_CLK_SD_EN));
		return hc->achieved_hz;
	}

	ret = dwc_mshc_wait_inhibit_clear(base);
	if (ret != DWC_MSHC_OK) {
		return 0U;
	}

	/* stop the card clock before moving the mux and the divider */
	mmio_wr16(base + DWC_MSHC_CLOCK_CTRL, 0U);

	achieved = dwc_mshc_select_clock(hc, hz, &fsel);
	if (achieved == 0U) {
		LOG_ERROR("dwc_mshc: clock %u Hz refused by this round's sources\n",
			  (unsigned)hz);
		return 0U;
	}

	/* internal clock on, divider programmed, card clock still stopped */
	mmio_wr16(base + DWC_MSHC_CLOCK_CTRL,
		  (uint16_t)(DWC_MSHC_CLK_INTERNAL_EN | DWC_MSHC_CLK_FREQ_SEL(fsel)));

	loop = dwc_mshc_polls_for(DWC_MSHC_CMD_BUDGET_US);
	while ((mmio_rd16(base + DWC_MSHC_CLOCK_CTRL) & DWC_MSHC_CLK_INTERNAL_STABLE) == 0U) {
		if (--loop == 0U) {
			LOG_ERROR("dwc_mshc: internal clock never stabilised (clk_ctrl=0x%x)\n",
				  mmio_rd16(base + DWC_MSHC_CLOCK_CTRL));
			return 0U;
		}
		dwc_mshc_udelay(DWC_MSHC_POLL_US);
	}

	/* below 100 MHz the DLL stays in bypass (this round's only path) */
	dwc_mshc_dll_bypass(base);

	/* the card clock comes back up with its divider already in place */
	ctrl = mmio_rd16(base + DWC_MSHC_CLOCK_CTRL);
	mmio_wr16(base + DWC_MSHC_CLOCK_CTRL,
		  (uint16_t)(ctrl | DWC_MSHC_CLK_SD_EN));

	if (achieved >= DWC_MSHC_HIGH_SPEED_MIN_HZ) {
		ctrl = mmio_rd8(base + DWC_MSHC_HOST_CTRL1);
		mmio_wr8(base + DWC_MSHC_HOST_CTRL1, (uint8_t)(ctrl | DWC_MSHC_HC1_HIGH_SPEED));

		/* the same selector u-boot and the kernel program for eMMC high
		 * speed; the 1.8 V signalling bit set at bring-up stays set */
		ctrl = mmio_rd16(base + DWC_MSHC_HOST_CTRL2);
		ctrl = (ctrl & ~(uint32_t)DWC_MSHC_HC2_UHS_MASK) | DWC_MSHC_HC2_UHS_SDR25;
		mmio_wr16(base + DWC_MSHC_HOST_CTRL2, (uint16_t)ctrl);
	}

	hc->clock_hz = hz;
	hc->achieved_hz = achieved;
	hc->clk_ctrl = mmio_rd16(base + DWC_MSHC_CLOCK_CTRL);

	dwc_mshc_udelay(DWC_MSHC_CLOCK_SETTLE_US);

	return achieved;
}

void dwc_mshc_set_bus_width(struct dwc_mshc *hc, unsigned int width)
{
	uintptr_t base = hc->cfg.base;
	uint32_t ctrl = mmio_rd8(base + DWC_MSHC_HOST_CTRL1);

	ctrl &= ~(uint32_t)(DWC_MSHC_HC1_DAT_XFER_WIDTH | DWC_MSHC_HC1_EXT_DAT_WIDTH);

	if (width == 8U) {
		ctrl |= DWC_MSHC_HC1_EXT_DAT_WIDTH;
	} else if (width == 4U) {
		ctrl |= DWC_MSHC_HC1_DAT_XFER_WIDTH;
	}

	mmio_wr8(base + DWC_MSHC_HOST_CTRL1, (uint8_t)ctrl);
	hc->width = width;
}

bool dwc_mshc_card_present(const struct dwc_mshc *hc)
{
	return (mmio_rd32(hc->cfg.base + DWC_MSHC_PRESENT_STATE) & DWC_MSHC_PS_CARD_INSERTED) != 0U;
}

bool dwc_mshc_card_busy(const struct dwc_mshc *hc)
{
	return (mmio_rd32(hc->cfg.base + DWC_MSHC_PRESENT_STATE) & DWC_MSHC_PS_DAT_0_LEVEL) == 0U;
}

void dwc_mshc_read_state(const struct dwc_mshc *hc, uint32_t *pstate,
			 uint32_t *normal_int, uint32_t *error_int)
{
	uintptr_t base = hc->cfg.base;

	if (pstate != NULL) {
		*pstate = mmio_rd32(base + DWC_MSHC_PRESENT_STATE);
	}
	if (normal_int != NULL) {
		*normal_int = mmio_rd16(base + DWC_MSHC_NORMAL_INT_STAT);
	}
	if (error_int != NULL) {
		*error_int = mmio_rd16(base + DWC_MSHC_ERROR_INT_STAT);
	}
}

/* --- the transfer path ---------------------------------------------------- */

static uint16_t dwc_mshc_command_bits(const struct dwc_mshc_cmd *cmd)
{
	uint32_t bits = (cmd->index & 0x3fu) << DWC_MSHC_CMD_INDEX_SHIFT;

	if (cmd->resp) {
		bits |= cmd->resp_long ? DWC_MSHC_CMD_RESP_136 : DWC_MSHC_CMD_RESP_48;
		if (cmd->resp_crc) {
			bits |= DWC_MSHC_CMD_CRC_CHECK;
			/* a 136-bit response has no command index field, so
			 * asking for the index check would fail every CMD2/CMD9 */
			if (!cmd->resp_long) {
				bits |= DWC_MSHC_CMD_INDEX_CHECK;
			}
		}
	}
	if (cmd->data != NULL) {
		bits |= DWC_MSHC_CMD_DATA_PRESENT;
	}

	return (uint16_t)bits;
}

/*
 * The response registers, and the one thing about them that is not the
 * specification's: this core presents a 136-bit response shifted by one byte
 * (the first byte of the register window is the response's padding bit, so the
 * whole response sits one byte further along than the register map says). The
 * older BSP driver for this board recovered the four words by shifting across
 * registers and checked the result byte for byte against the kernel's CID and
 * CSD readback; this is that recovery.
 */
static void dwc_mshc_read_response(struct dwc_mshc *hc, struct dwc_mshc_cmd *cmd)
{
	uintptr_t base = hc->cfg.base;

	if (cmd->resp_long) {
		uint32_t r01 = mmio_rd32(base + DWC_MSHC_RESP01);
		uint32_t r23 = mmio_rd32(base + DWC_MSHC_RESP23);
		uint32_t r45 = mmio_rd32(base + DWC_MSHC_RESP45);
		uint32_t r67 = mmio_rd32(base + DWC_MSHC_RESP67);

		cmd->response[3] = (r67 << 8) | (r45 >> 24);
		cmd->response[2] = (r45 << 8) | (r23 >> 24);
		cmd->response[1] = (r23 << 8) | (r01 >> 24);
		cmd->response[0] = (r01 << 8);
	} else {
		/* a 48-bit response's status field is already in place */
		cmd->response[0] = mmio_rd32(base + DWC_MSHC_RESP01);
		cmd->response[1] = 0U;
		cmd->response[2] = 0U;
		cmd->response[3] = 0U;
	}
}

static int dwc_mshc_prepare_dma(struct dwc_mshc *hc, const struct dwc_mshc_cmd *cmd)
{
	uintptr_t base = hc->cfg.base;
	const struct dwc_mshc_data *d = cmd->data;
	uintptr_t buf = (uintptr_t)d->buf;
	uint32_t xfer;

	if ((d->buf == NULL) || (d->len == 0U) || (d->blkcnt == 0U)) {
		return DWC_MSHC_ERR_ARG;
	}

	/* The engine takes word addresses and word lengths, and the SDMA
	 * boundary stops it, so the buffer's start and length must both be
	 * word-granular. The layer above owns that rule. */
	if (((buf & 0x3u) != 0U) || ((d->len & 0x3u) != 0U)) {
		LOG_ERROR("dwc_mshc: CMD%u buffer/length not word-aligned (buf=0x%lx len=%u)\n",
			  cmd->index, (unsigned long)buf, d->len);
		return DWC_MSHC_ERR_ARG;
	}

	/* The buffer crosses to the device: a write's dirty lines are written
	 * back before the engine reads memory, and a read's cached copies are
	 * dropped on both sides of the window, so no eviction during the window
	 * can write over what the card sent. */
	if (d->write) {
		cache_dcache_flush(buf, (unsigned long)d->len);
	} else {
		cache_dcache_invalidate(buf, (unsigned long)d->len);
	}

	mmio_wr32(base + DWC_MSHC_SDMA_ADDR, (uint32_t)buf);
	mmio_wr16(base + DWC_MSHC_BLOCK_SIZE,
		  (uint16_t)((d->blksz & 0xfffu) | DWC_MSHC_BLKSZ_SDMA_BOUNDARY_512K));
	mmio_wr16(base + DWC_MSHC_BLOCK_COUNT, (uint16_t)d->blkcnt);

	xfer = DWC_MSHC_XFER_DMA_EN | DWC_MSHC_XFER_BLK_CNT_EN;
	if (d->blkcnt > 1U) {
		xfer |= DWC_MSHC_XFER_MULTI_BLK;
	}
	if (!d->write) {
		xfer |= DWC_MSHC_XFER_DATA_DIR_READ;
	}
	mmio_wr16(base + DWC_MSHC_XFER_MODE, (uint16_t)xfer);

	return DWC_MSHC_OK;
}

static int dwc_mshc_send_command(struct dwc_mshc *hc, struct dwc_mshc_cmd *cmd)
{
	uintptr_t base = hc->cfg.base;
	uint32_t inhibit = DWC_MSHC_PS_CMD_INHIBIT;
	uint32_t loop = dwc_mshc_polls_for(DWC_MSHC_CMD_BUDGET_US);
	uint32_t stat;
	uint32_t err;

	if (cmd->data != NULL) {
		inhibit |= DWC_MSHC_PS_DAT_INHIBIT;
	}

	while ((mmio_rd32(base + DWC_MSHC_PRESENT_STATE) & inhibit) != 0U) {
		if (--loop == 0U) {
			LOG_ERROR("dwc_mshc: CMD%u inhibit stuck (pstate=0x%x clk_ctrl=0x%x)\n",
				  cmd->index, mmio_rd32(base + DWC_MSHC_PRESENT_STATE),
				  mmio_rd16(base + DWC_MSHC_CLOCK_CTRL));
			return DWC_MSHC_ERR_TIMEOUT;
		}
		dwc_mshc_udelay(DWC_MSHC_POLL_US);
	}

	dwc_mshc_int_status_clear(hc);

	mmio_wr32(base + DWC_MSHC_ARGUMENT, cmd->arg);
	mmio_wr16(base + DWC_MSHC_COMMAND, dwc_mshc_command_bits(cmd));

	loop = dwc_mshc_polls_for(DWC_MSHC_CMD_BUDGET_US);
	do {
		stat = mmio_rd16(base + DWC_MSHC_NORMAL_INT_STAT);
		err = mmio_rd16(base + DWC_MSHC_ERROR_INT_STAT);

		if ((stat & DWC_MSHC_INT_CMD_COMPLETE) != 0U) {
			break;
		}
		if ((err & DWC_MSHC_EINT_CMD_ERRORS) != 0U) {
			break;
		}

		dwc_mshc_udelay(DWC_MSHC_POLL_US);
	} while (--loop != 0U);

	if (loop == 0U) {
		LOG_ERROR("dwc_mshc: CMD%u timeout (pstate=0x%x nint=0x%x eint=0x%x "
			  "clk_ctrl=0x%x ctrl1=0x%x)\n", cmd->index,
			  mmio_rd32(base + DWC_MSHC_PRESENT_STATE), stat, err,
			  mmio_rd16(base + DWC_MSHC_CLOCK_CTRL),
			  mmio_rd8(base + DWC_MSHC_HOST_CTRL1));
		dwc_mshc_int_status_clear(hc);
		/* reset the command circuit so the inhibit bit cannot wedge the
		 * commands that follow */
		(void)dwc_mshc_soft_reset(hc, (uint8_t)DWC_MSHC_SW_RESET_CMD);
		return DWC_MSHC_ERR_TIMEOUT;
	}

	if ((err & DWC_MSHC_EINT_CMD_ERRORS) != 0U) {
		LOG_ERROR("dwc_mshc: CMD%u failed (eint=0x%x pstate=0x%x)\n",
			  cmd->index, err, mmio_rd32(base + DWC_MSHC_PRESENT_STATE));
		mmio_wr16(base + DWC_MSHC_ERROR_INT_STAT, (uint16_t)err);
		return DWC_MSHC_ERR_CMD;
	}

	/* Only the completion bit is cleared here: a data command's transfer
	 * completion may already be latched beside it, and clearing the whole
	 * word would drop the data wait's own evidence. */
	mmio_wr16(base + DWC_MSHC_NORMAL_INT_STAT, (uint16_t)DWC_MSHC_INT_CMD_COMPLETE);

	if (cmd->resp) {
		dwc_mshc_read_response(hc, cmd);
	}

	return DWC_MSHC_OK;
}

static int dwc_mshc_wait_data(struct dwc_mshc *hc, const struct dwc_mshc_cmd *cmd)
{
	uintptr_t base = hc->cfg.base;
	uint32_t loop = dwc_mshc_polls_for(DWC_MSHC_DATA_BUDGET_US);
	uintptr_t addr = (uintptr_t)cmd->data->buf;
	uintptr_t end = addr + cmd->data->len;
	uint32_t stat;
	uint32_t err;

	do {
		stat = mmio_rd16(base + DWC_MSHC_NORMAL_INT_STAT);
		err = mmio_rd16(base + DWC_MSHC_ERROR_INT_STAT);

		if ((err & DWC_MSHC_EINT_DATA_ERRORS) != 0U) {
			LOG_ERROR("dwc_mshc: CMD%u data error (eint=0x%x nint=0x%x "
				  "blksz=0x%x blkcnt=0x%x)\n", cmd->index, err, stat,
				  mmio_rd16(base + DWC_MSHC_BLOCK_SIZE),
				  mmio_rd16(base + DWC_MSHC_BLOCK_COUNT));
			dwc_mshc_int_status_clear(hc);
			return DWC_MSHC_ERR_DATA;
		}

		if ((stat & DWC_MSHC_INT_XFER_COMPLETE) != 0U) {
			mmio_wr16(base + DWC_MSHC_NORMAL_INT_STAT, (uint16_t)stat);
			if (!cmd->data->write) {
				/* the buffer is the card's until here: drop whatever
				 * this core pulled into its caches during the window */
				cache_dcache_invalidate((uintptr_t)cmd->data->buf,
							(unsigned long)cmd->data->len);
			}
			return DWC_MSHC_OK;
		}

		/* The SDMA engine stopped at a boundary and is waiting to be
		 * pointed at the continuation address: the first boundary after
		 * the address it was last given. */
		if ((stat & DWC_MSHC_INT_DMA) != 0U) {
			mmio_wr16(base + DWC_MSHC_NORMAL_INT_STAT, (uint16_t)DWC_MSHC_INT_DMA);
			addr = dwc_mshc_sdma_continue(addr);
			dwc_mshc_census_state.boundary_events++;
			if (addr < end) {
				mmio_wr32(base + DWC_MSHC_SDMA_ADDR, (uint32_t)addr);
			}
		}

		dwc_mshc_udelay(DWC_MSHC_POLL_US);
	} while (--loop != 0U);

	LOG_ERROR("dwc_mshc: CMD%u data timeout (nint=0x%x eint=0x%x pstate=0x%x)\n",
		  cmd->index, stat, err, mmio_rd32(base + DWC_MSHC_PRESENT_STATE));
	dwc_mshc_int_status_clear(hc);

	return DWC_MSHC_ERR_TIMEOUT;
}

int dwc_mshc_transfer(struct dwc_mshc *hc, struct dwc_mshc_cmd *cmd)
{
	int ret;

	if ((hc == NULL) || (cmd == NULL)) {
		return DWC_MSHC_ERR_ARG;
	}

	dwc_mshc_census_state.transfers++;
	if (cmd->index == 23U) {
		dwc_mshc_census_state.cmd23++;
	}

	if (cmd->data == NULL) {
		ret = dwc_mshc_send_command(hc, cmd);
	} else {
		dwc_mshc_census_state.with_data++;
		ret = dwc_mshc_prepare_dma(hc, cmd);
		if (ret == DWC_MSHC_OK) {
			ret = dwc_mshc_send_command(hc, cmd);
		}
		if (ret == DWC_MSHC_OK) {
			ret = dwc_mshc_wait_data(hc, cmd);
		}
		if (ret != DWC_MSHC_OK) {
			/* reset the data circuit so a stuck data path cannot
			 * wedge the next command */
			(void)dwc_mshc_soft_reset(hc, (uint8_t)DWC_MSHC_SW_RESET_DAT);
			dwc_mshc_udelay(DWC_MSHC_CLOCK_SETTLE_US);
		}
		if (ret == DWC_MSHC_OK) {
			if (cmd->data->write) {
				dwc_mshc_census_state.tx_bytes += cmd->data->len;
			} else {
				dwc_mshc_census_state.rx_bytes += cmd->data->len;
			}
		}
	}

	if (ret != DWC_MSHC_OK) {
		dwc_mshc_census_state.errors++;
	}

	return ret;
}

void dwc_mshc_census_get(struct dwc_mshc_census *out)
{
	if (out != NULL) {
		*out = dwc_mshc_census_state;
	}
}

void dwc_mshc_census_reset(void)
{
	(void)memset(&dwc_mshc_census_state, 0, sizeof(dwc_mshc_census_state));
}
