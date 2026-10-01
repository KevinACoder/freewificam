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
 * Port notes vs the prior workspace's CMSIS-Driver shape (specs/lwip-adapt
 * §3.4, the搬运≠照搬 list): the CMSIS instance macro and the Driver_ETH_MAC
 * vtable are gone; interrupts arm through gicv3 irq_register (the handler
 * takes the instance index as its argument); the ring lock is the port's own
 * interrupt line only (the NO_SYS=1 image has one thread - the main loop -
 * and the mutex covered thread-vs-thread); the osTimer watchdog became
 * dwc_eqos_watchdog(), called from the main loop; the RxFrame/TxFrame event
 * callback is gone (the main loop polls the staging ring). Every register
 * sequence, ring discipline and cache-maintenance point is unchanged.
 *
 * @author zhugengyu
 * @date   16.09.2026 (board-verified core) / 02.10.2026 (port re-arrangement)
 */

#ifndef FREEWEBCAM_DWC_EQOS_H
#define FREEWEBCAM_DWC_EQOS_H

#include <stdint.h>

#include "eqos_types.h"

/* Two GMACs on this SoC. */
#define DWC_EQOS_PORT_COUNT	2

/* Descriptor ring sizes and frame geometry. 64 slots each way is what the
 * emBox driver (and the netbsd/linux ports before it) ran with; the ring is
 * indexed with a wrap at the end, so both counts must match the hardware's
 * ring-length register (count - 1). */
#define DWC_EQOS_RX_DESC_COUNT	64
#define DWC_EQOS_TX_DESC_COUNT	64

/* Per-frame DMA buffer size. 1600, not 1518: the emBox port sized the DMA
 * buffers for jumbo-ish frames and the value is also the ceiling the tx path
 * checks a frame against. */
#define DWC_EQOS_RX_BUF_SIZE	1600

/* Every frame gets its own 2 KiB slot so that cache maintenance on one buffer
 * can never touch a neighbour: the dcache line is 64 bytes, and two frames
 * sharing a line would need flush/invalidate pairs that fight each other. */
#define DWC_EQOS_BUF_ALIGN	0x800
#define DWC_EQOS_BUF_STRIDE	0x800

/* Frames handed up to the driver's caller are staged here, because the
 * descriptor slot is re-armed immediately in the receive path (a slot left in
 * software hands is how the RX ring wedges - see the comment on rx_complete).
 * Deep enough to absorb a burst, shallow enough to be dropped rather than
 * delay: overflow increments a counter instead of stalling the ring. */
#define DWC_EQOS_RX_STAGE_COUNT	8

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

#define DWC_EQOS_MAC_HW_FEATURE1             0x120
#define  DWC_EQOS_HW_TXFIFOSZ_SHIFT          6
#define  DWC_EQOS_HW_TXFIFOSZ_MASK           (0x1fu << 6)
#define  DWC_EQOS_HW_RXFIFOSZ_MASK           0x1fu

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
#define  DWC_EQOS_CH0_STATUS_RI              (1u << 6)
#define  DWC_EQOS_CH0_STATUS_TI              (1u << 0)

/* --- descriptor ----------------------------------------------------------- */

/* 16-byte normal descriptor, contiguous ring (no skip length), i.e. the
 * "legacy" format and not the 32-byte alternate/context descriptor.
 *
 * TX: des2 = frame length [13:0] | IOC [31], des3 = OWN [31] | FD [29] |
 *     LD [28] | frame length [14:0].
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
#define DWC_EQOS_DESC_RX_LEN_MASK		0x7fffu

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

/* Statistics for the bring-up heartbeat: everything the driver had to drop or
 * could not complete, so a bad cable and a full ring are distinguishable. */
struct dwc_eqos_stats {
	uint32_t rx_frames;	/* delivered to the frame staging ring */
	uint32_t rx_dropped;	/* staging ring full */
	uint32_t rx_errors;	/* runt frames, bogus lengths */
	uint32_t tx_frames;	/* descriptors armed */
	uint32_t tx_dropped;	/* no descriptor became free in time */
	uint32_t dma_errors;	/* fatal bus errors seen in the status word */
};

void dwc_eqos_get_stats(unsigned int index, struct dwc_eqos_stats *stats);

#endif /* FREEWEBCAM_DWC_EQOS_H */
