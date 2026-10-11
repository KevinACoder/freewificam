/*
 * @file   dwc_mmc.c
 * @brief  DesignWare Mobile Storage Host driver for this board's sdmmc0:
 *         CRU clock/reset, command issue, the IDMAC data path, polling.
 *
 * The sequences follow the SoC's TRM (part 2, chapter 6) as the u-boot
 * dw_mmc driver implements them, with the Rockchip CRU and clock-phase
 * coordinates of this SoC. Three details of the data path are not optional on
 * this board and are called out where they live below: the IDMAC status enables
 * must be armed before the engine starts, a read must invalidate the CPU's
 * copies of its buffer on both sides of the DMA window, and DTO alone does not
 * mean the data has reached memory.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "arch_asm.h"
#include "cache.h"
#include "generic_timer.h"
#include "mmio.h"

#include "dwc_mmc.h"
#include "dwc_mmc_regs.h"

/* polling budget: the loop counts 100 us steps */
#define DWC_MMC_POLL_US			100U
#define DWC_MMC_CMD_POLLS		1000U	/* 100 ms for one command */
#define DWC_MMC_DATA_POLLS		3000U	/* 300 ms for one data stage */
#define DWC_MMC_RESET_POLLS		500U	/* 50 ms for a self-clearing step */

/* The poll at which a data wait says what it is seeing: 10 ms in, a data phase
 * that has not started at all and one that started and stalled look the same
 * from outside, and this line is what separates them. */
#define DWC_MMC_DATA_SAMPLE_POLL	100U

/*
 * The IDMAC descriptor list. One controller, one card and transfers serialised
 * by the stack above, so a single static list is the whole requirement.
 *
 * It lives in the linker's non-cacheable window, which is this board's rule for
 * DMA-visible memory (arch/aarch64/baremetal.ld `.noncacheable`, the same
 * placement the ethernet driver's descriptor rings use): a 16-byte descriptor
 * shares a 64-byte cache line with its neighbours, and arming one of them would
 * flush a stale copy of an already-completed neighbour back over what the
 * engine wrote. The window is identity-mapped, so the list's virtual address is
 * the address the engine takes.
 */
static struct dwc_mmc_desc dwc_mmc_desc_pool[DWC_MMC_DESC_NUM]
	__attribute__((section(".noncacheable"), aligned(64)));

/* The controllers this board wires. One entry: sdmmc0 carries the SDIO part. */
static const struct dwc_mmc_cfg dwc_mmc_board[] = {
	{ .base = DWC_MMC0_BASE, .index = 0U, .intid = DWC_MMC0_INTID },
};

const struct dwc_mmc_cfg *dwc_mmc_board_cfg(unsigned int index)
{
	if (index >= (unsigned int)(sizeof(dwc_mmc_board) / sizeof(dwc_mmc_board[0]))) {
		return NULL;
	}

	return &dwc_mmc_board[index];
}

/* --- small helpers -------------------------------------------------------- */

/* Microsecond busy-wait on the architectural counter. A requested delay always
 * costs at least one tick, so a zero-length request does not degenerate into
 * no wait at all. */
static void dwc_mmc_udelay(uint32_t usec)
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

/* Rockchip CRU/GRF registers carry a hiword write-enable, so a field is
 * changed with one masked write that leaves its neighbours alone. */
static void dwc_mmc_cru_write(uintptr_t reg, uint32_t mask, uint32_t val)
{
	mmio_wr32(reg, ((mask & 0xffffu) << 16) | (val & mask));
}

/* --- CRU: gates, reset release, clock source ------------------------------ */

