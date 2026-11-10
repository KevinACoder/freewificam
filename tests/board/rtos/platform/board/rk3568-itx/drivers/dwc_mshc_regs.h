/*
 * @file   dwc_mshc_regs.h
 * @brief  SDHCI register map and bit definitions, the DWC MSHC vendor area,
 *         and this SoC's CRU/GRF coordinates for the eMMC controller.
 *
 * The SDHCI fields agree with the SD Host Controller specification (the IP
 * implements 4.20a) and with the two drivers that run this same controller on
 * this board: u-boot's drivers/mmc/sdhci.c + rockchip_sdhci.c and the kernel's
 * sdhci-of-dwcmshc.c. The CRU/GRF coordinates were cross-checked against the
 * older standalone BSP driver for this controller and against the kernel's
 * clock and pinctrl code for this SoC; where a value is not double-sourced it
 * is called out at the field.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 *
 * Register and field values derived from the board lab's standalone eMMC
 * controller driver (BSD-3-Clause, commit 45d03100 of that tree), rewritten
 * against this project's mmio/log/cache services.
 */

#ifndef FREEWIFICAM_DWC_MSHC_REGS_H
#define FREEWIFICAM_DWC_MSHC_REGS_H

#include <stdint.h>

/* --- SDHCI register map (offsets from the controller base 0xfe310000) ------ */

#define DWC_MSHC_SDMA_ADDR		0x000U	/* SDMA system address */
#define DWC_MSHC_BLOCK_SIZE		0x004U	/* 16-bit */
#define DWC_MSHC_BLOCK_COUNT		0x006U	/* 16-bit */
#define DWC_MSHC_ARGUMENT		0x008U
#define DWC_MSHC_XFER_MODE		0x00cU	/* 16-bit */
#define DWC_MSHC_COMMAND		0x00eU	/* 16-bit */
#define DWC_MSHC_RESP01			0x010U
#define DWC_MSHC_RESP23			0x014U
#define DWC_MSHC_RESP45			0x018U
#define DWC_MSHC_RESP67			0x01cU
#define DWC_MSHC_BUF_DATA		0x020U
#define DWC_MSHC_PRESENT_STATE		0x024U
#define DWC_MSHC_HOST_CTRL1		0x028U	/* 8-bit */
#define DWC_MSHC_POWER_CTRL		0x029U	/* 8-bit */
#define DWC_MSHC_BLOCK_GAP_CTRL		0x02aU	/* 8-bit */
#define DWC_MSHC_CLOCK_CTRL		0x02cU	/* 16-bit */
#define DWC_MSHC_TIMEOUT_CTRL		0x02eU	/* 8-bit */
#define DWC_MSHC_SW_RESET		0x02fU	/* 8-bit */
#define DWC_MSHC_NORMAL_INT_STAT	0x030U	/* 16-bit */
#define DWC_MSHC_ERROR_INT_STAT		0x032U	/* 16-bit */
#define DWC_MSHC_NORMAL_INT_EN		0x034U	/* 16-bit */
#define DWC_MSHC_ERROR_INT_EN		0x036U	/* 16-bit */
#define DWC_MSHC_NORMAL_INT_EN_SIG	0x038U	/* 16-bit */
#define DWC_MSHC_ERROR_INT_EN_SIG	0x03aU	/* 16-bit */
#define DWC_MSHC_AUTO_CMD_STAT		0x03cU	/* 16-bit */
#define DWC_MSHC_HOST_CTRL2		0x03eU	/* 16-bit */
#define DWC_MSHC_CAPABILITIES1		0x040U
#define DWC_MSHC_CAPABILITIES2		0x044U
#define DWC_MSHC_ADMA_ERR_STAT		0x054U	/* 8-bit */
#define DWC_MSHC_HOST_VERSION		0x0feU	/* 16-bit */

/* The DWC MSHC vendor area. Offsets agreed by u-boot's rockchip_sdhci.c and the
 * kernel's sdhci-of-dwcmshc.c. */
#define DWC_MSHC_HOST_CTRL3		0x508U	/* bit0: command-conflict check */
#define DWC_MSHC_EMMC_CTRL		0x52cU	/* bit0: CARD_IS_EMMC */
#define DWC_MSHC_AT_CTRL		0x540U	/* auto-tuning delays (the tuning round) */
#define DWC_MSHC_MISC_CON		0x81cU	/* bit1: internal clock enable */
#define DWC_MSHC_DLL_CTRL		0x800U
#define DWC_MSHC_DLL_RXCLK		0x804U
#define DWC_MSHC_DLL_TXCLK		0x808U
#define DWC_MSHC_DLL_STRBIN		0x80cU
#define DWC_MSHC_DLL_CMDOUT		0x810U
#define DWC_MSHC_DLL_STATUS0		0x840U

