/*
 * @file   dwc_eqos.h
 * @brief  Synopsys DesignWare Ethernet QoS (dwmac-4.x) MAC core.
 *
 * Register interface of the DesignWare EQoS generation: MAC block at 0x000,
 * MTL queues at 0xd00, per-channel DMA at 0x1000, MDIO in the dwmac4 layout.
 * This is the core found in the RK3568 (dwmac-4.20a, two instances: gmac0 at
 * 0xFE2A0000 and gmac1 at 0xFE010000, RTL8211F PHYs over RGMII).
 *
 * Register names and field positions follow the DesignWare Ethernet QoS user
 * manual. The older DWMAC 3.x generation has a different register map (0x0 for
 * its CONFIG register, MDIO at 0x10) and is a different driver, not a variant
 * of this one - do not mix the two layouts.
 *
 * What is SoC-specific - clock trees, pin iomux, RGMII delay programming, the
 * PHY reset GPIO, and the clock-mux walkaround that DMA_MODE.SWR needs on this
 * chip - is NOT in this file: it arrives through the soc_init/soc_swr_quirk
 * hooks of struct dwc_eqos_plat, so another SoC reuses the core unchanged
 * (drivers/eqos_rk3568.c is the RK3568 side).
 *
 * Register values, bit positions, timing and barrier placement in this driver
 * are the ones verified on the board; they are not to be re-derived from the
 * manual. Prior-work evidence: docs/evidence/20260916-m1a-dual-gmac-lwip.md
 * (M1-A), lab runs 20260905-embox-gmac-bringup.
 *
 * Port notes: there is no CMSIS-Driver instance macro or Driver_ETH_MAC
 * vtable here; interrupts arm through gicv3 irq_register (the handler takes
 * the instance index as its argument); the ring lock is the port's own
 * interrupt line only, because the contexts that touch these rings are the
 * service thread and this port's interrupt alone; the completion sweep is
 * driven by dwc_eqos_watchdog() from the service thread rather than a timer
 * callback; received frames wait in the staging ring for the bridge's poll.
 * Every register sequence, ring discipline and cache-maintenance point is
 * the board-verified one.
 *
 * @author zhugengyu
 * @date   16.09.2026 (board-verified core) / 02.10.2026 (port re-arrangement)
 */

#ifndef FREEWIFICAM_DWC_EQOS_H
#define FREEWIFICAM_DWC_EQOS_H

#include <stdint.h>

#include "eqos_types.h"

/* Two GMACs on this SoC. */
#define DWC_EQOS_PORT_COUNT	2

/* RX ring depth. 128 is the shipped value (N7 R2: the ring IS the receive
 * burst buffer now, so its depth sets how long a burst survives a slow
 * reader). The ring is indexed with a wrap at the end, so the count must match
 * the hardware's ring-length register (count - 1), which is programmed from
 * this same macro.
 *
 * IT IS ALSO THE SWEEP COST MULTIPLIER (measured 2026-10-17): the completion
 * sweep is O(depth) - every descriptor is probed because this controller
 * exposes no completion boundary (CUR_RX_DESC turned out to be a fetch pointer
 * that does not move while the channel is parked, on the 2026-10-17 scan-shape
 * arm) - while
 * the work between two sweeps is O(frames), and at line rate only ~1-1.5
 * frames complete per sweep. So probes per frame is depth / frames-per-sweep:
 * 66.4 measured at depth 128, i.e. 17.6 us of every frame. Halving the depth
 * halves that; the ladder sweeps it. Shallower ring = less burst tolerance and
 * fewer DMA buffers (each slot is a 2 KiB stride), and the re-arm budget is
 * the ring itself, so the floor is set by "can the reader keep up". */
#ifndef CONFIG_GMAC_RX_RING
#define CONFIG_GMAC_RX_RING	128
#endif
#define DWC_EQOS_RX_DESC_COUNT	CONFIG_GMAC_RX_RING
/* TX ring depth. 64 is the board-verified depth; the TX ladder sweeps it
 * (the Phytium fgmac lwIP port runs 128). The copy path needs exactly the
 * ring depth in buffers, so this multiplies the whole TX buffer block - a
 * 128-deep ring costs another 256 KiB of .bss. */
#ifndef CONFIG_GMAC_TX_RING
#define CONFIG_GMAC_TX_RING	64
#endif
#define DWC_EQOS_TX_DESC_COUNT	CONFIG_GMAC_TX_RING

/* DMA buffer slots. The copy path needs exactly the ring depth (each frame's
 * buffer is its only home until the ISR copies it out). The zero-copy path
 * (CONFIG_GMAC_RX_ZC, N7 R4) hands buffers UP with the frame, so re-arm
 * needs spare slots while frames are in flight with the consumer: ring
 * depth + ring depth, and a frame is dropped (not parked) when the spares
 * run out - the DMA never waits on a descriptor. */
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
#define DWC_EQOS_RX_SLOT_COUNT	(DWC_EQOS_RX_DESC_COUNT * 2)
#else
#define DWC_EQOS_RX_SLOT_COUNT	DWC_EQOS_RX_DESC_COUNT
#endif

/* Per-frame DMA buffer size. 1600, not 1518: the emBox port sized the DMA
 * buffers for jumbo-ish frames and the value is also the ceiling the tx path
 * checks a frame against. */
#define DWC_EQOS_RX_BUF_SIZE	1600

/* Every frame gets its own 2 KiB slot so that cache maintenance on one buffer
 * can never touch a neighbour: the dcache line is 64 bytes, and two frames
 * sharing a line would need flush/invalidate pairs that fight each other. */
#define DWC_EQOS_BUF_ALIGN	0x800
#define DWC_EQOS_BUF_STRIDE	0x800

/* Frames handed up to the driver's caller are staged in the ISR: the RX
 * DMA stops the moment it laps a descriptor that is not owned, so the
 * completion sweep re-arms every completed slot immediately (in interrupt
 * context) and pays one linear copy into this ring as the price. Depth
 * sets the burst budget: 8 overflowed under the iperf downlink (N7 R0
 * dropped 13.5%); 128 = the descriptor ring depth it serves. */
#define DWC_EQOS_RX_STAGE_COUNT	128