static void dwc_mmc_cru_clock_init(struct dwc_mmc *hc)
{
	uint32_t gate;
	uint32_t rst;
	uint32_t mux;
	uint32_t shift;

	if (hc->cfg.index == 0U) {
		gate = DWC_MMC_CRU_SDMMC0_HCLK_GATE | DWC_MMC_CRU_SDMMC0_CLK_GATE;
		rst = DWC_MMC_CRU_SDMMC0_SOFTRST;
		mux = DWC_MMC_CRU_SDMMC0_MUX_MASK;
		shift = DWC_MMC_CRU_SDMMC0_MUX_SHIFT;
	} else {
		gate = DWC_MMC_CRU_SDMMC1_HCLK_GATE | DWC_MMC_CRU_SDMMC1_CLK_GATE;
		rst = DWC_MMC_CRU_SDMMC1_SOFTRST;
		mux = DWC_MMC_CRU_SDMMC1_MUX_MASK;
		shift = DWC_MMC_CRU_SDMMC1_MUX_SHIFT;
	}

	/* a gate bit reads 1 when the clock is off, so enabling means writing 0 */
	dwc_mmc_cru_write(DWC_MMC_CRU_BASE + DWC_MMC_CRU_GATE_CON, gate, 0U);
	/* asserting a reset means writing 1, releasing it writing 0 */
	dwc_mmc_cru_write(DWC_MMC_CRU_BASE + DWC_MMC_CRU_SOFTRST_CON, rst, 0U);
	/* start from the 50 MHz source; the identification clock divides it */
	dwc_mmc_cru_write(DWC_MMC_CRU_BASE + DWC_MMC_CRU_CLKSEL_CON, mux,
			  DWC_MMC_CRU_SEL_50M << shift);

	hc->src_hz = 50000000U;
}

/* Pick the CRU source and the divider for a target card clock. The controller
 * divides by 2 * CLKDIV, and CLKDIV 0 bypasses the divider, so the achievable
 * rates are the source's four values or half of them. A target between two of
 * them rounds up to the next one. */
static void dwc_mmc_select_clock(struct dwc_mmc *hc, uint32_t target, uint32_t *div)
{
	uint32_t mux;
	uint32_t shift;
	uint32_t sel;
	uint32_t src;

	if (hc->cfg.index == 0U) {
		mux = DWC_MMC_CRU_SDMMC0_MUX_MASK;
		shift = DWC_MMC_CRU_SDMMC0_MUX_SHIFT;
	} else {
		mux = DWC_MMC_CRU_SDMMC1_MUX_MASK;
		shift = DWC_MMC_CRU_SDMMC1_MUX_SHIFT;
	}

	if (target <= 750000U) {
		/* the identification clock: the 750 kHz source, bypassing the divider */
		sel = DWC_MMC_CRU_SEL_750K;
		src = 750000U;
		*div = 0U;
	} else if (target <= 50000000U) {
		sel = DWC_MMC_CRU_SEL_50M;
		src = 50000000U;
		*div = (target >= 50000000U) ? 0U : 1U;
	} else if (target <= 100000000U) {
		sel = DWC_MMC_CRU_SEL_100M;
		src = 100000000U;
		*div = 0U;
	} else {
		sel = DWC_MMC_CRU_SEL_300M;
		src = 300000000U;
		*div = 1U;
	}

	dwc_mmc_cru_write(DWC_MMC_CRU_BASE + DWC_MMC_CRU_CLKSEL_CON, mux, sel << shift);

	hc->src_hz = src;
}

/* One update-clock step: raise the command and wait for the CIU to take it. */
static int dwc_mmc_clock_step(uintptr_t base, uint32_t bits)
{
	uint32_t loop = DWC_MMC_RESET_POLLS;

	mmio_wr32(base + DWC_MMC_CMD, bits | DWC_MMC_CMD_START);
	while ((mmio_rd32(base + DWC_MMC_CMD) & DWC_MMC_CMD_START) != 0U) {
		if (--loop == 0U) {
			return DWC_MMC_ERR_TIMEOUT;
		}
		dwc_mmc_udelay(DWC_MMC_POLL_US);
	}

	return DWC_MMC_OK;
}

/* Apply a clock change the way the TRM prescribes: stop the clocks, write the
 * divider, start them again - each step delivered to the CIU as an
 * update-clock command. The Rockchip drive/sample phases are written with the
 * divider (the vendor kernel's set_ios does the same). */
static int dwc_mmc_clock_update(struct dwc_mmc *hc, uint32_t div)
{
	uintptr_t base = hc->cfg.base;
	uint32_t bits = DWC_MMC_CMD_UPD_CLK | DWC_MMC_CMD_WAIT_PRV_DAT;

	mmio_wr32(base + DWC_MMC_CLKENA, 0U);
	if (dwc_mmc_clock_step(base, bits) != DWC_MMC_OK) {
		return DWC_MMC_ERR_TIMEOUT;
	}

	mmio_wr32(base + DWC_MMC_CLKDIV, div);
	mmio_wr32(base + DWC_MMC_TIMING_CON0, DWC_MMC_TIMING_DRIVE_90);
	mmio_wr32(base + DWC_MMC_TIMING_CON1, DWC_MMC_TIMING_SAMPLE_0);
	if (dwc_mmc_clock_step(base, bits) != DWC_MMC_OK) {
		return DWC_MMC_ERR_TIMEOUT;
	}

	mmio_wr32(base + DWC_MMC_CLKENA, DWC_MMC_CLKEN_ENABLE | DWC_MMC_CLKEN_LOW_PWR);
	if (dwc_mmc_clock_step(base, bits) != DWC_MMC_OK) {
		return DWC_MMC_ERR_TIMEOUT;
	}

	return DWC_MMC_OK;
}

