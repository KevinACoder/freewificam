/*
 * @file   dwc_mmc_regs.h
 * @brief  DesignWare Mobile Storage Host register map and bit definitions,
 *         RK3568 sdmmc0/sdmmc1.
 *
 * The controller bit definitions agree with the SoC's TRM (part 2, chapter 6)
 * and with u-boot's drivers/mmc/dw_mmc.c and the kernel's dw_mmc.h; the CRU
 * coordinates below are this SoC's. They are kept beside the driver they belong
 * to, because the register sequences in dwc_mmc.c are only meaningful against
 * these exact fields.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 */

#ifndef FREEWIFICAM_DWC_MMC_REGS_H
#define FREEWIFICAM_DWC_MMC_REGS_H

#include <stdint.h>

/* Rockchip CRU/GRF registers use a hiword write-enable: a write carries
 * ((mask & 0xffff) << 16) | (val & mask), so a field is changed without
 * disturbing its neighbours. */
#define DWC_MMC_CRU_BASE			0xfdd20000UL

#define DWC_MMC_CRU_CLKSEL_CON			0x178U	/* clksel_con[30] */
#define DWC_MMC_CRU_GATE_CON			0x33cU	/* clkgate_con[15] */
#define DWC_MMC_CRU_SOFTRST_CON			0x434U	/* softrst_con[13] */

#define DWC_MMC_CRU_SDMMC0_MUX_MASK		0x700U
#define DWC_MMC_CRU_SDMMC0_MUX_SHIFT		8U
#define DWC_MMC_CRU_SDMMC1_MUX_MASK		0x7000U
#define DWC_MMC_CRU_SDMMC1_MUX_SHIFT		12U

#define DWC_MMC_CRU_SDMMC0_HCLK_GATE		(1u << 0)
#define DWC_MMC_CRU_SDMMC0_CLK_GATE		(1u << 1)
#define DWC_MMC_CRU_SDMMC1_HCLK_GATE		(1u << 2)
#define DWC_MMC_CRU_SDMMC1_CLK_GATE		(1u << 3)

#define DWC_MMC_CRU_SDMMC0_SOFTRST		(1u << 4)
#define DWC_MMC_CRU_SDMMC1_SOFTRST		(1u << 6)

/* CLK_SDMMCx source selection (clksel_con[30]). */
#define DWC_MMC_CRU_SEL_24M			0U
#define DWC_MMC_CRU_SEL_400M			1U
#define DWC_MMC_CRU_SEL_300M			2U
#define DWC_MMC_CRU_SEL_100M			3U
#define DWC_MMC_CRU_SEL_50M			4U
#define DWC_MMC_CRU_SEL_750K			5U

/* --- register map (offsets from the controller base) ---------------------- */

#define DWC_MMC_CTRL				0x000U
#define DWC_MMC_PWREN				0x004U
#define DWC_MMC_CLKDIV				0x008U
#define DWC_MMC_CLKSRC				0x00cU
#define DWC_MMC_CLKENA				0x010U
#define DWC_MMC_TMOUT				0x014U
#define DWC_MMC_CTYPE				0x018U
#define DWC_MMC_BLKSIZ				0x01cU
#define DWC_MMC_BYTCNT				0x020U
#define DWC_MMC_INTMASK				0x024U
#define DWC_MMC_CMDARG				0x028U
#define DWC_MMC_CMD				0x02cU
#define DWC_MMC_RESP0				0x030U
#define DWC_MMC_RESP1				0x034U
#define DWC_MMC_RESP2				0x038U
#define DWC_MMC_RESP3				0x03cU
#define DWC_MMC_MINTSTS				0x040U
#define DWC_MMC_RINTSTS				0x044U
#define DWC_MMC_STATUS				0x048U
#define DWC_MMC_FIFOTH				0x04cU
#define DWC_MMC_CDETECT				0x050U
#define DWC_MMC_WRTPRT				0x054U
#define DWC_MMC_DEBNCE				0x064U
#define DWC_MMC_USRID				0x068U
#define DWC_MMC_VERID				0x06cU
#define DWC_MMC_HCON				0x070U
#define DWC_MMC_UHSREG				0x074U
#define DWC_MMC_RSTN				0x078U
#define DWC_MMC_BMOD				0x080U
#define DWC_MMC_PLDMND				0x084U
#define DWC_MMC_DBADDR				0x088U
#define DWC_MMC_IDSTS				0x08cU
#define DWC_MMC_IDINTEN				0x090U
#define DWC_MMC_DSCADDR				0x094U
#define DWC_MMC_BUFADDR				0x098U
#define DWC_MMC_CARDTHRCTL			0x100U