/* One TX completion interrupt per this many frames (the last frame of a
 * burst always interrupts, so a quiet line never waits on the counter).
 * Swept by the TX ladder: 1 gives every frame its own completion, which
 * shortens the reclaim latency at the cost of one interrupt per frame. */
#ifndef CONFIG_GMAC_TX_IOC_DIV
#define CONFIG_GMAC_TX_IOC_DIV	8
#endif
#define DWC_EQOS_TX_IOC_DIV	CONFIG_GMAC_TX_IOC_DIV

/* MDIO clock divider. pclk_gmac runs at 100 MHz here, and CR=4 selects the
 * 150-250 MHz range in the dwmac4 encoding, which keeps MDC at ~8.3 MHz -
 * inside the RTL8211F's 12.5 MHz limit. */
#define DWC_EQOS_MDIO_CR	4

/* 1000 Mbps fast ethernet frame limits. */
#define DWC_EQOS_MAC_ADDR_LEN	6
#define DWC_EQOS_MIN_TX_FRAME	60	/* before FCS: shorter frames are
						 * dropped as runts by the peer, and a
						 * 42-byte ARP reply is a normal
						 * occurrence, not an edge case */

/* --- MAC registers (offsets from the port base) --------------------------- */

#define DWC_EQOS_MAC_CONFIGURATION           0x000
#define  DWC_EQOS_MAC_CONF_RE                (1u << 0)
#define  DWC_EQOS_MAC_CONF_TE                (1u << 1)
#define  DWC_EQOS_MAC_CONF_DCRS              (1u << 9)
#define  DWC_EQOS_MAC_CONF_DM                (1u << 13)
#define  DWC_EQOS_MAC_CONF_FES               (1u << 14)
#define  DWC_EQOS_MAC_CONF_PS                (1u << 15)
#define  DWC_EQOS_MAC_CONF_JE                (1u << 16)
#define  DWC_EQOS_MAC_CONF_JD                (1u << 17)
#define  DWC_EQOS_MAC_CONF_BE                (1u << 18)
#define  DWC_EQOS_MAC_CONF_IPC               (1u << 27)	/* RX checksum
							 * offload: the IPC
							 * block validates
							 * L3/L4 of every IP
							 * frame (Linux
							 * dwmac4.h
							 * GMAC_CONFIG_IPC;
							 * board dump diff
							 * Reg0 bit27) */

#define DWC_EQOS_MAC_PACKET_FILTER           0x008
#define  DWC_EQOS_PKT_FLT_PR                 (1u << 0)

#define DWC_EQOS_MAC_Q0_TX_FLOW_CTRL         0x070
#define DWC_EQOS_MAC_RX_FLOW_CTRL            0x090
#define  DWC_EQOS_RX_FLOW_CTRL_RFE           (1u << 0)

#define DWC_EQOS_MAC_RXQ_CTRL0               0x0A0
#define  DWC_EQOS_RXQ_CTRL0_RXQ0EN_MASK      (3u << 0)
#define  DWC_EQOS_RXQ_CTRL0_RXQ0EN_DCB       (1u << 0)
#define DWC_EQOS_MAC_RXQ_CTRL1               0x0A4
#define  DWC_EQOS_RXQ_CTRL1_MCBCQEN          (1u << 20)

#define DWC_EQOS_MAC_1US_TIC_COUNTER         0x0DC

/* MAC-level interrupt status/enable. Separate from the DMA channel's
 * INTR_ENABLE (0x1134): this one covers MAC events (link status change, PMT,
 * timestamp, MDIO). Both feed the same interrupt line, so a latched bit here
 * with its enable set holds the line asserted no matter how thoroughly the
 * DMA status is cleared. */
#define DWC_EQOS_MAC_INTERRUPT_STATUS        0x0B0
#define DWC_EQOS_MAC_INTERRUPT_ENABLE        0x0B4
/* MAC-level status bits (Linux dwmac4.h enum dwmac4_irq_status): the MMC
 * counter events present here too. These are NOT gated by 0x0B4 - the MMC
 * block has its own masks (see below) - which is exactly why the 2026-10-02
 * freeze walked past a disabled 0x0B4: an unmasked MMC latch holds the
 * level line with nothing the DMA-status handler can see. */
#define  DWC_EQOS_MAC_INTR_ST_MMC_TX         (1u << 10)
#define  DWC_EQOS_MAC_INTR_ST_MMC_RX         (1u << 9)
#define  DWC_EQOS_MAC_INTR_ST_MMC_RX_CSUM    (1u << 11)
#define  DWC_EQOS_MAC_INTR_ST_TS             (1u << 12)

/* --- MMC counter interrupt block (base + 0x700; Linux mmc_core.c offsets) --
 * MMC counters raise an interrupt when the counter crosses its half-way
 * point (bit 31 of a 32-bit counter); under a 650 Mbit/s TX flow the TX
 * octet counter reaches 0x8000_0000 after ~2 GiB, i.e. ~26 s - matching the
 * measured freeze onset. The events latch into the two status words and
 * assert the same line as the DMA channel; the ONLY thing that stops them
 * is the mask pair (Linux setup: stmmac_mmc_intr_all_mask, mmc_core.c
 * "To mask all interrupts"). */
#define DWC_EQOS_MMC_RX_INTR                 0x704
#define DWC_EQOS_MMC_TX_INTR                 0x708
#define DWC_EQOS_MMC_RX_INTR_MASK            0x70C
#define DWC_EQOS_MMC_TX_INTR_MASK            0x710
#define DWC_EQOS_MMC_RX_IPC_INTR_MASK        0x800

/* --- MTL queue interrupt (Linux dwmac4.h: MTL_INT_STATUS 0xC20,
 * MTL_CHAN_INT_CTRL queue0 0xD2C; RX overflow status bit16, enable bit24).
 * Linux services this in the same host-IRQ path (dwmac4_core.c
 * dwmac4_irq_status): check the summary, W1C the overflow status. An
 * unserviced latch here is the other way the line is held with a clean DMA
 * status. */
#define DWC_EQOS_MTL_INT_STATUS              0x0C20
#define DWC_EQOS_MTL_CHAN0_INT_CTRL          0x0D2C
#define  DWC_EQOS_MTL_RX_OVERFLOW_INT        (1u << 16)