/* --- reset ---------------------------------------------------------------- */

static int dwc_mmc_soft_reset(struct dwc_mmc *hc)
{
	uintptr_t base = hc->cfg.base;
	uint32_t loop = DWC_MMC_RESET_POLLS;

	mmio_wr32(base + DWC_MMC_CTRL, DWC_MMC_CTRL_ALL_RESET_FLAGS);
	while ((mmio_rd32(base + DWC_MMC_CTRL) & DWC_MMC_CTRL_ALL_RESET_FLAGS) != 0U) {
		if (--loop == 0U) {
			LOG_ERROR("dwc_mmc: reset did not self-clear (ctrl=0x%x)\n",
				  mmio_rd32(base + DWC_MMC_CTRL));
			return DWC_MMC_ERR_TIMEOUT;
		}
		dwc_mmc_udelay(DWC_MMC_POLL_US);
	}

	return DWC_MMC_OK;
}

/* Everything a reset clears and the driver needs back. */
static void dwc_mmc_reprogram(struct dwc_mmc *hc)
{
	uintptr_t base = hc->cfg.base;

	mmio_wr32(base + DWC_MMC_CTRL, DWC_MMC_CTRL_INT_ENABLE | DWC_MMC_CTRL_USE_IDMAC);
	mmio_wr32(base + DWC_MMC_RINTSTS, DWC_MMC_INT_ALL);
	mmio_wr32(base + DWC_MMC_IDSTS, DWC_MMC_INT_ALL);
	mmio_wr32(base + DWC_MMC_FIFOTH,
		  DWC_MMC_FIFOTH_MSIZE(6) | DWC_MMC_FIFOTH_RX_WMARK(0x7fu) |
		  DWC_MMC_FIFOTH_TX_WMARK(0x80u));
	/* The data timeout counter is disabled while this register reads zero, and
	 * a disabled counter is why a data phase that never starts looks like a
	 * silent hang instead of a timeout. The reference drivers leave it at its
	 * maximum. */
	mmio_wr32(base + DWC_MMC_TMOUT, 0xffffffffU);
	mmio_wr32(base + DWC_MMC_PWREN, 1U);
}

/* One line of controller state, for the moment a transfer failed: a verdict
 * about a data path is only worth anything next to the register values that
 * produced it. */
static void dwc_mmc_dump_state(struct dwc_mmc *hc)
{
	uintptr_t base = hc->cfg.base;

	LOG_ERROR("dwc_mmc: state cmd=0x%x ctrl=0x%x bmod=0x%x ctype=0x%x "
		  "clkena=0x%x clkdiv=0x%x\n", hc->last_cmd,
		  mmio_rd32(base + DWC_MMC_CTRL), mmio_rd32(base + DWC_MMC_BMOD),
		  mmio_rd32(base + DWC_MMC_CTYPE), mmio_rd32(base + DWC_MMC_CLKENA),
		  mmio_rd32(base + DWC_MMC_CLKDIV));
	LOG_ERROR("dwc_mmc: state status=0x%x tmout=0x%x dsc=0x%x buf=0x%x "
		  "idsts=0x%x idinten=0x%x\n",
		  mmio_rd32(base + DWC_MMC_STATUS), mmio_rd32(base + DWC_MMC_TMOUT),
		  mmio_rd32(base + DWC_MMC_DSCADDR), mmio_rd32(base + DWC_MMC_BUFADDR),
		  mmio_rd32(base + DWC_MMC_IDSTS), mmio_rd32(base + DWC_MMC_IDINTEN));
}

/* --- command issue -------------------------------------------------------- */

