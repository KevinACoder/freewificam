/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   board_conf.h
 * @brief  firefly v2 board calibration: every register base, interrupt
 *         number and hardware-behaviour constant in one place.
 *
 * DATA ONLY - no declarations, no inline functions - so assembly files can
 * include it too, the same convention as the RK3568 and firefly v1 board
 * configurations.
 *
 * Directory contract (this repository's firefly family layout): the family's
 * generic code lives in board/firefly/ and consumes these macros through the
 * include path - the build puts board/firefly/v2 first, so "board_conf.h"
 * resolves to THIS file.
 *
 * Fact provenance (kept next to each value): the board's Linux device tree,
 * the SoC parameter map the boot loader prints against, and the U-Boot
 * console the lab drives. Where sources disagreed the value actually proven
 * on silicon is the one recorded here.
 */

#ifndef FREEWIFICAM_FIREFLY_V2_BOARD_CONF_H
#define FREEWIFICAM_FIREFLY_V2_BOARD_CONF_H

/* --- register bases ------------------------------------------------------- */

/* Console: UART1 (DTS uart@2800d000, the node the boot loader prints on).
 * Standard PL011 byte offsets, 32-bit accesses. The clock tree feeds this
 * instance 100 MHz (DTS clock-names sysclk_100mhz) - four times the v1
 * console's rate, so the baud divisor differs. */
#define BOARD_UART1_BASE	0x2800d000UL
#define BOARD_UART_BASE		BOARD_UART1_BASE
#define BOARD_UART_CLOCK_HZ	100000000UL
#define BOARD_UART_BAUD		115200UL

/* GICv3 (DTS interrupt-controller: GICD 0x30800000 len 0x20000, GICR
 * 0x30880000 len 0x80000). One redistributor pair is 128KiB. */
#define BOARD_GICD_BASE		0x30800000UL
#define BOARD_GICR_BASE		0x30880000UL	/* core 0 redistributor */
#define BOARD_GICR_STRIDE	0x20000UL

/* GICv3 ITS frame (DTS gic-its reg 0x30820000). Reserved for the
 * message-interrupt rounds; the wired-NIC round here polls. */
#define BOARD_ITS_BASE		0x30820000UL

/* --- interrupts ------------------------------------------------------------ */

/* GIC INTID of the console UART (DTS uart@2800d000 interrupts GIC_SPI 84 =>
 * INTID 84 + 32). The console stays polled (IMSC=0); the number is recorded
 * for the rounds that enable it. */
#define BOARD_CONSOLE_INTID	116U

/* Tick source: EL1 virtual timer (CNTV), PPI 27 - DTS "arm,armv8-timer"
 * lists PPI 11 (INTID 27) for the virtual timer, its clock runs at 50 MHz. */
#define BOARD_TICK_INTID	27U

/* Tick interrupt priority, RAW 8-bit GIC form: level 14 of 16 in the top
 * nibble - the lowest usable level under a 4-bit PMR comparison. */
#define BOARD_TICK_PRIORITY_RAW	0xe0u

/* Distributor default SPI priority (mid). Callers override per interrupt. */
#define BOARD_SPI_DEFAULT_PRIORITY_RAW	0xa0u

/* Priority for handlers that call a kernel FromISR API, RAW 8-bit GIC form:
 * level 11 in the top nibble (above the tick, below the SPI default). */
#define BOARD_IRQ_PRIORITY_API_CALL_RAW	0xb0u

/* --- Ethernet (on-SoC Cadence GEM-class MAC + SGMII PHY) ------------------- */

/* xmac0: the port carrying the board's copper RJ45 (DTS ethernet@3200c000,
 * register space 0x2000). Register map and DMA contract are the Cadence
 * GEM/dwmac-gem class: control at +0x0000, DMA at +0x1000, descriptor rings
 * programmed through the queue-base registers. The boot loader's own tftp
 * transfer runs this port, so its PHY is trained before the image starts. */
#define BOARD_XMAC0_BASE	0x3200c000ULL

/* The port's refill clock feeds the MDIO divider; the boot loader's PHY scan
 * finds the transceiver at MDIO address 0 (it prints "PHY present at 0") and
 * the board's device tree gives the same address. */
#define BOARD_XMAC0_PHY_ADDR	0U

/* MDC clock divider field (network-config bits 20:18): the MAC's pclk is
 * 50 MHz here, divisor 32 keeps MDC inside the 2.5 MHz clause-22 ceiling. */
#define BOARD_XMAC0_MDC_DIV	2U

/* RX/TX descriptor ring depths. One buffer each, 1 KiB granularity in the
 * DMA config; 256 deep keeps a polled receiver ahead of a 1 Gbit/s line at
 * the case's poll budget. */
#define BOARD_XMAC_RX_DESCS	256U
#define BOARD_XMAC_TX_DESCS	256U

/* Per-descriptor buffer bytes: max Ethernet frame (1518) rounded up to the
 * 64-byte boundary the receiver works in, plus the 2-byte offset the MAC
 * writes into the buffer. */
#define BOARD_XMAC_BUF_BYTES	1600U

/* xmac0 queue-0 interrupt: firefly_v2-soc.dtsi lists GIC_SPI 55 first among
 * the controller's eight lines, and a GIC SPI n is INTID n + 32. The receiver
 * raises it when a descriptor completes, which is what turns the drain from a
 * polled loop into an event: the RX thread then sleeps instead of spinning,
 * and a frame is picked up as soon as it lands rather than on the next tick. */
#define BOARD_XMAC0_INTID	87U

/* Where every SPI in the distributor is routed at bring-up: MPIDR affinity
 * 0x200, the first little core (cpu@200 in the board's device tree) - which is
 * also the core the firmware hands control to, so it is the only one running
 * this image. Routing SPIs to 0 instead sends them to a core that never runs,
 * the line stays asserted unacknowledged, and the firmware reports it as a
 * fault in its own address space. A board that boots on a different core
 * overrides BOARD_GIC_SPI_TARGET_AFFINITY; the arch layer falls back to the
 * running core's own affinity when the board does not say. */
#define BOARD_GIC_SPI_TARGET_AFFINITY	0x200ULL

/* --- PCIe (ECAM root complex, firmware-trained) ----------------------------- */

/* One ECAM window covers the whole root complex (DTS pcie@40000000 reg
 * 0x40000000 len 0x10000000; ranges IO 0x50000000/0x00f00000, MEM32
 * 0x58000000/0x28000000, MEM64 0x1000000000/0x1000000000). The image keeps
 * the firmware assignment: probe-only, no bus/BAR re-assignment. */
#define BOARD_PCIE_ECAM_BASE	0x40000000ULL
#define BOARD_PCIE_ECAM_SIZE	0x10000000ULL
#define BOARD_PCIE_IO_BASE	0x50000000ULL
#define BOARD_PCIE_IO_SIZE	0x00f00000ULL
#define BOARD_PCIE_MEM32_BASE	0x58000000ULL
#define BOARD_PCIE_MEM32_SIZE	0x28000000ULL
#define BOARD_PCIE_MEM64_BASE	0x1000000000ULL
#define BOARD_PCIE_MEM64_SIZE	0x1000000000ULL

/* Bus/dev/fn geometry of the ECAM window. */
#define BOARD_PCIE_BUS_MAX	256U
#define BOARD_PCIE_DEV_MAX	32U
#define BOARD_PCIE_FN_MAX	8U

/* INTx of the root port, wired as SPIs (DTS interrupt-map: the four legacy
 * lines land on GIC_SPI 4..7 => INTID 36..39). The wired-NIC round stays
 * polled; these are recorded for the interrupt rounds. */
#define BOARD_PCIE_INTX_INTID_FIRST	36U

/* --- core identity ---------------------------------------------------------- */

/* HOW A CORE'S LOGICAL NUMBER IS EXTRACTED FROM MPIDR_EL1.
 *
 * The DTS numbers this SoC's four cores cpu@0/cpu@100/cpu@200/cpu@201, so
 * like v1 the logical number lives in AFFINITY 0. */
#define BOARD_MPIDR_CORE_SHIFT	0

/* --- MMU layout (consumed by arch/aarch64/mmu.c) ----------------------------- */

/* Image RAM base: the lab profile loads the flat binary at 0x90000000,
 * entered with `go`. Everything below it inside the low 1GiB (UART, GIC,
 * the MAC and MDIO frames) stays Device. */
#define BOARD_MMU_IMAGE_RAM_BASE	0x90000000ULL

/* Usable RAM window top: the image's own bound; the linker script enforces
 * the same number via its override symbol. The board has 1.9 GiB of DRAM
 * from 0x80000000, so this window is well inside it. */
#define BOARD_RAM_WINDOW_TOP	0xc0000000ULL

/* TCR_EL1 physical address size: PS=0b010 selects 40-bit PA. The MEM64 PCIe
 * window tops at 0x2000000000 (128 GiB), and nothing this image touches
 * sits above 40 bits unless the firmware assigns a 64-bit BAR there. */
#define BOARD_MMU_TCR_PS	(2ULL << 32)

/* L1 indices of the low 4GiB mapped as whole 1GiB Device blocks instead of
 * Normal: index 1 covers 0x40000000-0x7fffffff - the ECAM config window, the
 * IO window and the MEM32 window, all MMIO, no DRAM. */
#define BOARD_MMU_L1_DEVICE_LO	1U
#define BOARD_MMU_L1_DEVICE_HI	1U

/* L1 indices at or above 4GiB mapped as whole 1GiB Device blocks: indices
 * 64..95 cover 0x1000000000-0x17ffffffff, the front of the MEM64 window the
 * firmware may have assigned 64-bit endpoint BARs into. */
#define BOARD_MMU_L1_DEVICE_HI4G_FIRST	64U
#define BOARD_MMU_L1_DEVICE_HI4G_LAST	95U

#endif /* FREEWIFICAM_FIREFLY_V2_BOARD_CONF_H */