/* --- XFER_MODE (16-bit) --------------------------------------------------- */

#define DWC_MSHC_XFER_DMA_EN		(1U << 0)
#define DWC_MSHC_XFER_BLK_CNT_EN	(1U << 1)
#define DWC_MSHC_XFER_AUTO_CMD_EN_MASK	(3U << 2)	/* 1 = CMD12, 2 = CMD23 */
#define DWC_MSHC_XFER_DATA_DIR_READ	(1U << 4)
#define DWC_MSHC_XFER_MULTI_BLK		(1U << 5)

/* The SDMA boundary the controller stops its engine at, as the BLOCK_SIZE
 * register's [14:12] field: 7 selects 512 Kbytes. The driver honors the stop
 * instead of programming a larger boundary, because the engine's stop is what
 * makes an unaligned transfer address visible (see dwc_mshc_dma.h). */
#define DWC_MSHC_BLKSZ_SDMA_BOUNDARY_512K	(7U << 12)

/* --- COMMAND (16-bit) ----------------------------------------------------- */

#define DWC_MSHC_CMD_INDEX_SHIFT	8U
#define DWC_MSHC_CMD_RESP_NONE		0U
#define DWC_MSHC_CMD_RESP_136		(1U << 0)
#define DWC_MSHC_CMD_RESP_48		(1U << 1)
#define DWC_MSHC_CMD_CRC_CHECK		(1U << 3)
#define DWC_MSHC_CMD_INDEX_CHECK	(1U << 4)
#define DWC_MSHC_CMD_DATA_PRESENT	(1U << 5)

/* --- PRESENT_STATE -------------------------------------------------------- */

#define DWC_MSHC_PS_CMD_INHIBIT		(1U << 0)
#define DWC_MSHC_PS_DAT_INHIBIT		(1U << 1)
#define DWC_MSHC_PS_CARD_INSERTED	(1U << 16)
#define DWC_MSHC_PS_DAT_0_LEVEL		(1U << 20)	/* 0 = card holding DAT0 busy */

/* --- HOST_CTRL1 (8-bit) --------------------------------------------------- */

#define DWC_MSHC_HC1_LED_POWER		(1U << 0)
#define DWC_MSHC_HC1_DAT_XFER_WIDTH	(1U << 1)	/* 4-bit bus */
#define DWC_MSHC_HC1_HIGH_SPEED		(1U << 2)
#define DWC_MSHC_HC1_DMA_SEL_MASK	(3U << 3)
#define DWC_MSHC_HC1_DMA_SEL_SDMA	(0U << 3)
#define DWC_MSHC_HC1_DMA_SEL_ADMA2	(2U << 3)
#define DWC_MSHC_HC1_EXT_DAT_WIDTH	(1U << 5)	/* 8-bit bus */

/* --- POWER_CTRL (8-bit): bus voltage in [3:1], bus power in bit 0 ---------- */

#define DWC_MSHC_PWR_BUS_VOLTAGE_3_3V	(7U << 1)
#define DWC_MSHC_PWR_BUS_POWER		(1U << 0)

/* --- CLOCK_CTRL (16-bit) -------------------------------------------------- */

#define DWC_MSHC_CLK_INTERNAL_EN	(1U << 0)
#define DWC_MSHC_CLK_INTERNAL_STABLE	(1U << 1)
#define DWC_MSHC_CLK_SD_EN		(1U << 2)
#define DWC_MSHC_CLK_FREQ_SEL(x)	(((uint32_t)(x) & 0xffu) << 8)

/* --- SW_RESET (8-bit) ----------------------------------------------------- */

#define DWC_MSHC_SW_RESET_ALL		(1U << 0)
#define DWC_MSHC_SW_RESET_CMD		(1U << 1)
#define DWC_MSHC_SW_RESET_DAT		(1U << 2)

/* --- NORMAL_INT_STAT / _EN (16-bit) --------------------------------------- */

#define DWC_MSHC_INT_CMD_COMPLETE	(1U << 0)
#define DWC_MSHC_INT_XFER_COMPLETE	(1U << 1)
#define DWC_MSHC_INT_BLOCK_GAP		(1U << 2)
#define DWC_MSHC_INT_DMA			(1U << 3)	/* SDMA boundary reached */
#define DWC_MSHC_INT_BUF_WR_READY	(1U << 4)
#define DWC_MSHC_INT_BUF_RD_READY	(1U << 5)
#define DWC_MSHC_INT_CARD_INSERTION	(1U << 6)
#define DWC_MSHC_INT_CARD_REMOVAL	(1U << 7)
#define DWC_MSHC_INT_CARD_INTERRUPT	(1U << 8)
#define DWC_MSHC_INT_ERROR		(1U << 15)
#define DWC_MSHC_INT_ALL		0xffffU