/* Rockchip clock-phase registers (dw_mmc-rockchip's SDMMC_TIMING_CON0/1): the
 * card clock's drive and sample phases, HIWORD-encoded like the CRU (value
 * bits [10:1], write-enable bits [26:17]). The vendor kernel's set_ios writes
 * drive 90 degrees and sample 0 degrees, and rk356x.dtsi carries no default
 * sample phase. */
#define DWC_MMC_TIMING_CON0			0x130U
#define DWC_MMC_TIMING_CON1			0x134U
#define DWC_MMC_TIMING_HIWORD(raw) \
	((((uint32_t)(raw) & 0x7ffu) << 1) | (0x7ffu << 17))
#define DWC_MMC_TIMING_DRIVE_90			DWC_MMC_TIMING_HIWORD(1u)
#define DWC_MMC_TIMING_SAMPLE_0			DWC_MMC_TIMING_HIWORD(0u)

/* --- CTRL ----------------------------------------------------------------- */

#define DWC_MMC_CTRL_CONTROLLER_RESET		(1u << 0)
#define DWC_MMC_CTRL_FIFO_RESET			(1u << 1)
#define DWC_MMC_CTRL_DMA_RESET			(1u << 2)
#define DWC_MMC_CTRL_INT_ENABLE			(1u << 4)
#define DWC_MMC_CTRL_DMA_ENABLE			(1u << 5)
#define DWC_MMC_CTRL_READ_WAIT			(1u << 6)
#define DWC_MMC_CTRL_USE_IDMAC			(1u << 25)
#define DWC_MMC_CTRL_ALL_RESET_FLAGS \
	(DWC_MMC_CTRL_CONTROLLER_RESET | DWC_MMC_CTRL_FIFO_RESET | \
	 DWC_MMC_CTRL_DMA_RESET)

/* --- CMD (TRM 6.4.2, index in [5:0]) -------------------------------------- */

#define DWC_MMC_CMD_INDX(n)			((n) & 0x3fu)
#define DWC_MMC_CMD_RESP_EXP			(1u << 6)
#define DWC_MMC_CMD_RESP_LENGTH			(1u << 7)
#define DWC_MMC_CMD_CHECK_CRC			(1u << 8)
#define DWC_MMC_CMD_DATA_EXP			(1u << 9)
#define DWC_MMC_CMD_RW				(1u << 10)
#define DWC_MMC_CMD_SEND_AUTO_STOP		(1u << 12)
#define DWC_MMC_CMD_WAIT_PRV_DAT		(1u << 13)
#define DWC_MMC_CMD_STOP_ABORT			(1u << 14)
#define DWC_MMC_CMD_SEND_INIT			(1u << 15)
#define DWC_MMC_CMD_UPD_CLK			(1u << 21)
#define DWC_MMC_CMD_VOLT_SWITCH			(1u << 28)
#define DWC_MMC_CMD_USE_HOLD_REG		(1u << 29)
#define DWC_MMC_CMD_START			(1u << 31)

/* --- UHSREG (1.8 V signal level) ------------------------------------------ */

#define DWC_MMC_UHSREG_18V			(1u << 0)

/* --- RINTSTS / INTMASK ---------------------------------------------------- */

#define DWC_MMC_INT_CD				(1u << 0)	/* card detect */
#define DWC_MMC_INT_RE				(1u << 1)	/* response error */
#define DWC_MMC_INT_CDONE			(1u << 2)	/* command done */
#define DWC_MMC_INT_DTO				(1u << 3)	/* data transfer over */
#define DWC_MMC_INT_TXDR			(1u << 4)	/* tx fifo data request */
#define DWC_MMC_INT_RXDR			(1u << 5)	/* rx fifo data request */
#define DWC_MMC_INT_RCRC			(1u << 6)	/* response crc error */
#define DWC_MMC_INT_DCRC			(1u << 7)	/* data crc error */
#define DWC_MMC_INT_RTO				(1u << 8)	/* response timeout */
#define DWC_MMC_INT_DRTO			(1u << 9)	/* data read timeout */
#define DWC_MMC_INT_HTO				(1u << 10)	/* data starvation */
#define DWC_MMC_INT_FRUN			(1u << 11)	/* fifo under/overrun */
#define DWC_MMC_INT_HLE				(1u << 12)	/* locked write error */
#define DWC_MMC_INT_SBE				(1u << 13)	/* start bit error */
#define DWC_MMC_INT_ACD				(1u << 14)	/* auto command done */
#define DWC_MMC_INT_EBE				(1u << 15)	/* end bit error */
#define DWC_MMC_INT_SDIO			(1u << 16)	/* sdio interrupt */