static uint32_t dwc_mmc_make_raw_cmd(const struct dwc_mmc_cmd *cmd)
{
	uint32_t raw = DWC_MMC_CMD_INDX(cmd->index);

	if (cmd->send_init) {
		raw |= DWC_MMC_CMD_SEND_INIT;
	}

	if (cmd->data != NULL) {
		raw |= DWC_MMC_CMD_DATA_EXP | DWC_MMC_CMD_WAIT_PRV_DAT;
		if (cmd->data->write) {
			raw |= DWC_MMC_CMD_RW;
		}
	} else {
		raw |= DWC_MMC_CMD_WAIT_PRV_DAT;
	}

	if (cmd->resp) {
		raw |= DWC_MMC_CMD_RESP_EXP;
		if (cmd->resp_long) {
			raw |= DWC_MMC_CMD_RESP_LENGTH;
		}
	}

	if (cmd->resp_crc) {
		raw |= DWC_MMC_CMD_CHECK_CRC;
	}

	return raw;
}

static void dwc_mmc_read_response(struct dwc_mmc *hc, struct dwc_mmc_cmd *cmd)
{
	uintptr_t base = hc->cfg.base;

	if (cmd->resp_long) {
		/* the controller presents a 136-bit response most significant word
		 * first; the SDIO path never asks for one */
		cmd->response[0] = mmio_rd32(base + DWC_MMC_RESP3);
		cmd->response[1] = mmio_rd32(base + DWC_MMC_RESP2);
		cmd->response[2] = mmio_rd32(base + DWC_MMC_RESP1);
		cmd->response[3] = mmio_rd32(base + DWC_MMC_RESP0);
	} else {
		cmd->response[0] = mmio_rd32(base + DWC_MMC_RESP0);
		cmd->response[1] = 0U;
		cmd->response[2] = 0U;
		cmd->response[3] = 0U;
	}
}

static int dwc_mmc_send_command(struct dwc_mmc *hc, struct dwc_mmc_cmd *cmd)
{
	uintptr_t base = hc->cfg.base;
	uint32_t mask = 0U;
	uint32_t loop;

	/* the data path must be idle before a command may use it */
	loop = DWC_MMC_CMD_POLLS;
	while ((mmio_rd32(base + DWC_MMC_STATUS) & DWC_MMC_STATUS_DATA_BUSY) != 0U) {
		if (--loop == 0U) {
			LOG_ERROR("dwc_mmc: data path stayed busy before CMD%u\n", cmd->index);
			return DWC_MMC_ERR_TIMEOUT;
		}
		dwc_mmc_udelay(DWC_MMC_POLL_US);
	}

	mmio_wr32(base + DWC_MMC_RINTSTS, DWC_MMC_INT_ALL);
	mmio_wr32(base + DWC_MMC_IDSTS, DWC_MMC_INT_ALL);

	mmio_wr32(base + DWC_MMC_CMDARG, cmd->arg);
	hc->last_cmd = dwc_mmc_make_raw_cmd(cmd) | DWC_MMC_CMD_USE_HOLD_REG |
		       DWC_MMC_CMD_START;
	mmio_wr32(base + DWC_MMC_CMD, hc->last_cmd);

	loop = DWC_MMC_CMD_POLLS;
	do {
		mask = mmio_rd32(base + DWC_MMC_RINTSTS);
		if ((mask & DWC_MMC_INT_CDONE) != 0U) {
			break;
		}
		dwc_mmc_udelay(DWC_MMC_POLL_US);
	} while (--loop != 0U);

	if (loop == 0U) {
		LOG_ERROR("dwc_mmc: CMD%u timed out (rintsts=0x%x)\n", cmd->index, mask);
		dwc_mmc_dump_state(hc);
		mmio_wr32(base + DWC_MMC_RINTSTS, mask);
		return DWC_MMC_ERR_TIMEOUT;
	}

	if ((mask & DWC_MMC_INT_RTO) != 0U) {
		mmio_wr32(base + DWC_MMC_RINTSTS, mask);
		return DWC_MMC_ERR_TIMEOUT;
	}

	if ((mask & DWC_MMC_INT_RE) != 0U) {
		LOG_ERROR("dwc_mmc: CMD%u response error (rintsts=0x%x)\n", cmd->index, mask);
		mmio_wr32(base + DWC_MMC_RINTSTS, mask);
		return DWC_MMC_ERR_CMD;
	}

	if (cmd->resp) {
		dwc_mmc_read_response(hc, cmd);
	}

	return DWC_MMC_OK;
}

/* --- IDMAC data path ------------------------------------------------------ */