/* --- ERROR_INT_STAT / _EN (16-bit) ---------------------------------------- */

#define DWC_MSHC_EINT_CMD_TIMEOUT	(1U << 0)
#define DWC_MSHC_EINT_CMD_CRC		(1U << 1)
#define DWC_MSHC_EINT_CMD_END_BIT	(1U << 2)
#define DWC_MSHC_EINT_CMD_INDEX		(1U << 3)
#define DWC_MSHC_EINT_DATA_TIMEOUT	(1U << 4)
#define DWC_MSHC_EINT_DATA_CRC		(1U << 5)
#define DWC_MSHC_EINT_DATA_END_BIT	(1U << 6)
#define DWC_MSHC_EINT_CURRENT_LIMIT	(1U << 7)
#define DWC_MSHC_EINT_AUTO_CMD		(1U << 8)
#define DWC_MSHC_EINT_ADMA		(1U << 9)
#define DWC_MSHC_EINT_TUNING		(1U << 10)
#define DWC_MSHC_EINT_RESP		(1U << 11)
#define DWC_MSHC_EINT_ALL		0xffffU

/* The two families a command response and a data stage fail in. */
#define DWC_MSHC_EINT_CMD_ERRORS \
	(DWC_MSHC_EINT_CMD_TIMEOUT | DWC_MSHC_EINT_CMD_CRC | \
	 DWC_MSHC_EINT_CMD_END_BIT | DWC_MSHC_EINT_CMD_INDEX | DWC_MSHC_EINT_RESP)
#define DWC_MSHC_EINT_DATA_ERRORS \
	(DWC_MSHC_EINT_DATA_TIMEOUT | DWC_MSHC_EINT_DATA_CRC | \
	 DWC_MSHC_EINT_DATA_END_BIT | DWC_MSHC_EINT_ADMA)

/* --- HOST_CTRL2 (16-bit) -------------------------------------------------- */

#define DWC_MSHC_HC2_UHS_MASK		0x7U
#define DWC_MSHC_HC2_UHS_SDR25		1U	/* the field u-boot and the kernel
						 * use for both UHS SDR25 and eMMC
						 * high speed */
#define DWC_MSHC_HC2_UHS_SDR104		3U
#define DWC_MSHC_HC2_SIGNALING_1_8V	(1U << 3)
#define DWC_MSHC_HC2_EXEC_TUNING	(1U << 6)
#define DWC_MSHC_HC2_TUNED_CLK		(1U << 7)

/* --- CAPABILITIES1: the bits this driver reads back as evidence ----------- */

#define DWC_MSHC_CAP1_8BIT		(1U << 18)
#define DWC_MSHC_CAP1_ADMA2		(1U << 19)
#define DWC_MSHC_CAP1_HIGH_SPEED	(1U << 21)
#define DWC_MSHC_CAP1_SDMA		(1U << 22)

/* --- DLL (vendor area; the bypass path this round programs) --------------- */

#define DWC_MSHC_DLL_START		(1U << 0)
#define DWC_MSHC_DLL_RESET		(1U << 1)
#define DWC_MSHC_DLL_BYPASS		(1U << 24)
#define DWC_MSHC_DLL_DLYENA		(1U << 27)
#define DWC_MSHC_DLL_RXCLK_ORI_GATE	(1U << 31)
#define DWC_MSHC_DLL_STRBIN_DELAY_EN	(1U << 27)
#define DWC_MSHC_DLL_STRBIN_DELAY_SEL	(1U << 26)
#define DWC_MSHC_DLL_STRBIN_DELAY_SHIFT	16U
/* The bypass path's strobe-in delay, u-boot's ddr50_strbin_delay_num for this
 * SoC. */
#define DWC_MSHC_DLL_STRBIN_DELAY_16	16U
#define DWC_MSHC_DLL_STATUS_LOCKED	(1U << 8)
#define DWC_MSHC_DLL_STATUS_TIMEOUT	(1U << 9)

#define DWC_MSHC_MISC_INTCLK_EN		(1U << 1)

/* --- this SoC's CRU (base 0xfdd20000; hiword write-enable) ---------------- */

/* clksel_con[28]: CCLK_EMMC in [14:12], BCLK_EMMC in [9:8]. */
#define DWC_MSHC_CRU_BASE		0xfdd20000UL
#define DWC_MSHC_CRU_CLKSEL_CON28	0x170U
#define DWC_MSHC_CRU_CLKGATE_CON9	0x324U
#define DWC_MSHC_CRU_SOFTRST_CON7	0x41cU