#define DWC_EQOS_MAC_HW_FEATURE1             0x120
#define  DWC_EQOS_HW_TXFIFOSZ_SHIFT          6
#define  DWC_EQOS_HW_TXFIFOSZ_MASK           (0x1fu << 6)
#define  DWC_EQOS_HW_RXFIFOSZ_MASK           0x1fu
/* Capability register 0 (TRM Part2 ch.20 GMAC_MAC_HW_Feature0, 0x011C; the
 * same one Linux reads for these bits - dwmac4_dma.c:383). TRM reset value
 * on this SoC is 0x160171E3, i.e. TXCOSEL and RXCOESEL are both 1. */
#define DWC_EQOS_MAC_HW_FEATURE0             0x11C
/* TX checksum offload capability, FEATURE0 bit 14 (Linux dwmac4.h
 * GMAC_HW_FEAT_TXCOSEL, dwmac4_dma.c:402): 0 = the core was not synthesized
 * with the inserter and CIC writes do nothing, which for a zero-checksum TCP
 * frame means it leaves broken - so the CIC tagging is gated on this bit at
 * runtime, not on the build switch alone. */
#define  DWC_EQOS_HW_TXCOSEL                 (1u << 14)

#define DWC_EQOS_MAC_VERSION                 0x110

#define DWC_EQOS_MAC_MDIO_ADDRESS            0x200
/* Field layout on this core: PA[24:21] RDA[20:16] CR[10:8] GOC[3:2] GB[0].
 * This is NOT the DWMAC 3.x layout (PA[20:16] RDA[15:11]) - see the note at
 * the top of this file. */
#define  DWC_EQOS_MDIO_PA_SHIFT              21
#define  DWC_EQOS_MDIO_PA_MASK               (0x1fu << 21)
#define  DWC_EQOS_MDIO_RDA_SHIFT             16
#define  DWC_EQOS_MDIO_RDA_MASK              (0x1fu << 16)
#define  DWC_EQOS_MDIO_CR_SHIFT              8
#define  DWC_EQOS_MDIO_CR_MASK               (0x7u << 8)
#define  DWC_EQOS_MDIO_GOC_READ              (3u << 2)
#define  DWC_EQOS_MDIO_GOC_WRITE             (1u << 2)
#define  DWC_EQOS_MDIO_GB                    (1u << 0)
#define DWC_EQOS_MAC_MDIO_DATA               0x204

#define DWC_EQOS_MAC_ADDRESS0_HIGH           0x300
#define DWC_EQOS_MAC_ADDRESS0_LOW            0x304

/* --- MTL (queues) --------------------------------------------------------- */

#define DWC_EQOS_MTL_TXQ0_OPERATION_MODE     0xD00
#define  DWC_EQOS_MTL_TXQ0_OP_TQS_SHIFT      16
#define  DWC_EQOS_MTL_TXQ0_OP_TQS_MASK       (0x1ffu << 16)
#define  DWC_EQOS_MTL_TXQ0_OP_TXQEN_EN       (2u << 2)
#define  DWC_EQOS_MTL_TXQ0_OP_TSF            (1u << 1)

#define DWC_EQOS_MTL_RXQ0_OPERATION_MODE     0xD30
#define  DWC_EQOS_MTL_RXQ0_OP_RQS_SHIFT      20
#define  DWC_EQOS_MTL_RXQ0_OP_RQS_MASK       (0x3ffu << 20)
#define  DWC_EQOS_MTL_RXQ0_OP_RSF            (1u << 5)
#define  DWC_EQOS_MTL_RXQ0_OP_FEP            (1u << 4)
#define  DWC_EQOS_MTL_RXQ0_OP_FUP            (1u << 3)

/* --- DMA channel 0 -------------------------------------------------------- */

#define DWC_EQOS_DMA_MODE                    0x1000
#define  DWC_EQOS_DMA_MODE_SWR               (1u << 0)

#define DWC_EQOS_DMA_SYSBUS_MODE             0x1004
#define  DWC_EQOS_SYSBUS_WR_OSR_SHIFT        24
#define  DWC_EQOS_SYSBUS_RD_OSR_SHIFT        16
#define  DWC_EQOS_SYSBUS_EAME                (1u << 11)
#define  DWC_EQOS_SYSBUS_BLEN16              (1u << 3)
#define  DWC_EQOS_SYSBUS_BLEN8               (1u << 2)
#define  DWC_EQOS_SYSBUS_BLEN4               (1u << 1)

#define DWC_EQOS_DMA_CH0_CONTROL             0x1100
#define  DWC_EQOS_CH0_CTRL_PBLX8             (1u << 16)

#define DWC_EQOS_DMA_CH0_TX_CONTROL          0x1104
#define  DWC_EQOS_TX_CTRL_TXPBL_SHIFT        16
#define  DWC_EQOS_TX_CTRL_TXPBL_MASK         (0x3fu << 16)
#define  DWC_EQOS_TX_CTRL_OSP                (1u << 4)
#define  DWC_EQOS_TX_CTRL_ST                 (1u << 0)

#define DWC_EQOS_DMA_CH0_RX_CONTROL          0x1108
#define  DWC_EQOS_RX_CTRL_RXPBL_SHIFT        16
#define  DWC_EQOS_RX_CTRL_RXPBL_MASK         (0x3fu << 16)
#define  DWC_EQOS_RX_CTRL_RBSZ_SHIFT         1
#define  DWC_EQOS_RX_CTRL_RBSZ_MASK          (0x3fffu << 1)
#define  DWC_EQOS_RX_CTRL_SR                 (1u << 0)

#define DWC_EQOS_DMA_CH0_TX_BASE_ADDR_HI     0x1110
#define DWC_EQOS_DMA_CH0_TX_BASE_ADDR_LO     0x1114
#define DWC_EQOS_DMA_CH0_RX_BASE_ADDR_HI     0x1118
#define DWC_EQOS_DMA_CH0_RX_BASE_ADDR_LO     0x111C
#define DWC_EQOS_DMA_CH0_TX_END_ADDR         0x1120
#define DWC_EQOS_DMA_CH0_RX_END_ADDR         0x1128
#define DWC_EQOS_DMA_CH0_TX_RING_LEN         0x112C
#define DWC_EQOS_DMA_CH0_RX_RING_LEN         0x1130
/* Channel + 0x38 (Linux dwmac4_dma.h DMA_CHAN_RX_WATCHDOG; the running
 * Linux on this board shows 0xa0 here = its DEF_DMA_RIWT). Unit is 256 DMA
 * clock ticks; expiry asserts RI even with every descriptor IOC off. */