static uint32_t dwc_mmc_build_descs(uintptr_t buf, uint32_t len)
{
	uintptr_t pool = (uintptr_t)dwc_mmc_desc_pool;
	uint32_t remain = len;
	uintptr_t addr = buf;
	uint32_t n = 0U;

	while ((remain > 0U) && (n < DWC_MMC_DESC_NUM)) {
		struct dwc_mmc_desc *desc = &dwc_mmc_desc_pool[n];
		uint32_t chunk = (remain > DWC_MMC_DESC_MAX_LEN) ?
					 DWC_MMC_DESC_MAX_LEN : remain;

		desc->flags = DWC_MMC_DESC_OWN | DWC_MMC_DESC_CH;
		desc->cnt = chunk;
		desc->addr = (uint32_t)addr;
		if (n == 0U) {
			desc->flags |= DWC_MMC_DESC_FD;
		}
		if (chunk >= remain) {
			desc->flags |= DWC_MMC_DESC_LD;
			desc->next = 0U;
		} else {
			desc->next = (uint32_t)(pool + (uintptr_t)(n + 1U) *
						(uintptr_t)sizeof(struct dwc_mmc_desc));
		}

		remain -= chunk;
		addr += chunk;
		n++;
	}

	return n;
}

static void dwc_mmc_dma_disable(struct dwc_mmc *hc)
{
	uintptr_t base = hc->cfg.base;
	uint32_t ctrl = mmio_rd32(base + DWC_MMC_CTRL);

	ctrl &= ~(uint32_t)DWC_MMC_CTRL_DMA_ENABLE;
	mmio_wr32(base + DWC_MMC_CTRL, ctrl);
}

static int dwc_mmc_prepare_dma(struct dwc_mmc *hc, const struct dwc_mmc_cmd *cmd)
{
	uintptr_t base = hc->cfg.base;
	const struct dwc_mmc_data *d = cmd->data;
	uintptr_t buf = (uintptr_t)d->buf;
	uint32_t descs;
	uint32_t ctrl;
	uint32_t loop;

	if ((d->buf == NULL) || (d->len == 0U)) {
		return DWC_MMC_ERR_ARG;
	}
	/* The engine takes word addresses and word lengths. The rule belongs to the
	 * layer above (the SDIO adapter absorbs it); reaching this point with a
	 * length that is not a multiple of four means that layer let it through. */
	if (((buf & 0x3u) != 0U) || ((d->len & 0x3u) != 0U) || ((buf >> 32) != 0U)) {
		LOG_ERROR("dwc_mmc: CMD%u buffer/length not usable by the engine "
			  "(buf=0x%lx len=%u)\n", cmd->index, (unsigned long)buf, d->len);
		return DWC_MMC_ERR_ARG;
	}

	descs = dwc_mmc_build_descs(buf, d->len);
	if (descs == 0U) {
		LOG_ERROR("dwc_mmc: CMD%u descriptor chain refused (len=%u)\n", cmd->index, d->len);
		return DWC_MMC_ERR_ARG;
	}

	/*
	 * The list is in the non-cacheable window, so the engine reads what the
	 * CPU wrote once the store reaches memory - which is what this barrier
	 * orders, in place of a cache clean the window makes unnecessary.
	 */
	ARCH_DSB();

	/*
	 * The buffer crosses in the other direction. A read hands it to the device,
	 * so the CPU's copies are dropped first: a dirty line evicted during the DMA
	 * window would otherwise write back over the incoming data (the block-mode
	 * corruption this path was hunted down through). A write hands the data to
	 * the device, so the CPU's dirty lines are written back first.
	 */
	if (d->write) {
		cache_dcache_flush((uintptr_t)d->buf, (unsigned long)d->len);
	} else {
		cache_dcache_invalidate((uintptr_t)d->buf, (unsigned long)d->len);
	}

	/* the FIFO is emptied and the reset waited out before the engine is armed */
	mmio_wr32(base + DWC_MMC_CTRL, DWC_MMC_CTRL_FIFO_RESET);
	loop = DWC_MMC_RESET_POLLS;
	while ((mmio_rd32(base + DWC_MMC_CTRL) & DWC_MMC_CTRL_FIFO_RESET) != 0U) {
		if (--loop == 0U) {
			LOG_ERROR("dwc_mmc: FIFO reset did not self-clear\n");
			return DWC_MMC_ERR_TIMEOUT;
		}
		dwc_mmc_udelay(DWC_MMC_POLL_US);
	}

	mmio_wr32(base + DWC_MMC_DBADDR, (uint32_t)(uintptr_t)dwc_mmc_desc_pool);

	/* Software-reset the IDMAC before switching it on: the reference drivers do
	 * this on every transfer, and an engine that was left mid-transfer by an
	 * earlier failure is otherwise switched on in a state of its own. */
	ctrl = mmio_rd32(base + DWC_MMC_BMOD);
	ctrl |= DWC_MMC_IDMAC_SWRESET;
	mmio_wr32(base + DWC_MMC_BMOD, ctrl);
	loop = DWC_MMC_RESET_POLLS;
	while ((mmio_rd32(base + DWC_MMC_BMOD) & DWC_MMC_IDMAC_SWRESET) != 0U) {
		if (--loop == 0U) {
			LOG_ERROR("dwc_mmc: the IDMAC reset did not self-clear\n");
			return DWC_MMC_ERR_TIMEOUT;
		}
		dwc_mmc_udelay(DWC_MMC_POLL_US);
	}

	/* INT_ENABLE comes along: the FIFO reset above cleared it */
	ctrl = mmio_rd32(base + DWC_MMC_CTRL);
	ctrl |= DWC_MMC_CTRL_USE_IDMAC | DWC_MMC_CTRL_DMA_ENABLE | DWC_MMC_CTRL_INT_ENABLE;
	mmio_wr32(base + DWC_MMC_CTRL, ctrl);

	/*
	 * Arm the IDMAC's status enables BEFORE switching the engine on. On this IP
	 * the IDSTS completion and error bits do not post unless the matching
	 * IDINTEN bits are set, so a wait that only reads RI without arming it sees
	 * a permanently zero status word and every data command fails with nothing
	 * to show for it. NetBSD writes IDIE before its wait; the kernel's
	 * dw_mci_idmac_init does the same.
	 */
	mmio_wr32(base + DWC_MMC_IDSTS, DWC_MMC_INT_ALL);
	mmio_wr32(base + DWC_MMC_IDINTEN,
		  DWC_MMC_IDMAC_INT_RI | DWC_MMC_IDMAC_INT_TI |
		  DWC_MMC_IDMAC_INT_NI | DWC_MMC_IDMAC_INT_AI |
		  DWC_MMC_IDMAC_ERR_FLAGS);

	ctrl = mmio_rd32(base + DWC_MMC_BMOD);
	ctrl |= DWC_MMC_IDMAC_ENABLE | DWC_MMC_IDMAC_FB;
	mmio_wr32(base + DWC_MMC_BMOD, ctrl);

	mmio_wr32(base + DWC_MMC_BLKSIZ, d->blksz);
	mmio_wr32(base + DWC_MMC_BYTCNT, d->len);

	return DWC_MMC_OK;
}

