/*
 * @file   board_conf.h
 * @brief  RK3568-itx board calibration: every register base, interrupt number
 *         and hardware-behaviour constant in one place.
 *
 * DATA ONLY - no declarations, no inline functions - so assembly files can
 * include it too (.S files run the preprocessor but cannot digest function
 * bodies).
 *
 * Every value here was established by experiment on this board unless a
 * comment says otherwise; where a wrong value fails silently rather than
 * loudly, the failure mode is recorded next to the value.
 */

#ifndef FREEWIFICAM_RK3568_BOARD_CONF_H
#define FREEWIFICAM_RK3568_BOARD_CONF_H

/* --- register bases ------------------------------------------------------- */

#define BOARD_UART2_BASE	0xfe660000UL	/* console UART2 */
#define BOARD_GICD_BASE		0xfd400000UL
#define BOARD_GICR_BASE		0xfd460000UL	/* core 0 redistributor */

/* Console UART calibration (DW-APB 16550-compatible: 32-bit access, register
 * byte-offset stride 4 - byte accesses read and write garbage). */
#define BOARD_UART_BASE		BOARD_UART2_BASE
#define BOARD_UART_CLOCK_HZ	24000000UL
#define BOARD_UART_BAUD		115200UL

/* --- interrupts ------------------------------------------------------------ */

/* GIC INTID of the console UART (SPI 118 + 32). This image keeps the console
 * polled (IER=0); the number is recorded for the rounds that enable it. */
#define BOARD_CONSOLE_INTID	150U

/* Tick source: EL1 virtual timer (CNTV), PPI 27. Board-proven on this board:
 * writing CNTV_TVAL raises GICR_ISPENDR0 bit 27 and INTID 27 arrives. The
 * alternative CNTP (ns physical, PPI 30) is kept as a build option
 * (CONFIG_TICK_SOURCE, R2); PPI 29 is the SECURE physical timer held by
 * OP-TEE as Group 0 - a different line entirely. */
#define BOARD_TICK_INTID	27U

/* GIC-600 redistributor frame stride: a redistributor is a pair of 64KiB
 * frames, so consecutive cores' RD_base frames are 2 * 64KiB apart. */
#define BOARD_GICR_STRIDE	0x20000UL

/* Tick interrupt priority, RAW 8-bit GIC form: logical level 14 of the 16
 * unique-priority scheme shifted into the top nibble (14 << 4) - the lowest
 * USABLE level (level 15 / 0xf0 is unsignable on this GIC's four-bit PMR
 * comparison; the prior image measured this and stayed at 0xe0). Lower raw
 * value = more urgent. */
#define BOARD_TICK_PRIORITY_RAW	0xe0u

/* N7-stab tick monitor (2026-10-02): the EL1 non-secure PHYSICAL timer as an
 * independent watchdog above every other line in this image. PPI 30 - the
 * virtual timer (PPI 27) is the kernel tick, and PPI 29 is OP-TEE's secure
 * timer. 0x60 is strictly more urgent than the GMAC line (0xa0) and the tick
 * (0xe0), so the monitor always gets a window even while a GMAC interrupt
 * storm runs (each ERET lets the GIC pick the highest-priority pending). */
#define BOARD_TICK_MON_INTID		30U
#define BOARD_TICK_MON_PRIORITY_RAW	0x60u

/* Distributor default SPI priority (mid). Callers override per interrupt. */
#define BOARD_SPI_DEFAULT_PRIORITY_RAW	0xa0u

/* Priority for handlers that call a kernel FromISR API, RAW 8-bit GIC form:
 * logical level 11 in the top nibble. Lower raw = more urgent, so a line left
 * at the SPI default (0xa0) sits ABOVE this ceiling and a handler that wakes a
 * waiter trips the port's own priority assertion. The PCIe completion handlers
 * (LPI property bytes) use this level. */
#define BOARD_IRQ_PRIORITY_API_CALL_RAW	0xb0u

/* --- GICv3 ITS (message interrupts for PCIe endpoints) --------------------- */

/* GIC-600 ITS register file. An endpoint's MSI is a posted write to the ITS
 * GITS_TRANSLATER register inside this frame; the ITS driver owns that
 * register's offset and hands the resulting address to the MSI domain, so the
 * only fact this table carries is the frame's base. */
#define BOARD_ITS_BASE		0xfd440000UL

/* --- PCIe controllers (DesignWare root complex) ---------------------------- */

/* Two controllers. DBI frames sit above 4GiB and are mapped Device by mmu.c;
 * the MEM windows are where firmware assigned the endpoint BARs (the NVMe's
 * BAR0 lands at 0xf4300000, inside pcie2x1's window). Both keep the
 * firmware-trained link - this line does not bring a link up itself. */
#define BOARD_PCIE2X1_APB_BASE	0xfe260000UL	/* M.2 slot: the NVMe */
#define BOARD_PCIE2X1_DBI_BASE	0x3c0000000ULL
#define BOARD_PCIE2X1_CFG_BASE	0xf4000000ULL
#define BOARD_PCIE2X1_MEM_BASE	0xf4200000ULL
#define BOARD_PCIE2X1_MEM_SIZE	0x1e00000ULL	/* 30 MiB */

#define BOARD_PCIE3X2_APB_BASE	0xfe280000UL	/* x4 slot: the 7260 */
#define BOARD_PCIE3X2_DBI_BASE	0x3c0800000ULL
#define BOARD_PCIE3X2_CFG_BASE	0xf0000000ULL
#define BOARD_PCIE3X2_MEM_BASE	0xf0200000ULL
#define BOARD_PCIE3X2_MEM_SIZE	0x1e00000ULL	/* 30 MiB */

/* Config viewport: one bus's worth of config space (32 dev x 8 fn x 4 KiB). */
#define BOARD_PCIE_CFG_SIZE	0x100000ULL

/* MSI doorbell page the inbound iATU must forward: it is the page containing
 * the ITS GITS_TRANSLATER register (BOARD_ITS_BASE + 0x10000). The card's DMA
 * buffers and this page are the two inbound addresses an endpoint needs. */
#define BOARD_PCIE_DOORBELL_BASE	0xfd450000ULL
#define BOARD_PCIE_DOORBELL_SIZE	0x1000ULL

/* --- core identity ---------------------------------------------------------- */

/* HOW A CORE'S LOGICAL NUMBER IS EXTRACTED FROM MPIDR_EL1.
 *
 * Measured on this board: MPIDR_EL1 = 0x8100_0N00 with N = 0..3 - the logical
 * core number lives in AFFINITY 1, and Aff0 is 0 for every core. Aff0-based
 * extraction makes every core believe it is core 0. */
#define BOARD_MPIDR_CORE_SHIFT	8

/* --- MMU layout (consumed by arch/aarch64/mmu.c) ----------------------------- */

/* Image RAM base: below this is firmware (ATF/OP-TEE) and must stay Device
 * (never cached, never speculated, never executed). */
#define BOARD_MMU_IMAGE_RAM_BASE	0x0a000000ULL

/* Usable RAM window top: touching anything at or above this raises an
 * external abort on this board (the image's linker-script ASSERT enforces
 * the same number). */
#define BOARD_RAM_WINDOW_TOP	0x10000000ULL

/* TCR_EL1 physical address size: PS=0b010 selects 40-bit PA, which is what
 * this SoC implements (a larger IPS than the hardware supports is
 * UNPREDICTABLE). */
#define BOARD_MMU_TCR_PS	(2ULL << 32)

#endif /* FREEWIFICAM_RK3568_BOARD_CONF_H */