#define DWC_MSHC_CRU_CCLK_MUX_MASK	(7U << 12)
#define DWC_MSHC_CRU_CCLK_MUX_SHIFT	12U
#define DWC_MSHC_CRU_BCLK_MUX_MASK	(3U << 8)
#define DWC_MSHC_CRU_BCLK_MUX_SHIFT	8U

/* CCLK_EMMC sources, in selector order. The parent list is the kernel's for
 * this clock - {xin24m, gpll_200m, gpll_150m, cpll_100m, cpll_50m,
 * clk_osc0_div_375k} - and the kernel's mux for it carries no divider, so the
 * selected parent's rate IS the rate the controller's own divider divides.
 * The two parents this round selects are the fixed ones, whose rate does not
 * depend on the boot firmware's PLL programming: 24 MHz and the cpll-derived
 * 50 MHz slot (both the older standalone driver for this controller and the
 * earlier Embox port on this board report 50 MHz at slot 4). Slot 1 is the
 * gpll-derived 200 MHz slot: its achieved rate follows gpll (the next round's
 * subject), so nothing in this round's clock table depends on it. */
#define DWC_MSHC_CRU_CCLK_SEL_24M	0U
#define DWC_MSHC_CRU_CCLK_SEL_200M	1U
#define DWC_MSHC_CRU_CCLK_SEL_150M	2U
#define DWC_MSHC_CRU_CCLK_SEL_100M	3U
#define DWC_MSHC_CRU_CCLK_SEL_50M	4U	/* 50 MHz */
#define DWC_MSHC_CRU_CCLK_SEL_375K	5U	/* 375 kHz */

#define DWC_MSHC_CRU_BCLK_SEL_200M	0U
#define DWC_MSHC_CRU_BCLK_SEL_150M	1U
#define DWC_MSHC_CRU_BCLK_SEL_125M	2U

/* clkgate_con[9]: ACLK/HCLK/BCLK/CCLK/TCLK_EMMC in bits 5..9. A gate bit reads
 * 1 when the clock is off, so enabling means writing 0 into it. */
#define DWC_MSHC_CRU_EMMC_GATE_ALL	(0x1fu << 5)

/* softrst_con[7]: SRST_A/H/B/C/T_EMMC in bits 5..9. A reset is asserted by
 * writing 1 and released by writing 0. */
#define DWC_MSHC_CRU_EMMC_SOFTRST_ALL	(0x1fu << 5)

/* --- this SoC's GRF (base 0xfdc60000; 4-bit iomux fields, 2-bit pulls,
 *     8-bit drive strength, all with the hiword write-enable) --------------- */

#define DWC_MSHC_GRF_BASE		0xfdc60000UL

/* GPIO1 iomux: bank 1's own base is 0x0000, words are per four pins and the
 * register covers eight (the kernel's rk3568 iomux layout). */
#define DWC_MSHC_GRF_GPIO1B_IOMUX_H	0x00cU	/* B4..B7 */
#define DWC_MSHC_GRF_GPIO1C_IOMUX_L	0x010U	/* C0..C3 */
#define DWC_MSHC_GRF_GPIO1C_IOMUX_H	0x014U	/* C4..C7 */

#define DWC_MSHC_GRF_GPIO1B_PULL	0x084U	/* B0..B7, 2 bits per pin */
#define DWC_MSHC_GRF_GPIO1C_PULL	0x088U	/* C0..C7, 2 bits per pin */

#define DWC_MSHC_GRF_GPIO1B_DS_2	0x218U	/* B4, B5 */
#define DWC_MSHC_GRF_GPIO1B_DS_3	0x21cU	/* B6, B7 */
#define DWC_MSHC_GRF_GPIO1C_DS_0	0x220U	/* C0, C1 */
#define DWC_MSHC_GRF_GPIO1C_DS_1	0x224U	/* C2, C3 */
#define DWC_MSHC_GRF_GPIO1C_DS_2	0x228U	/* C4, C5 */
#define DWC_MSHC_GRF_GPIO1C_DS_3	0x22cU	/* C6, C7 */

#define DWC_MSHC_GRF_IOMUX_FN1		0x1U	/* each pin's 4-bit field */
#define DWC_MSHC_GRF_PULL_UP		0x1U	/* each pin's 2-bit field */
#define DWC_MSHC_GRF_DS_LEVEL2		0x7U	/* each pin's 8-bit field */
#define DWC_MSHC_GRF_DS_ENABLE		0x3fu	/* the field's enable bits */

#endif /* FREEWIFICAM_DWC_MSHC_REGS_H */