#define DWC_EQOS_DMA_CH0_RX_WATCHDOG         0x1138

/* Where the RX DMA says it is. Channel + 0x4c / + 0x5c (Linux
 * dwmac4_dma.h DMA_CHAN_CUR_RX_DESC / DMA_CHAN_CUR_RX_BUF_ADDR; note the
 * +0x58 word next door is the high half of the buffer address). One read of
 * CUR_RX_DESC is what turns the completion sweep from a 128-slot blind probe
 * into a bounded walk. */
#define DWC_EQOS_DMA_CH0_CUR_RX_DESC         0x114C
#define DWC_EQOS_DMA_CH0_CUR_RX_BUF_ADDR     0x115C

#define DWC_EQOS_DMA_CH0_INTR_ENABLE         0x1134
/* On this silicon NIE/AIE sit at 15/14 (the dwmac4.10a layout); bit 16 is not
 * writable. Enabling bit 16 instead looks harmless and leaves every interrupt
 * masked. */
#define  DWC_EQOS_CH0_INTR_NIE               (1u << 15)
#define  DWC_EQOS_CH0_INTR_AIE               (1u << 14)
#define  DWC_EQOS_CH0_INTR_FBE               (1u << 12)
#define  DWC_EQOS_CH0_INTR_RIE               (1u << 6)
#define  DWC_EQOS_CH0_INTR_TIE               (1u << 0)
#define  DWC_EQOS_CH0_INTR_DEFAULT           (DWC_EQOS_CH0_INTR_NIE \
						| DWC_EQOS_CH0_INTR_AIE \
						| DWC_EQOS_CH0_INTR_FBE \
						| DWC_EQOS_CH0_INTR_RIE \
						| DWC_EQOS_CH0_INTR_TIE)

#define DWC_EQOS_DMA_CH0_STATUS              0x1160
#define  DWC_EQOS_CH0_STATUS_NIS             (1u << 15)
#define  DWC_EQOS_CH0_STATUS_AIS             (1u << 14)
#define  DWC_EQOS_CH0_STATUS_FB              (1u << 12)
#define  DWC_EQOS_CH0_STATUS_RWT             (1u << 9)	/* RX watchdog
							 * expiry (the
							 * coalesced RI
							 * source) */
#define  DWC_EQOS_CH0_STATUS_RI              (1u << 6)
#define  DWC_EQOS_CH0_STATUS_TI              (1u << 0)
/* Two further causes this driver has always seen but never named: they have
 * no independent enable bits in INTR_ENABLE (checked: Linux's dwmac4_dma.h
 * layouts have none either), so they latch into the status word and ride the
 * normal/abnormal summaries. TBU is a LIVE condition - the DMA met an
 * unowned descriptor, which is what a drained TX ring looks like - so it
 * re-latches on the next status read after the W1C; ETI reports the TX FIFO
 * crossing its early threshold. Linux consumes the same word as service
 * hints, not as faults (dwmac4_lib.c: TBU -> handle_tx, ETI -> counter). */
#define  DWC_EQOS_CH0_STATUS_TBU             (1u << 2)	/* transmit buffer
							 * unavailable (live) */
#define  DWC_EQOS_CH0_STATUS_ETI             (1u << 10)	/* early transmit
							 * interrupt */
#define  DWC_EQOS_CH0_STATUS_KNOWN_ODD \
	(DWC_EQOS_CH0_STATUS_TBU | DWC_EQOS_CH0_STATUS_ETI \
	 | DWC_EQOS_CH0_STATUS_NIS | DWC_EQOS_CH0_STATUS_AIS)

/* --- descriptor ----------------------------------------------------------- */

/* 16-byte normal descriptor, contiguous ring (no skip length), i.e. the
 * "legacy" format and not the 32-byte alternate/context descriptor.
 *
 * TX: des2 = frame length [13:0] | IOC [31], des3 = OWN [31] | FD [29] |
 *     LD [28] | frame length [14:0]; after the writeback the frame length
 *     keeps that position and [15] reports the error summary (the bit
 *     positions are the upstream dwmac4_descs.h set).
 * RX: des2 = 0, des3 = OWN [31] | IOC [30] | BUF1V [24], and after the DMA
 *     writes the frame back the length appears in des3 [14:0] including FCS. */
struct dwc_eqos_desc {
	uint32_t des0;	/* buffer address bits[31:0] */
	uint32_t des1;	/* buffer address bits[47:32] (unused, 0) */
	uint32_t des2;	/* TX: frame length | IOC */
	uint32_t des3;	/* control / writeback status */
};

#define DWC_EQOS_DESC_OWN			(1u << 31)
#define DWC_EQOS_DESC_TX_IOC			(1u << 31)
#define DWC_EQOS_DESC_RX_IOC			(1u << 30)
#define DWC_EQOS_DESC_TX_FD			(1u << 29)
#define DWC_EQOS_DESC_TX_LD			(1u << 28)
#define DWC_EQOS_DESC_RX_BUF1V			(1u << 24)
#define DWC_EQOS_DESC_TX_ERR			(1u << 15)	/* writeback:
							 error summary */
#define DWC_EQOS_DESC_RX_LEN_MASK		0x7fffu
/* TX checksum insertion control (TDES3[17:16] on this core; RK3568 TRM
 * Part2 ch.20 "Transmit Descriptor 3": 2'b11 = IP header checksum and
 * payload checksum calculation and insertion enabled with the pseudo-header
 * checksum computed in hardware - the same full-offload encoding Linux
 * programs for CHECKSUM_PARTIAL frames, value 3, descs.h:179). Linux
 * proves the field position twice: dwmac4_descs.h:32 TDES3_CHECKSUM_
 * INSERTION_MASK = GENMASK(17,16), and dwmac4_descs.c:321 writes it into
 * tdes3. The first board attempt of this feature put the tag in des2 and
 * the frames left rejected (2026-10-18) - the field belongs to the SAME
 * double-word as the length and OWN. The stack side stops generating the L4
 * checksum and the driver seeds the field with the plain pseudo-header sum
 * (eqos_tx_csum_prepare carries the derivation); both halves move under one
 * build switch (CONFIG_GMAC_TX_CSUM). */