/* Wait for the receive engine to close its last descriptor. DTO only says the
 * controller consumed the wire data; the FIFO-to-memory drain trails it, and
 * reading the buffer before the drain ends returns stale bytes. RI is the bit
 * the drain finishes on. */
static int dwc_mmc_wait_idmac_rx(struct dwc_mmc *hc)
{
	uintptr_t base = hc->cfg.base;
	uint32_t idsts = 0U;
	uint32_t loop = DWC_MMC_DATA_POLLS;

	do {
		idsts = mmio_rd32(base + DWC_MMC_IDSTS);
		if ((idsts & DWC_MMC_IDMAC_ERR_FLAGS) != 0U) {
			LOG_ERROR("dwc_mmc: IDMAC error (idsts=0x%x)\n", idsts);
			break;
		}
		if ((idsts & DWC_MMC_IDMAC_INT_RI) != 0U) {
			mmio_wr32(base + DWC_MMC_IDSTS, DWC_MMC_INT_ALL);
			return DWC_MMC_OK;
		}
		dwc_mmc_udelay(DWC_MMC_POLL_US);
	} while (--loop != 0U);

	mmio_wr32(base + DWC_MMC_IDSTS, DWC_MMC_INT_ALL);
	return DWC_MMC_ERR_TIMEOUT;
}

