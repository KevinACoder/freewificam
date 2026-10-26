/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   board_conf.h
 * @brief  firefly v1 board calibration: every register base,
 *         interrupt number and hardware-behaviour constant in one place.
 *
 * DATA ONLY - no declarations, no inline functions - so assembly files can
 * include it too (.S files run the preprocessor but cannot digest function
 * bodies), the same convention as the RK3568 board configuration.
 *
 * Directory contract (this repository's firefly family layout): the family's
 * generic code lives in board/firefly/ and consumes these macros through the
 * include path - the build puts board/firefly/v1 first, so "board_conf.h"
 * resolves to THIS file. firefly v2+ adds its own version directory with its
 * own register map; the generic code never changes.
 *
 * Fact provenance (kept next to each value): the reference standalone SDK's
 * SoC parameter header, the Linux device tree this board boots (kernel
 * 6.6.133) and the lab's measured U-Boot behaviour - the exact trees are
 * pinned outside this repository. Where the sources disagreed the value
 * actually proven on silicon is the one recorded here.
 */

#ifndef FREEWIFICAM_FIREFLY_V1_BOARD_CONF_H
#define FREEWIFICAM_FIREFLY_V1_BOARD_CONF_H

/* --- register bases ------------------------------------------------------- */

/* Console: UART1, an ARM PL011 at the U-Boot console address (DTS uart@28001000,
 * fparameters FUART1_BASE_ADDR; the lab profile's CONSOLE=ttyAMA1). Standard
 * PL011 byte offsets, 32-bit accesses (the standalone SDK's proven access
 * pattern on this silicon). 48 MHz uartclk (fparameters FUART1_CLK_FREQ_HZ,
 * DTS clock-names uartclk). */
#define BOARD_UART1_BASE	0x28001000UL
#define BOARD_UART_BASE		BOARD_UART1_BASE
#define BOARD_UART_CLOCK_HZ	48000000UL
#define BOARD_UART_BAUD		115200UL

/* GICv3 (DTS interrupt-controller reg: GICD 0x29a00000, GICR 0x29b00000;
 * fparameters agrees: GICV3_BASE 0x29a00000 + 0x100000 RD). One redistributor
 * pair is 128KiB (fparameters RD_OFFSET 2<<16). */
#define BOARD_GICD_BASE		0x29a00000UL
#define BOARD_GICR_BASE		0x29b00000UL	/* core 0 redistributor */
#define BOARD_GICR_STRIDE	0x20000UL

/* GICv3 ITS register frame (fparameters GICV3_ITS_BASE_ADDR; DTS gic-its reg
 * 0x29a20000). Reserved for the message-interrupt rounds; the ECAM
 * enumeration round keeps every endpoint polled. */
#define BOARD_ITS_BASE		0x29a20000UL

/* --- interrupts ------------------------------------------------------------ */

/* GIC INTID of the console UART (fparameters FUART1_IRQ_NUM 39). The console
 * stays polled (IMSC=0); the number is recorded for the rounds that enable
 * it. */
#define BOARD_CONSOLE_INTID	39U

/* Tick source: EL1 virtual timer (CNTV), PPI 27 - DTS "arm,armv8-timer"
 * lists PPI 11 (INTID 27) for the virtual timer. Board-proven by the GICv3
 * PPI bank on this class of timer (the line clears when TVAL reloads). */
#define BOARD_TICK_INTID	27U

/* Tick interrupt priority, RAW 8-bit GIC form: level 14 of 16 in the top
 * nibble - the lowest usable level under a 4-bit PMR comparison. */
#define BOARD_TICK_PRIORITY_RAW	0xe0u

/* Distributor default SPI priority (mid). Callers override per interrupt. */
#define BOARD_SPI_DEFAULT_PRIORITY_RAW	0xa0u

/* Priority for handlers that call a kernel FromISR API, RAW 8-bit GIC form:
 * level 11 in the top nibble (above the tick, below the SPI default). */
#define BOARD_IRQ_PRIORITY_API_CALL_RAW	0xb0u

/* --- PCIe (ECAM root complex, firmware-trained) ----------------------------- */

/* One ECAM window covers the whole root complex (fparameters FPCI_ECAM_*:
 * config 0x40000000 len 0x10000000, IO 0x50000000/0x08000000, MEM32
 * 0x58000000/0x27ffffff, MEM64 0x1000000000/0x1fffffffff). Six root ports
 * (U-Boot: pcie-controller@40000000, root-port VID:PID 17cd:dc16/dc08/dc01);
 * the lab's U-Boot enumeration is the comparison fixture for this line.
 * The image keeps the firmware assignment: probe-only, no bus/BAR
 * re-assignment. */
#define BOARD_PCIE_ECAM_BASE	0x40000000ULL
#define BOARD_PCIE_ECAM_SIZE	0x10000000ULL
#define BOARD_PCIE_IO_BASE	0x50000000ULL
#define BOARD_PCIE_IO_SIZE	0x08000000ULL
#define BOARD_PCIE_MEM32_BASE	0x58000000ULL
#define BOARD_PCIE_MEM32_SIZE	0x28000000ULL
#define BOARD_PCIE_MEM64_BASE	0x1000000000ULL
#define BOARD_PCIE_MEM64_SIZE	0x200000000ULL

/* Bus/dev/fn geometry of the ECAM window (fparameters: 256/32/8). */
#define BOARD_PCIE_BUS_MAX	256U
#define BOARD_PCIE_DEV_MAX	32U
#define BOARD_PCIE_FN_MAX	8U

/* INTx of the six root ports, wired as SPIs INTA..INTD = 60..63
 * (fparameters). The ECAM enumeration round stays polled; these are recorded
 * for the interrupt rounds, together with the INTx EOI quirk registers the
 * root complex needs (fparameters: EOI at 0x29100000/0x29101000, per-port
 * istatus at 0x29000184..0x29050184). */
#define BOARD_PCIE_INTX_INTID_FIRST	60U
#define BOARD_PCIE_INTX_EOI0_BASE	0x29100000ULL
#define BOARD_PCIE_INTX_EOI1_BASE	0x29101000ULL

/* --- Ethernet (on-SoC Synopsys DWC GMAC, dwmac1000 register layout) --------- */

/* gmac0: the NIC the boot loader itself tftp-boots through (DTS
 * ethernet@2820c000, register space 0x2000). MAC registers at offset 0,
 * DMA registers at offset 0x1000 - the dwmac1000-class layout. */
#define BOARD_GMAC0_BASE	0x2820c000ULL

/* MDC clock divider for the fixed 250 MHz stmmaceth clock (DTS clocks): the
 * GMII address register's CR field value 0b0101 covers 250-300 MHz. */
#define BOARD_GMAC0_MDC_CR	5U

/* RX/TX descriptor ring depths (one buffer each, enhanced descriptors). */
#define BOARD_GMAC_RX_DESCS	64U
#define BOARD_GMAC_TX_DESCS	64U

/* Per-descriptor buffer bytes (max Ethernet frame + headroom; the size
 * field is 11 bits). */
#define BOARD_GMAC_BUF_BYTES	1600U

/* --- core identity ---------------------------------------------------------- */

/* HOW A CORE'S LOGICAL NUMBER IS EXTRACTED FROM MPIDR_EL1.
 *
 * The DTS numbers the eight cores cpu@0..cpu@7 with reg <0x0 0xN>:
 * the logical core number lives in AFFINITY 0. (The RK3568 board keeps it in
 * Aff1 - this is a per-SoC fact, never an assumption.) */
#define BOARD_MPIDR_CORE_SHIFT	0

/* --- MMU layout (consumed by arch/aarch64/mmu.c) ----------------------------- */

/* Image RAM base: the lab profile loads the flat binary at 0x90000000 (the
 * same DRAM window the embox and standalone forms use), entered with `go`.
 * Everything below it inside the low 1GiB (UART, GIC) stays Device. */
#define BOARD_MMU_IMAGE_RAM_BASE	0x90000000ULL

/* Usable RAM window top: the image's own bound; the linker script enforces
 * the same number via its override symbol. */
#define BOARD_RAM_WINDOW_TOP	0xc0000000ULL

/* TCR_EL1 physical address size: PS=0b010 selects 40-bit PA. The MEM64 PCIe
 * window tops at 0x1200000000 (~72.5GiB), which 40-bit PA covers. */
#define BOARD_MMU_TCR_PS	(2ULL << 32)

/* L1 indices of the low 4GiB mapped as whole 1GiB Device blocks instead of
 * Normal: index 1 covers 0x40000000-0x7fffffff - the ECAM config window, the
 * IO window and the MEM32 window, all MMIO, no DRAM (the RK3568 board has
 * DRAM there and maps it Normal; this is the per-board MMU fact mmu.c's
 * board hook consumes). */
#define BOARD_MMU_L1_DEVICE_LO	1U
#define BOARD_MMU_L1_DEVICE_HI	1U

/* L1 indices at or above 4GiB mapped as whole 1GiB Device blocks: indices
 * 64..71 cover 0x1000000000-0x1200000000, the MEM64 window the firmware may
 * have assigned 64-bit endpoint BARs into (probe-only must be able to read
 * them). */
#define BOARD_MMU_L1_DEVICE_HI4G_FIRST	64U
#define BOARD_MMU_L1_DEVICE_HI4G_LAST	71U

#endif /* FREEWIFICAM_FIREFLY_V1_BOARD_CONF_H */