#define DWC_EQOS_DESC_TX_CIC_SHIFT		16
#define DWC_EQOS_DESC_TX_CIC_FULL		(3u << 16)
/* RX writeback checksum status (the IPC block's report, Linux dwmac4_descs.h
 * RDES1_*): valid only when the des3 RDES1_VALID flag is set, which the MAC
 * does for IPv4/IPv6 frames only - ARP and friends sail through unchecked,
 * exactly as before. */
#define DWC_EQOS_DESC_RX_RDES1_VALID		(1u << 26)	/* in des3 */
#define DWC_EQOS_RDES1_IP_HDR_ERR		(1u << 3)	/* in des1 */
#define DWC_EQOS_RDES1_IP_PAYLOAD_ERR		(1u << 7)	/* in des1 */

/* --- per-instance configuration ------------------------------------------- */

/* Everything a port needs to be told, and nothing it can look up: SoC
 * integration data is opaque (soc_data) and only the glue hooks touch it, so
 * the core stays reusable across SoCs. */
struct dwc_eqos_plat {
	unsigned int   index;		/* instance slot, 0..DWC_EQOS_PORT_COUNT-1 */
	uintptr_t      base_addr;	/* MAC register base */
	uint32_t       irq_num;		/* combined mac/dma interrupt (INTID) */
	uint8_t        mac_addr[6];	/* MAC_ADDR_LEN bytes */

	/* SoC glue. soc_init brings up clocks, pin iomux, RGMII delays and
	 * pulses the PHY reset before the core touches the MAC; soc_swr_quirk
	 * (optional) walks the clock mux while DMA_MODE.SWR is asserted. */
	int  (*soc_init)(const struct dwc_eqos_plat *plat);
	void (*soc_swr_quirk)(const struct dwc_eqos_plat *plat, int enter);
	const void *soc_data;
};

/* --- control codes (the CMSIS ARM_ETH_MAC_CONTROL_* shape) ------------------ */

#define DWC_EQOS_CONFIGURE	0	/* arg: speed/duplex bits (eqos_types) */
#define DWC_EQOS_CONTROL_TX	1	/* arg != 0 starts the channel */
#define DWC_EQOS_CONTROL_RX	2	/* (TX/RX share one channel here) */

/* --- core entry points ------------------------------------------------------ */

int32_t dwc_eqos_initialize(unsigned int index, const struct dwc_eqos_plat *plat);
int32_t dwc_eqos_uninitialize(unsigned int index);
int32_t dwc_eqos_power_control(unsigned int index, eqos_power_state_t state);
int32_t dwc_eqos_get_mac_address(unsigned int index, uint8_t *addr /* 6 bytes */);
int32_t dwc_eqos_set_mac_address(unsigned int index, const uint8_t *addr);
int32_t dwc_eqos_send_frame(unsigned int index, const uint8_t *frame, uint32_t len);
int32_t dwc_eqos_read_frame(unsigned int index, uint8_t *frame, uint32_t len);
uint32_t dwc_eqos_get_rx_frame_size(unsigned int index);
int32_t dwc_eqos_control(unsigned int index, uint32_t control, uint32_t arg);
int32_t dwc_eqos_phy_read(unsigned int index, uint8_t phy_addr, uint8_t reg_addr, uint16_t *data);
int32_t dwc_eqos_phy_write(unsigned int index, uint8_t phy_addr, uint8_t reg_addr, uint16_t data);

/* Main-loop watchdog sweep: completion re-runs + RX end-pointer re-poke.
 * The prior workspace ran this in a 100 ms osTimer; the NO_SYS image calls it
 * from its main loop at the same cadence. */
void dwc_eqos_watchdog(unsigned int index);

/* Combined MAC/DMA interrupt body, registered per port through gicv3 with the
 * instance index as the handler argument. */
void dwc_eqos_irq(void *arg);

/* Optional RX-notify hook (P7-2a): set before the channel starts; called
 * from the combined ISR after rx_complete delivered at least one frame -
 * the adapter releases its RX semaphore there (the prior workspace's
 * event_rx_frame, reduced to the one event the bridge consumes). */
void dwc_eqos_set_rx_notify(void (*fn)(unsigned int index));

/* NAPI-shaped RX drain (CONFIG_GMAC_RX_NAPI, 2026-10-17): the ISR masks the RX
 * interrupt and signals, the RX thread calls this under a frame budget, and the
 * interrupt comes back only when a sweep finds the ring empty. Shaped after the
 * Linux driver's stmmac_napi_poll_rx (budget = NAPI_POLL_WEIGHT, RI masked at
 * schedule time, re-enabled only when work_done < budget). Returns the frames
 * delivered; 0 means the ring was empty. Thread context only. */
#ifndef CONFIG_GMAC_RX_BUDGET
#define CONFIG_GMAC_RX_BUDGET	64
#endif
int dwc_eqos_rx_poll(unsigned int index, int budget);

/* Runtime RX-watchdog (coalescing batch size) knob: the register value, so a
 * measurement round sweeps RIWT inside one boot. */
void dwc_eqos_set_rx_riwt(unsigned int index, uint32_t riwt);
uint32_t dwc_eqos_get_rx_riwt(unsigned int index);

/* Interrupt-shape runtime seams (2026-10-19 census round), so one boot carries
 * the whole A/B instead of one boot per build:
 *  - oddmode 0 keeps the N7-stab treatment of unserviced status words (count,
 *    first power-of-two prints, burst breaker); oddmode 1 handles TBU/ETI-only
 *    words as the ordinary traffic conditions they are (see the TBU/ETI note
 *    above) - they stop feeding the odd net, the unknown-bit net stays.
 *  - rxcoal programs the RX watchdog AND stops arming the per-frame IOC bit on
 *    future descriptors (the arming change lands as the RX thread re-arms the
 *    ring, one turnover); riwt 0 restores per-frame RI.
 *  - txdiv is the TX completion interval (descriptors between TI arms). */