static int dwc_mmc_wait_data(struct dwc_mmc *hc, const struct dwc_mmc_cmd *cmd)
{
	uintptr_t base = hc->cfg.base;
	uint32_t mask = 0U;
	uint32_t loop = DWC_MMC_DATA_POLLS;

	do {
		mask = mmio_rd32(base + DWC_MMC_RINTSTS);

		if ((mask & (DWC_MMC_INT_DATA_ERR | DWC_MMC_INT_DATA_TOUT)) != 0U) {
			LOG_ERROR("dwc_mmc: CMD%u data error (rintsts=0x%x r5=0x%x)\n",
				  cmd->index, mask, cmd->response[0]);
			dwc_mmc_dump_state(hc);
			mmio_wr32(base + DWC_MMC_RINTSTS, mask);
			mmio_wr32(base + DWC_MMC_IDSTS, DWC_MMC_INT_ALL);
			return DWC_MMC_ERR_DATA;
		}

		if ((mask & DWC_MMC_INT_DTO) != 0U) {
			break;
		}

		/* A data phase that never started and one that started and stalled look
		 * identical from the outside; the status word tells them apart (bit 9
		 * is the card holding the data lines busy), so it is read once, well
		 * into the wait. */
		if (loop == (DWC_MMC_DATA_POLLS - DWC_MMC_DATA_SAMPLE_POLL)) {
			LOG_ERROR("dwc_mmc: CMD%u data wait status=0x%x rintsts=0x%x "
				  "idsts=0x%x dsc=0x%x buf=0x%x\n", cmd->index,
				  mmio_rd32(base + DWC_MMC_STATUS), mask,
				  mmio_rd32(base + DWC_MMC_IDSTS),
				  mmio_rd32(base + DWC_MMC_DSCADDR),
				  mmio_rd32(base + DWC_MMC_BUFADDR));
		}

		dwc_mmc_udelay(DWC_MMC_POLL_US);
	} while (--loop != 0U);

	if (loop == 0U) {
		LOG_ERROR("dwc_mmc: CMD%u data timeout (rintsts=0x%x r5=0x%x)\n",
			  cmd->index, mask, cmd->response[0]);
		dwc_mmc_dump_state(hc);
		mmio_wr32(base + DWC_MMC_RINTSTS, mask);
		return DWC_MMC_ERR_TIMEOUT;
	}

	mmio_wr32(base + DWC_MMC_RINTSTS, mask);

	if (!cmd->data->write) {
		int ret = dwc_mmc_wait_idmac_rx(hc);

		if (ret != DWC_MMC_OK) {
			return ret;
		}

		/* Drop whatever this core pulled into its caches while the device was
		 * writing: the buffer is the device's, not the CPU's, until here. */
		cache_dcache_invalidate((uintptr_t)cmd->data->buf,
					(unsigned long)cmd->data->len);
	}

	return DWC_MMC_OK;
}

static int dwc_mmc_transfer_data(struct dwc_mmc *hc, struct dwc_mmc_cmd *cmd)
{
	int ret;

	ret = dwc_mmc_prepare_dma(hc, cmd);
	if (ret != DWC_MMC_OK) {
		return ret;
	}

	ret = dwc_mmc_send_command(hc, cmd);
	if (ret != DWC_MMC_OK) {
		dwc_mmc_dma_disable(hc);
		return ret;
	}

	ret = dwc_mmc_wait_data(hc, cmd);

	dwc_mmc_dma_disable(hc);

	return ret;
}

/* --- public interface ----------------------------------------------------- */

int dwc_mmc_init(struct dwc_mmc *hc, const struct dwc_mmc_cfg *cfg)
{
	uintptr_t base;

	if ((hc == NULL) || (cfg == NULL) || (cfg->base == 0U)) {
		return DWC_MMC_ERR_ARG;
	}

	(void)memset(hc, 0, sizeof(*hc));
	hc->cfg = *cfg;
	base = hc->cfg.base;

	dwc_mmc_cru_clock_init(hc);

	if (dwc_mmc_soft_reset(hc) != DWC_MMC_OK) {
		return DWC_MMC_ERR_TIMEOUT;
	}

	dwc_mmc_reprogram(hc);

	/* the card needs its power ramp before the first command (TRM 6.6.4.1) */
	osDelay(10U);

	if (dwc_mmc_set_clock(hc, 400000U) == 0U) {
		LOG_ERROR("dwc_mmc: the identification clock was refused\n");
		return DWC_MMC_ERR_TIMEOUT;
	}

	/* the clock needs to settle once it is running: without this wait the
	 * first CMD0 times out on this board */
	osDelay(2U);

	if (!dwc_mmc_card_present(hc)) {
		LOG_ERROR("dwc_mmc: the slot reads empty (base=0x%lx)\n", (unsigned long)base);
		return DWC_MMC_ERR_NO_CARD;
	}

	hc->ready = true;

	return DWC_MMC_OK;
}

void dwc_mmc_deinit(struct dwc_mmc *hc)
{
	if (hc == NULL) {
		return;
	}

	if (hc->ready) {
		(void)dwc_mmc_set_clock(hc, 0U);
	}

	(void)memset(hc, 0, sizeof(*hc));
}