#define DWC_MMC_INT_DATA_ERR \
	(DWC_MMC_INT_EBE | DWC_MMC_INT_SBE | DWC_MMC_INT_HLE | \
	 DWC_MMC_INT_FRUN | DWC_MMC_INT_DCRC | DWC_MMC_INT_RCRC)
#define DWC_MMC_INT_DATA_TOUT		(DWC_MMC_INT_HTO | DWC_MMC_INT_DRTO)
#define DWC_MMC_INT_CMD_ERR		(DWC_MMC_INT_RTO | DWC_MMC_INT_RE)
#define DWC_MMC_INT_ALL				0xffffffffU

/* --- STATUS --------------------------------------------------------------- */

#define DWC_MMC_STATUS_DATA_BUSY		(1u << 9)

/* --- FIFOTH (fifo depth 0x100: msize 6, rx watermark 0x7f, tx 0x80) -------- */

#define DWC_MMC_FIFOTH_MSIZE(x)			((x) << 28)
#define DWC_MMC_FIFOTH_RX_WMARK(x)		((x) << 16)
#define DWC_MMC_FIFOTH_TX_WMARK(x)		((x) & 0xffffu)

/* --- CLKENA --------------------------------------------------------------- */

#define DWC_MMC_CLKEN_ENABLE			(1u << 0)
#define DWC_MMC_CLKEN_LOW_PWR			(1u << 16)

/* --- CTYPE (card 0 field; this board drives one card) --------------------- */

#define DWC_MMC_CTYPE_1BIT			0u
#define DWC_MMC_CTYPE_4BIT			(1u << 0)
#define DWC_MMC_CTYPE_8BIT			(1u << 16)

/* --- IDMAC (BMOD / IDSTS / IDINTEN) --------------------------------------- */

#define DWC_MMC_IDMAC_SWRESET			(1u << 0)
#define DWC_MMC_IDMAC_FB			(1u << 1)
#define DWC_MMC_IDMAC_ENABLE			(1u << 7)

#define DWC_MMC_IDMAC_INT_TI			(1u << 0)	/* transmit */
#define DWC_MMC_IDMAC_INT_RI			(1u << 1)	/* receive */
#define DWC_MMC_IDMAC_INT_FBE			(1u << 2)	/* fatal bus error */
#define DWC_MMC_IDMAC_INT_DU			(1u << 4)	/* descriptor unavailable */
#define DWC_MMC_IDMAC_INT_CES			(1u << 5)	/* card error summary */
#define DWC_MMC_IDMAC_INT_NI			(1u << 8)	/* normal summary */
#define DWC_MMC_IDMAC_INT_AI			(1u << 9)	/* abnormal summary */

#define DWC_MMC_IDMAC_ERR_FLAGS \
	(DWC_MMC_IDMAC_INT_FBE | DWC_MMC_IDMAC_INT_DU | \
	 DWC_MMC_IDMAC_INT_CES | DWC_MMC_IDMAC_INT_AI)

/* One descriptor moves at most 4 Kbytes; the burst length field is 13 bits. */
#define DWC_MMC_DESC_MAX_LEN			0x1000U
#define DWC_MMC_DESC_CNT_MASK			0x1fffu

/* Descriptor control word (des0). */
#define DWC_MMC_DESC_DIC			(1u << 1)	/* no TI/RI on this one */
#define DWC_MMC_DESC_LD				(1u << 2)	/* last descriptor */
#define DWC_MMC_DESC_FD				(1u << 3)	/* first descriptor */
#define DWC_MMC_DESC_CH				(1u << 4)	/* chained */
#define DWC_MMC_DESC_ER				(1u << 5)	/* end of ring */
#define DWC_MMC_DESC_CES			(1u << 30)	/* card error summary */
#define DWC_MMC_DESC_OWN			(1u << 31)	/* owned by the IDMAC */

/* 32-bit IDMAC descriptor: control, buffer size, buffer address, next
 * descriptor address. */
struct dwc_mmc_desc {
	uint32_t flags;
	uint32_t cnt;
	uint32_t addr;
	uint32_t next;
} __attribute__((packed, aligned(4)));

/* The descriptor list the driver builds: 256 entries cover a 1 Mbyte transfer
 * at one 4 Kbyte descriptor each. */
#define DWC_MMC_DESC_NUM			256U

#endif /* FREEWIFICAM_DWC_MMC_REGS_H */