void dwc_eqos_set_irq_oddmode(unsigned int index, unsigned int mode);
unsigned int dwc_eqos_get_irq_oddmode(unsigned int index);
void dwc_eqos_set_rx_coalesce(unsigned int index, uint32_t riwt);
void dwc_eqos_set_tx_ioc_div(unsigned int index, unsigned int div);
unsigned int dwc_eqos_get_tx_ioc_div(unsigned int index);

/* Wide per-frame copy seam (2026-10-20): the two whole-frame copies (ring->
 * staging on RX, pbuf->TX buffer on send) switch between the image's scalar
 * memcpy and the arch layer's LDP/STP blocks. The WIDECOPY build lever picks
 * the default, this flips it so one boot carries the A/B; the census priced
 * the copy at 1.5 us per 1514-byte frame (the txcopy bucket). The `on` knob
 * moves both sites; the selector moves them separately - bit0 = the RX
 * staging copy, bit1 = the TX frame copy - because a downlink byte-trail
 * with both wide on made the RX site a suspect while the uplink win sits on
 * the TX site. Global, not per-port - one live port in this image (the
 * RX-notify hook's shape). */
void dwc_eqos_set_wide_copy(unsigned int index, unsigned int on);
unsigned int dwc_eqos_get_wide_copy(unsigned int index);
void dwc_eqos_set_wide_copy_sel(unsigned int index, unsigned int mask);
unsigned int dwc_eqos_get_wide_copy_sel(unsigned int index);

/* Zero-copy RX (CONFIG_GMAC_RX_ZC, N7 R4): the driver lends completed
 * frames out instead of copying them into the staging ring.
 *
 * The wrap hook runs in INTERRUPT context with the completed frame and
 * returns an opaque consumer handle the driver parks in its ready ring
 * (NULL = consumer refused; the driver drops the frame). The consumer
 * drains handles with dwc_eqos_rx_dequeue (thread context) and returns
 * each borrowed buffer with dwc_eqos_rx_release once it is done with it.
 * Buffers never leave the driver's slot pool, so a late release can never
 * make the DMA read from memory someone else owns. */
typedef void *(*dwc_eqos_rx_wrap_fn)(unsigned int index, unsigned int slot,
				     uint8_t *buf, unsigned int len);
void dwc_eqos_set_rx_zc(dwc_eqos_rx_wrap_fn fn);
int dwc_eqos_rx_dequeue(unsigned int index, void **handle);
void dwc_eqos_rx_release(unsigned int index, unsigned int slot);

/* Statistics for the bring-up heartbeat: everything the driver had to drop or
 * could not complete, so a bad cable and a full ring are distinguishable. */
struct dwc_eqos_stats {
	uint32_t rx_frames;	/* frames completed by the DMA */
	uint32_t rx_dropped;	/* (reserved; the desc-ring model has no
				 * intermediate stage to overflow) */
	uint32_t rx_errors;	/* runt frames, bogus lengths */
	uint32_t tx_frames;	/* descriptors armed */
	uint32_t tx_dropped;	/* no descriptor became free in time */
	uint32_t tx_errors;	/* writebacks with the error-summary bit set */
	uint32_t dma_errors;	/* fatal bus errors seen in the status word */
	uint32_t rx_irqs;	/* interrupt passes that ran the RX sweep
				 * (the coalescing gauge: compare against
				 * rx_frames per heartbeat) */
	uint32_t rx_csum_drops;	/* RDES1-flagged bad-checksum frames (IPC) */
	uint32_t tx_csum_frames;	/* frames sent with CIC=3 (the MAC inserts
					 * the IPv4/TCP or IPv4/UDP checksums) */
	uint32_t tx_csum_uncovered;	/* zero-checksum TCP frames the CIC
					 * classifier could not vouch for: arm
					 * acceptance requires zero (see
					 * eqos_tx_csum_prepare) */
};

void dwc_eqos_get_stats(unsigned int index, struct dwc_eqos_stats *stats);

/* Fast-probe snapshot (N7-stab): ring occupancy, completion-semaphore
 * count, zero-copy pressure, and the IRQ black box above, plus live
 * register reads (CH0_STATUS, INTR_ENABLE) and the GIC pending bit for
 * this port's line. Callable from any thread; takes the port line lock
 * only for the ring indices. */
struct dwc_eqos_diag {
	uint32_t tx_head;
	uint32_t tx_tail;
	uint32_t tx_ioc_seq;
	uint32_t tx_sem_count;	/* release-on-completion signals pending */
	/* TX-ladder discriminators: whether the ring is the limiter shows up
	 * here as senders finding it full, time parked on the completion
	 * semaphore, and how deep the ring actually ran. */
	uint32_t tx_waits;	/* sends that found the ring full */
	uint32_t tx_wait_ticks;	/* kernel ticks spent parked on tx_sem */
	uint32_t tx_occ_max;	/* peak descriptors in flight */
	uint32_t rx_ready_count;	/* ZC frames waiting for the consumer */
	uint32_t rx_free_count;	/* spare slots left in the ZC pool */
	uint32_t irq_n;
	uint32_t irq_odd;
	uint32_t last_status;
	uint32_t ch0_status;	/* live read at call time */
	uint32_t intr_enable;	/* live read at call time */
	int32_t  gic_pending;	/* pending bit of this port's GIC line */
	uint32_t irq_breaker;	/* 1 once the breaker fired (see dwc_eqos.c) */
};

void dwc_eqos_get_diag(unsigned int index, struct dwc_eqos_diag *diag);

/* RX ring-state snapshot (2026-10-17). The tail pointer's landing point and
 * the fetch-window semantics were argued from two contradictory comments for
 * two rounds, and two cursor walks failed on them (2026-10-16): one version
 * of this file says RX_END is "written once and left alone", the sweep writes
 * it every call. This reads the facts instead of the prose - where the DMA
 * says it is, where the window ends, and which side still owns each slot.
 * Console context only; walks the ring under the port-line lock. */