int dwc_mmc_reset(struct dwc_mmc *hc)
{
	if ((hc == NULL) || (!hc->ready)) {
		return DWC_MMC_ERR_NOT_READY;
	}

	if (dwc_mmc_soft_reset(hc) != DWC_MMC_OK) {
		return DWC_MMC_ERR_TIMEOUT;
	}

	dwc_mmc_reprogram(hc);

	/* A controller reset clears the clock and the bus width along with
	 * everything else, so both are put back: a caller that resets to recover
	 * must not be left talking to the card at 400 kHz over one line. */
	if (hc->clock_hz != 0U) {
		(void)dwc_mmc_set_clock(hc, hc->clock_hz);
	}
	dwc_mmc_set_bus_width(hc, hc->width);

	return DWC_MMC_OK;
}

uint32_t dwc_mmc_set_clock(struct dwc_mmc *hc, uint32_t hz)
{
	uint32_t div = 0U;

	if (hc == NULL) {
		return 0U;
	}

	if (hz == 0U) {
		/* stop the card clock */
		uintptr_t base = hc->cfg.base;
		uint32_t bits = DWC_MMC_CMD_UPD_CLK | DWC_MMC_CMD_WAIT_PRV_DAT;

		mmio_wr32(base + DWC_MMC_CLKENA, 0U);
		(void)dwc_mmc_clock_step(base, bits);
		return 0U;
	}

	dwc_mmc_select_clock(hc, hz, &div);

	if (dwc_mmc_clock_update(hc, div) != DWC_MMC_OK) {
		LOG_ERROR("dwc_mmc: clock update failed (target=%u div=%u)\n", hz, div);
		return 0U;
	}

	/* let the card see a settled clock before the next command */
	osDelay(1U);

	hc->clock_hz = hz;

	return (div == 0U) ? hc->src_hz : (hc->src_hz / (2U * div));
}

void dwc_mmc_set_bus_width(struct dwc_mmc *hc, unsigned int width)
{
	uint32_t ctype = DWC_MMC_CTYPE_1BIT;

	/*
	 * width is a count of signal lines - 1, 4 or 8 - not a width
	 * enumeration. A caller that hands over a 0/1/2 enumeration lands every
	 * four-bit request in the one-bit branch, which leaves the controller
	 * talking one-bit to a four-bit card: direct commands still work and every
	 * block transfer fails its data CRC. The mapping belongs to the caller;
	 * this function only encodes what it is given.
	 */
	if (width == 8U) {
		ctype = DWC_MMC_CTYPE_8BIT;
	} else if (width == 4U) {
		ctype = DWC_MMC_CTYPE_4BIT;
	}

	hc->width = width;

	mmio_wr32(hc->cfg.base + DWC_MMC_CTYPE, ctype);
}

bool dwc_mmc_card_present(const struct dwc_mmc *hc)
{
	/* CDETECT bit 0 reads 0 while the slot is occupied */
	return (mmio_rd32(hc->cfg.base + DWC_MMC_CDETECT) & 0x1U) == 0U;
}

int dwc_mmc_transfer(struct dwc_mmc *hc, struct dwc_mmc_cmd *cmd)
{
	int ret;

	if ((hc == NULL) || (cmd == NULL)) {
		return DWC_MMC_ERR_ARG;
	}

	if (!hc->ready) {
		return DWC_MMC_ERR_NOT_READY;
	}

	if (!dwc_mmc_card_present(hc)) {
		return DWC_MMC_ERR_NO_CARD;
	}

	if (cmd->data != NULL) {
		ret = dwc_mmc_transfer_data(hc, cmd);
	} else {
		ret = dwc_mmc_send_command(hc, cmd);
	}

	/*
	 * A failed transfer leaves the controller's data and command state machines
	 * armed. Left alone, the next command times out for a reason that has
	 * nothing to do with itself, and the failure's real cause is buried under a
	 * pile of follow-on timeouts - measured on this board: one refused CMD53
	 * turned every later command into a timeout. The reset puts the controller
	 * back in a known state so the next transfer's verdict is about itself.
	 */
	if (ret != DWC_MMC_OK) {
		LOG_ERROR("dwc_mmc: CMD%u failed (%d); resetting the controller\n",
			  cmd->index, ret);
		(void)dwc_mmc_reset(hc);
	}

	return ret;
}