struct dwc_eqos_ring {
	uint32_t rx_end;	/* DMA_CH0_RX_END_ADDR: the fetch-window tail */
	uint32_t rx_base;	/* DMA_CH0_RX_BASE_ADDR_LO */
	uint32_t rx_ring_len;	/* DMA_CH0_RX_RING_LEN (count - 1) */
	uint32_t cur_rx_desc;	/* DMA_CH0_CUR_RX_DESC: the DMA's position */
	uint32_t cur_rx_buf;	/* DMA_CH0_CUR_RX_BUF_ADDR */
	uint32_t rx_control;	/* RX_CONTROL; SR (bit 0) = channel running */
	uint32_t ch0_status;	/* live status word at call time */
	int32_t  cur_desc_slot;	/* cur_rx_desc as a slot index, -1 = outside */
	int32_t  end_slot;	/* rx_end as a slot index, -1 = outside */
	int32_t  rx_scan;	/* software sweep cursor (first slot to probe) */
	int32_t  last_armed;	/* last slot the sweep re-armed */
	uint32_t ring_armed;	/* slots currently handed to the DMA */
	uint32_t own_clear;	/* descriptors the DMA has written back */
	uint32_t walk_run;	/* OWN-clear run from the cursor, to the first
				 * descriptor the DMA still owns */
	uint32_t stale_slots;	/* OWN-clear slots OUTSIDE that run: non-zero
				 * means the bounded walk would leak a slot */
	uint32_t in_flight_mask[4];	/* rx_in_flight[i] == 1, bit i = slot i */
	uint32_t dma_owned_mask[4];	/* descriptor still OWN, bit i = slot i */
	/* Sticky sweep telemetry: what the LAST sweep saw, recorded while it
	 * happened (the phase-boundary snapshot only ever sees a drained ring). */
	uint32_t sw_sweeps;		/* sweeps that have run */
	uint32_t sw_clear_seen;		/* OWN-clear slots the last sweep saw */
	uint32_t sw_max_clear;		/* worst case of the above */
	uint32_t sw_span;		/* slots the SCAN=3 walk covered */
	int32_t  sw_curdesc;		/* CUR_RX_DESC slot read at sweep entry */
	int32_t  sw_last_armed;		/* the same sweep's last re-armed slot */
	/* How far the published tail sits BEHIND the DMA's own position, in
	 * slots, at sample time. 1 is the healthy at-rest geometry (DMA parked
	 * on the descriptor just past the tail); a larger value means the tail
	 * is retracted behind the channel, which is the 2026-10-17 stall. -1
	 * when either address is outside the ring. */
	int32_t  end_gap;
};

int dwc_eqos_get_ring(unsigned int index, struct dwc_eqos_ring *ring);

/* Live MAC/DMA register snapshot (2026-10-18). The Linux lane answers the
 * same question by reading these offsets out of its own driver (devmem /
 * ethtool -d), and the parity round diffs the two hardware pictures cell by
 * cell: every "the source says X" claim about bus bursts, FIFO watermarks,
 * flow control, coalescing and checksum offload has to become "the hardware
 * was seen at X" on BOTH stacks or it is not a difference, just a reading
 * of the source. Console context only; single read burst, no lock held
 * across it beyond the port line. */
struct dwc_eqos_regs {
	uint32_t mac_configuration;	/* 0x000 */
	uint32_t mac_packet_filter;	/* 0x008 */
	uint32_t mac_q0_tx_flow_ctrl;	/* 0x070 */
	uint32_t mac_rx_flow_ctrl;	/* 0x090 */
	uint32_t hw_feature0;		/* 0x11c: capability set 0 (csum engines) */
	uint32_t hw_feature1;		/* 0x120: TX/RX FIFO sizes */
	uint32_t mtl_txq0_op;		/* 0xd00 */
	uint32_t mtl_rxq0_op;		/* 0xd30 */
	uint32_t dma_mode;		/* 0x1000 */
	uint32_t sysbus_mode;		/* 0x1004 */
	uint32_t ch0_control;		/* 0x1100 */
	uint32_t ch0_tx_control;	/* 0x1104 */
	uint32_t ch0_rx_control;	/* 0x1108 */
	uint32_t ch0_tx_end;		/* 0x1120 */
	uint32_t ch0_rx_end;		/* 0x1128 */
	uint32_t ch0_tx_ring_len;	/* 0x112c */
	uint32_t ch0_rx_ring_len;	/* 0x1130 */
	uint32_t ch0_intr_enable;	/* 0x1134 */
	uint32_t ch0_rx_watchdog;	/* 0x1138 */
	uint32_t ch0_cur_rx_desc;	/* 0x114c */
	uint32_t ch0_status;		/* 0x1160 */
};

int dwc_eqos_get_regs(unsigned int index, struct dwc_eqos_regs *regs);

/* Per-phase service-time profile. The wired ladder is down to "where does the
 * single core's time go", and only an exclusive-time split can answer it: the
 * counters below are read from the system counter (24 MHz, 41.7 ns) at the
 * operation boundaries, so each bucket is exclusive of the others except
 * where it is nested (the RX sweep contains its cache and copy buckets; the
 * whole-ISR bucket contains all the interrupt-side ones). The harness samples
 * this before and after a phase and diffs, so a phase's upload and download
 * halves are attributable separately.
 *
 * Nested, in order of containment:
 *   irq_ticks > irq_odd_ticks | irq_rx_sweep_ticks | irq_tx_done_ticks
 *   irq_rx_sweep_ticks > irq_rx_cache_ticks | irq_rx_copy_ticks
 *   rx_walk_ticks > irq_rx_cache_ticks | irq_rx_copy_ticks
 *   tx_total_ticks > tx_memcpy_ticks | tx_flush_ticks
 *
 * rx_walk_ticks is the sweep in WHICHEVER context ran it. irq_rx_sweep_ticks
 * only sees interrupt context, so it reads ~0 once the sweep moves to the RX
 * thread (RXBH/RXPOLL) - which is exactly when the sweep's cost still has to
 * be visible. rx_walk_slots counts the descriptor probes, the direct
 * discriminator between the blind whole-ring scan (128 per sweep) and the
 * bounded walk (one or two).
 */
/* The arrival-histogram shape: log2 buckets, the highest an overflow. Index =
 * position of the highest set bit, so bucket k spans [2^(k-1), 2^k) ticks
 * (41.7 ns each at 24 MHz); bucket 0 is "under one tick". Buckets resolve
 * gaps up to 2^16 ticks = 2.7 ms and 16 + 1 = 18 buckets far exceed any
 * line-rate spacing; the top bucket saturates at 2^17 ticks = 5.5 ms. */
#define DWC_EQOS_PROF_HIST_BUCKETS	18
static inline unsigned int gmac_prof_hist_bucket(uint64_t ticks)
{
	unsigned int b = 0u;

	while ((ticks >>= 1) != 0u) {
		b++;
	}
	return (b < (DWC_EQOS_PROF_HIST_BUCKETS - 1u)) ? b
						       : (DWC_EQOS_PROF_HIST_BUCKETS - 1u);
}

struct dwc_eqos_prof {
	uint64_t irq_ticks;		/* whole ISR, entry to exit */
	uint64_t irq_odd_ticks;		/* passes that service nothing */
	uint64_t irq_rx_sweep_ticks;	/* the descriptor-ring scan */
	uint64_t irq_rx_cache_ticks;	/* cache maintenance inside the sweep */
	uint64_t irq_rx_copy_ticks;	/* the ISR staging copy */
	uint64_t irq_tx_done_ticks;	/* the completion reclaim */
	uint64_t rx_walk_ticks;		/* the sweep loop, any context */
	uint64_t rx_walk_slots;		/* descriptor probes in the sweep */
	uint64_t tx_total_ticks;	/* send_frame, whole */
	uint64_t tx_memcpy_ticks;	/* send_frame's copy into the DMA buffer */
	uint64_t tx_flush_ticks;	/* send_frame's cache flush */
	uint64_t irq_entries;		/* samples, so a bucket can be averaged */
	uint64_t rx_frames;		/* frames through the sweep */
	uint64_t rx_sweeps;		/* eqos_rx_complete calls, any context:
					 * rx_frames/rx_sweeps is the batching
					 * factor the wake-cost accounting needs
					 * (~1 = wake per frame, as in the
					 * per-frame-RI baseline) */
	uint64_t tx_frames;		/* frames through send_frame */

	/* Frame-arrival rhythm (2026-10-19 instrument): the inter-arrival
	 * histogram of staged RX frames, bucketed by gmac_prof_hist_bucket()
	 * (log2, 41.7 ns per tick at 24 MHz). The TCP question this answers:
	 * does the peer's stream arrive as a bursty train separated by idle
	 * gaps (a pacing/feedback limit - samples pile into the ms buckets),
	 * or as a smooth line-rate wall (a per-frame-cost limit - everything
	 * in the sub-10-us buckets)? At the measured ~1 frame per sweep the
	 * staging instants ARE the arrival instants. */
	uint64_t rx_gap_hist[DWC_EQOS_PROF_HIST_BUCKETS];
	uint64_t rx_gap_samples;	/* intervals counted */
	uint64_t rx_last_stage_ticks;	/* last staging instant (absolute) */
	uint32_t rx_stage_high_water;	/* peak staged frames waiting for the
					 * reader: near 0 means the RX thread keeps
					 * up with the DMA; near the ring depth means
					 * the reader is the lagging consumer */

	/* Interrupt-shape census (2026-10-19). The
	 * whole-ISR bucket above says HOW MUCH the interrupt line costs; these
	 * split it so the avoidable class is identifiable. Entries are
	 * classified by the FIRST non-zero status word they saw; the *_ticks
	 * cells carry that entry's FULL entry-to-return cost, so an odd entry's
	 * GIC entry, prologue and status read sit inside its own bucket - the
	 * older irq_odd_ticks bucket starts after them and stays
	 * for continuity only. Passes count every loop iteration (the
	 * per-entry round-trip gauge); irq_pass_mac is the empty-word exits
	 * that had to clear a MAC-level latch (the N7-stab defense) and
	 * irq_benign is TBU/ETI-only passes served as traffic conditions under
	 * irq_oddmode=1. irq_max_ticks is the worst single entry. */
	uint64_t irq_ent_rx, irq_ent_tx, irq_ent_odd, irq_ent_fb, irq_ent_none;
	uint64_t irq_ent_rx_ticks, irq_ent_tx_ticks, irq_ent_odd_ticks,
		 irq_ent_fb_ticks, irq_ent_none_ticks;
	uint64_t irq_max_ticks;
	uint64_t irq_passes, irq_pass_rx, irq_pass_tx, irq_pass_odd,
		 irq_pass_mac, irq_pass_zero;
	uint64_t irq_benign;
};

void dwc_eqos_get_prof(unsigned int index, struct dwc_eqos_prof *prof);
void dwc_eqos_reset_prof(unsigned int index);

/* Clear only the arrival-rhythm fields of the profile (gap histogram,
 * samples, stage high-water, last-staging instant), so a measurement can reset
 * the rhythm instrument between phases without stomping the service-time
 * buckets the same `gmac prof` diff carries. */
void dwc_eqos_reset_rx_hist(unsigned int index);

/* Interrupt-source forensics (N7-stab freeze round): emergency-print every
 * block that shares this port's interrupt line (MAC status - read-clear,
 * MTL queue interrupt, MMC counter interrupts + masks). Call from a
 * context that still runs when the DMA-status handler is spinning (the
 * tick monitor): the read of MAC_INTERRUPT_STATUS clears its latch and
 * doubles as an unstick attempt. */
void dwc_eqos_irq_dump(unsigned int index);

/* MAC MMC hardware counters (read-clear-nothing; base 0x700 region). The
 * RX FIFO overflow counter counts frames dropped INSIDE the MAC/MTL before
 * any descriptor is involved - invisible to rx_dropped/rx_errors above. */
struct dwc_eqos_mmc_stats {
	uint32_t rx_pkts_gb;	/* 0x780 */
	uint32_t rx_octets_gb;	/* 0x784 */
	uint32_t rx_fifo_overflow;	/* 0x7d4 */
	uint32_t rx_crc_error;	/* 0x794 */
};

void dwc_eqos_get_mmc_stats(unsigned int index, struct dwc_eqos_mmc_stats *stats);

#endif /* FREEWIFICAM_DWC_EQOS_H */
