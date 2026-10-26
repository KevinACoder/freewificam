/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   gmac.c
 * @brief  First-party driver for the on-SoC Ethernet MAC (Synopsys DWC GMAC,
 *         dwmac1000-class register layout).
 *
 * Derived from the reference standalone SDK's Ethernet MAC driver
 * (BSD-3-Clause, Copyright (C) 2022 Phytium Technology Co., Ltd.): the
 * reset/config order, the DMA bus-mode and operating-mode values and the
 * enhanced-descriptor ring contract are that driver's silicon-proven base.
 * The naming, the standard-clause-22 PHY handling (the reference drove an
 * 8035-class PHY through vendor registers; this board's PHY is driven
 * through the generic registers instead) and the copy-based send/receive
 * paths are this project's.
 *
 * Register layout: MAC registers at +0x0000 (GMII MDIO at +0x10, PHY
 * in-band status at +0xd8), DMA registers at +0x1000. Enhanced descriptors
 * are four words: status, control, buffer address (32-bit DMA, the whole
 * ring lives well below 4 GiB).
 *
 * DMA coherency: the SoC snoops this master's path (the same-boot OS runs
 * the same IP with dma-coherent); the driver issues no cache maintenance,
 * only a store barrier before handing a descriptor to the hardware.
 */

#include <string.h>

#include "log.h"

#include "gmac.h"

/* --- register offsets (MAC block) ----------------------------------------- */

#define GMAC_CONF			0x00u
#define GMAC_FRAME_FILTER		0x04u
#define GMAC_HASH_HIGH			0x08u
#define GMAC_HASH_LOW			0x0cu
#define GMAC_GMII_ADDR			0x10u
#define GMAC_GMII_DATA			0x14u
#define GMAC_FLOW_CTRL			0x18u
#define GMAC_VLAN_TAG			0x1cu
#define GMAC_INTR_STATUS		0x38u
#define GMAC_INTR_MASK			0x3cu
#define GMAC_MAC_ADDR0_HIGH		0x40u
#define GMAC_MAC_ADDR0_LOW		0x44u
#define GMAC_MMC_RX_INTR_MASK		0x010cu
#define GMAC_MMC_TX_INTR_MASK		0x0110u
#define GMAC_MMC_RX_IPC_INTR_MASK	0x0200u
#define GMAC_PHY_STATUS			0x00d8u	/* RGSMII in-band status */

/* --- register offsets (DMA block) ----------------------------------------- */

#define GMAC_DMA_BUS_MODE		0x1000u
#define GMAC_DMA_TX_POLL		0x1004u
#define GMAC_DMA_RX_POLL		0x1008u
#define GMAC_DMA_RX_LIST		0x100cu
#define GMAC_DMA_TX_LIST		0x1010u
#define GMAC_DMA_STATUS			0x1014u
#define GMAC_DMA_OP_MODE		0x1018u
#define GMAC_DMA_INTR_ENA		0x101cu
#define GMAC_DMA_AXI_MODE		0x1028u

/* --- MAC configuration bits ------------------------------------------------ */

#define GMAC_CONF_RX_EN			(1u << 2)
#define GMAC_CONF_TX_EN			(1u << 3)
#define GMAC_CONF_ACS			(1u << 7)
#define GMAC_CONF_DUPLEX		(1u << 11)
#define GMAC_CONF_LOOPBACK		(1u << 12)
#define GMAC_CONF_DO			(1u << 13)
#define GMAC_CONF_FES			(1u << 14)	/* 0:10, 1:100 (MII) */
#define GMAC_CONF_PORTSELECT		(1u << 15)	/* 0:GMII 1k, 1:MII */
#define GMAC_CONF_IFG_MASK		(7u << 17)
#define GMAC_CONF_JE			(1u << 20)
#define GMAC_CONF_BE			(1u << 21)
#define GMAC_CONF_JD			(1u << 22)
#define GMAC_CONF_WD			(1u << 23)
#define GMAC_CONF_CST			(1u << 25)
#define GMAC_CONF_DCRS			(1u << 16)

/* --- frame filter bits ------------------------------------------------------ */

#define GMAC_FF_PR			(1u << 0)
#define GMAC_FF_HUC			(1u << 1)
#define GMAC_FF_HMC			(1u << 2)
#define GMAC_FF_DAIF			(1u << 3)
#define GMAC_FF_PM			(1u << 4)
#define GMAC_FF_DBF			(1u << 5)
#define GMAC_FF_RA			(1u << 31)

/* --- flow control bits ------------------------------------------------------ */

#define GMAC_FLOW_DZPQ			(1u << 7)
#define GMAC_FLOW_RFE			(1u << 2)
#define GMAC_FLOW_TFE			(1u << 1)

/* --- PHY in-band status (RGSMII) ------------------------------------------- */

#define GMAC_RGSMII_LNKMODE		(1u << 0)	/* 1: full */
#define GMAC_RGSMII_SPEED_MASK		(3u << 1)
#define GMAC_RGSMII_SPEED_2_5		(0u << 1)
#define GMAC_RGSMII_SPEED_25		(1u << 1)
#define GMAC_RGSMII_SPEED_125		(2u << 1)
#define GMAC_RGSMII_LNKSTS		(1u << 3)	/* 1: up */

/* --- MDIO (GMII address register) ------------------------------------------ */

#define GMAC_MII_GB			(1u << 0)
#define GMAC_MII_GW			(1u << 1)
#define GMAC_MII_CR_MASK		(0xfu << 2)
#define GMAC_MII_CR(x)			(((x) & 0xfu) << 2)
#define GMAC_MII_GR(x)			(((x) & 0x1fu) << 6)
#define GMAC_MII_PA(x)			(((x) & 0x1fu) << 11)

/* --- DMA bus mode bits ------------------------------------------------------ */

#define GMAC_BUS_SWR			(1u << 0)
#define GMAC_BUS_AAL			(1u << 25)
#define GMAC_BUS_USP			(1u << 23)
#define GMAC_BUS_FB			(1u << 16)
#define GMAC_BUS_PBL(x)			(((x) & 0x3fu) << 8)
#define GMAC_BUS_RPBL(x)		(((x) & 0x3fu) << 17)

/* --- DMA operating mode bits ------------------------------------------------ */

#define GMAC_OP_TSF			(1u << 21)
#define GMAC_OP_FTF			(1u << 20)
#define GMAC_OP_TTC(x)			(((x) & 0x7u) << 14)
#define GMAC_OP_ST			(1u << 13)
#define GMAC_OP_OSF			(1u << 2)
#define GMAC_OP_RSF			(1u << 25)
#define GMAC_OP_SR			(1u << 1)

/* --- DMA status bits -------------------------------------------------------- */

#define GMAC_DMA_INTR_ALL \
	(0x1ffffu & ~((1u << 17) | (1u << 18) | (1u << 19) | (1u << 26) | \
		      (1u << 27) | (1u << 28) | (1u << 29) | (1u << 30) | \
		      (1u << 31)))
#define GMAC_STS_TU			(1u << 2)
#define GMAC_STS_RU			(1u << 7)
#define GMAC_STS_UNF			(1u << 5)

/* --- AXI bus mode bits ------------------------------------------------------ */

#define GMAC_AXI_BLEN4			(1u << 1)
#define GMAC_AXI_BLEN8			(1u << 2)
#define GMAC_AXI_BLEN16			(1u << 3)

/* --- enhanced descriptors --------------------------------------------------- */

struct gmac_desc {
	volatile uint32_t status;
	volatile uint32_t ctrl;
	volatile uint32_t buf_addr;
	uint32_t reserved;
};

#define GMAC_TDES0_OWN			(1u << 31)
#define GMAC_TDES1_SIZE1_MASK		0x7ffu
#define GMAC_TDES1_FS			(1u << 29)
#define GMAC_TDES1_LS			(1u << 30)
#define GMAC_TDES1_END_RING		(1u << 25)

#define GMAC_RDES0_OWN			(1u << 31)
#define GMAC_RDES0_FS			(1u << 9)
#define GMAC_RDES0_LS			(1u << 8)
#define GMAC_RDES0_ES			(1u << 15)
#define GMAC_RDES0_LEN_MASK		(0x3fffu << 16)
#define GMAC_RDES1_SIZE1_MASK		0x7ffu
#define GMAC_RDES1_END_RING		(1u << 25)

#define GMAC_TIMEOUT		200000u
#define GMAC_MDC_CSR		BOARD_GMAC0_MDC_CR

/* --- PHY registers (standard clause 22) ------------------------------------- */

#define MII_BMCR			0x00u
#define MII_BMSR			0x01u
#define MII_PHYSID1			0x02u
#define MII_PHYSID2			0x03u
#define MII_ADVERTISE			0x04u
#define MII_LPA				0x05u
#define MII_CTRL1000			0x09u
#define MII_STAT1000			0x0au

#define MII_BMCR_RESET			(1u << 15)
#define MII_BMCR_ANEG_EN		(1u << 12)
#define MII_BMCR_RESTART_ANEG		(1u << 9)
#define MII_BMSR_LINK			(1u << 2)
#define MII_BMSR_ANEG_DONE		(1u << 5)
#define MII_LPA_DUPLEX			(1u << 8)
#define MII_LPA_PAUSE			(1u << 10)
#define MII_STAT1000_LP_FULL		(1u << 11)
#define MII_STAT1000_LP_HALF		(1u << 10)
#define MII_PHY_ADDR_NONE		0xffu

/* --- the rings (driver-owned, one buffer per descriptor) --------------------- */

static struct gmac_desc gmac_rx_desc[BOARD_GMAC_RX_DESCS]
	__attribute__((aligned(16)));
static struct gmac_desc gmac_tx_desc[BOARD_GMAC_TX_DESCS]
	__attribute__((aligned(16)));
static uint8_t gmac_rx_buf[BOARD_GMAC_RX_DESCS][BOARD_GMAC_BUF_BYTES]
	__attribute__((aligned(8)));
static uint8_t gmac_tx_buf[BOARD_GMAC_TX_DESCS][BOARD_GMAC_BUF_BYTES]
	__attribute__((aligned(8)));

static uint32_t gmac_rx_idx;		/* next descriptor to examine */
static uint32_t gmac_tx_idx;		/* next free descriptor */

/* --- register access -------------------------------------------------------- */

static inline uint32_t gmac_rd(uintptr_t base, uint32_t off)
{
	return *(volatile uint32_t *)(base + off);
}

static inline void gmac_wr(uintptr_t base, uint32_t off, uint32_t val)
{
	*(volatile uint32_t *)(base + off) = val;
}

static inline void gmac_set(uintptr_t base, uint32_t off, uint32_t bits)
{
	gmac_wr(base, off, gmac_rd(base, off) | bits);
}

static inline void gmac_clr(uintptr_t base, uint32_t off, uint32_t bits)
{
	gmac_wr(base, off, gmac_rd(base, off) & ~bits);
}

/* The handover barrier: the descriptor's data is plain cached memory the
 * DMA snoops; the own-bit store must not be observed before the writes it
 * publishes. */
static inline void gmac_handover(void)
{
	__asm__ __volatile__("dsb st" ::: "memory");
}

/* --- MDIO ------------------------------------------------------------------- */

static int gmac_mdio_wait(uintptr_t base)
{
	uint32_t t = GMAC_TIMEOUT;

	while ((gmac_rd(base, GMAC_GMII_ADDR) & GMAC_MII_GB) != 0u) {
		if (--t == 0u) {
			return -1;
		}
	}
	return 0;
}

static int gmac_phy_read(struct gmac *m, uint8_t addr, uint16_t reg,
			 uint16_t *val)
{
	uintptr_t base = m->base;

	if (gmac_mdio_wait(base) != 0) {
		return -1;
	}
	gmac_wr(base, GMAC_GMII_ADDR,
		GMAC_MII_CR(GMAC_MDC_CSR) | GMAC_MII_PA(addr) |
		GMAC_MII_GR(reg) | GMAC_MII_GB);
	if (gmac_mdio_wait(base) != 0) {
		return -1;
	}
	*val = (uint16_t)(gmac_rd(base, GMAC_GMII_DATA) & 0xffffu);
	return 0;
}

static int gmac_phy_write(struct gmac *m, uint8_t addr, uint16_t reg,
			  uint16_t val)
{
	uintptr_t base = m->base;

	if (gmac_mdio_wait(base) != 0) {
		return -1;
	}
	gmac_wr(base, GMAC_GMII_DATA, val);
	gmac_wr(base, GMAC_GMII_ADDR,
		GMAC_MII_CR(GMAC_MDC_CSR) | GMAC_MII_PA(addr) |
		GMAC_MII_GR(reg) | GMAC_MII_GW | GMAC_MII_GB);
	return gmac_mdio_wait(base);
}

/* --- MAC address ------------------------------------------------------------ */

static void gmac_addr_read(uintptr_t base, uint8_t mac[6])
{
	uint32_t hi = gmac_rd(base, GMAC_MAC_ADDR0_HIGH);
	uint32_t lo = gmac_rd(base, GMAC_MAC_ADDR0_LOW);

	mac[0] = (uint8_t)(lo >> 0);
	mac[1] = (uint8_t)(lo >> 8);
	mac[2] = (uint8_t)(lo >> 16);
	mac[3] = (uint8_t)(lo >> 24);
	mac[4] = (uint8_t)(hi >> 0);
	mac[5] = (uint8_t)(hi >> 8);
}

static void gmac_addr_write(uintptr_t base, const uint8_t mac[6])
{
	gmac_wr(base, GMAC_MAC_ADDR0_HIGH,
		((uint32_t)mac[5] << 8) | (uint32_t)mac[4]);
	gmac_wr(base, GMAC_MAC_ADDR0_LOW,
		((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
		((uint32_t)mac[1] << 8) | (uint32_t)mac[0]);
}

static int gmac_addr_valid(const uint8_t mac[6])
{
	int all_zero = (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0;
	int all_ones = (mac[0] & mac[1] & mac[2] & mac[3] & mac[4] & mac[5]) == 0xff;
	int is_multicast = (mac[0] & 1u) != 0u;

	return !all_zero && !all_ones && !is_multicast;
}

/* --- interrupt masks (everything stays masked: fully polled) ---------------- */

static void gmac_mask_all(uintptr_t base)
{
	gmac_wr(base, GMAC_INTR_MASK, 0x1ffu);
	gmac_wr(base, GMAC_DMA_INTR_ENA, 0u);
	gmac_wr(base, GMAC_DMA_STATUS, GMAC_DMA_INTR_ALL);
}

/* --- PHY detect (clause-22 address scan) ------------------------------------ */

static int gmac_phy_detect(struct gmac *m)
{
	uint16_t id1 = 0xffffu;
	uint16_t id2 = 0xffffu;
	uint16_t bmsr = 0xffffu;
	uint8_t addr;

	for (addr = 0u; addr < 32u; addr++) {
		if (gmac_phy_read(m, addr, MII_BMSR, &bmsr) != 0) {
			return -1;
		}
		if (bmsr == 0xffffu) {
			continue;
		}
		if (gmac_phy_read(m, addr, MII_PHYSID1, &id1) != 0 ||
		    gmac_phy_read(m, addr, MII_PHYSID2, &id2) != 0) {
			return -1;
		}
		if (id1 != 0xffffu && id1 != 0x0000u) {
			m->phy_addr = addr;
			m->phy_id1 = id1;
			board_log("gmac: phy addr=%u id=%04x:%04x\n",
				  addr, id1, id2);
			return 0;
		}
	}
	return -1;
}

/* Link state from the PHY's own registers: BMSR's link bit is latched-low,
 * so it is read twice before it is believed (the clause-22 read protocol). */
static int gmac_phy_link(struct gmac *m, unsigned int *up)
{
	uint16_t bmsr = 0;

	if (gmac_phy_read(m, m->phy_addr, MII_BMSR, &bmsr) != 0 ||
	    gmac_phy_read(m, m->phy_addr, MII_BMSR, &bmsr) != 0) {
		return -1;
	}
	*up = (bmsr & MII_BMSR_LINK) != 0u;
	return 0;
}

/* Speed/duplex: the MAC's in-band status register first (the PHY repeats
 * the negotiated result over the RGMII inband channel; the reference
 * driver's proven source on this silicon), the MDIO advertisement registers
 * as the fallback when the inband copy reads down against a live link. */
static int gmac_phy_result(struct gmac *m, struct gmac_link *out)
{
	uintptr_t base = m->base;
	uint32_t rg = gmac_rd(base, GMAC_PHY_STATUS);
	uint16_t stat1000 = 0;
	uint16_t lpa = 0;

	out->up = 1u;
	if ((rg & GMAC_RGSMII_LNKSTS) != 0u) {
		switch (rg & GMAC_RGSMII_SPEED_MASK) {
		case GMAC_RGSMII_SPEED_125:
			out->speed_mbps = 1000u;
			break;
		case GMAC_RGSMII_SPEED_25:
			out->speed_mbps = 100u;
			break;
		default:
			out->speed_mbps = 10u;
			break;
		}
		out->full_duplex = (rg & GMAC_RGSMII_LNKMODE) != 0u;
		return 0;
	}

	/* Inband copy stale: derive from the PHY's registers. */
	if (gmac_phy_read(m, m->phy_addr, MII_STAT1000, &stat1000) != 0 ||
	    gmac_phy_read(m, m->phy_addr, MII_LPA, &lpa) != 0) {
		return -1;
	}
	if ((stat1000 & (MII_STAT1000_LP_FULL | MII_STAT1000_LP_HALF)) != 0u) {
		out->speed_mbps = 1000u;
		out->full_duplex = (stat1000 & MII_STAT1000_LP_FULL) != 0u;
	} else if ((lpa & (0x4000u | 0x2000u)) != 0u) {
		out->speed_mbps = 100u;
		out->full_duplex = (lpa & (0x4000u | MII_LPA_DUPLEX)) != 0u;
	} else {
		out->speed_mbps = 10u;
		out->full_duplex = (lpa & MII_LPA_DUPLEX) != 0u;
	}
	return 0;
}

/* Align the MAC's speed/duplex bits with the negotiated result. */
static void gmac_apply_link(struct gmac *m, const struct gmac_link *link)
{
	uintptr_t base = m->base;
	uint32_t conf = gmac_rd(base, GMAC_CONF);

	if (link->speed_mbps >= 1000u) {
		conf &= ~(GMAC_CONF_PORTSELECT | GMAC_CONF_FES);
	} else if (link->speed_mbps == 100u) {
		conf |= GMAC_CONF_PORTSELECT | GMAC_CONF_FES;
	} else {
		conf |= GMAC_CONF_PORTSELECT;
		conf &= ~GMAC_CONF_FES;
	}
	if (link->full_duplex) {
		conf |= GMAC_CONF_DUPLEX;
	} else {
		conf &= ~GMAC_CONF_DUPLEX;
	}
	gmac_wr(base, GMAC_CONF, conf);
}

/* --- ring construction ------------------------------------------------------- */

static void gmac_rx_ring_build(uintptr_t base)
{
	uint32_t i;

	memset(gmac_rx_desc, 0, sizeof(gmac_rx_desc));
	for (i = 0u; i < BOARD_GMAC_RX_DESCS; i++) {
		gmac_rx_desc[i].status = GMAC_RDES0_OWN;
		gmac_rx_desc[i].ctrl = BOARD_GMAC_BUF_BYTES &
				       GMAC_RDES1_SIZE1_MASK;
		gmac_rx_desc[i].buf_addr = (uint32_t)(uintptr_t)gmac_rx_buf[i];
		if (i == BOARD_GMAC_RX_DESCS - 1u) {
			gmac_rx_desc[i].ctrl |= GMAC_RDES1_END_RING;
		}
	}
	gmac_rx_idx = 0u;
	gmac_handover();
	gmac_wr(base, GMAC_DMA_RX_LIST,
		(uint32_t)(uintptr_t)gmac_rx_desc);
}

static void gmac_tx_ring_build(uintptr_t base)
{
	uint32_t i;

	memset(gmac_tx_desc, 0, sizeof(gmac_tx_desc));
	for (i = 0u; i < BOARD_GMAC_TX_DESCS; i++) {
		gmac_tx_desc[i].buf_addr = (uint32_t)(uintptr_t)gmac_tx_buf[i];
	}
	gmac_tx_desc[BOARD_GMAC_TX_DESCS - 1u].ctrl = GMAC_TDES1_END_RING;
	gmac_tx_idx = 0u;
	gmac_handover();
	gmac_wr(base, GMAC_DMA_TX_LIST,
		(uint32_t)(uintptr_t)gmac_tx_desc);
}

/* --- public API --------------------------------------------------------------- */

int gmac_init(struct gmac *m, uintptr_t base)
{
	uint32_t conf;
	uint32_t bus;
	uint32_t op;
	uint32_t t;
	uint8_t mac[6];

	memset(m, 0, sizeof(*m));
	m->base = base;
	m->phy_addr = MII_PHY_ADDR_NONE;

	/* MAC address: back it up before the reset clears it. The boot
	 * loader programmed a working one (it transferred this image); if the
	 * backup reads invalid, fall back to a locally administered address. */
	gmac_addr_read(base, mac);
	if (gmac_addr_valid(mac)) {
		memcpy(m->mac, mac, sizeof(m->mac));
	} else {
		m->mac[0] = 0x02u;	/* locally administered, unicast */
		m->mac[1] = 0x11u;
		m->mac[2] = 0x22u;
		m->mac[3] = 0x33u;
		m->mac[4] = 0x44u;
		m->mac[5] = 0x01u;
	}

	/* Quiesce, then full MAC+DMA software reset. */
	gmac_mask_all(base);
	gmac_clr(base, GMAC_DMA_OP_MODE, GMAC_OP_SR | GMAC_OP_ST);
	gmac_clr(base, GMAC_CONF, GMAC_CONF_RX_EN | GMAC_CONF_TX_EN);

	gmac_set(base, GMAC_DMA_BUS_MODE, GMAC_BUS_SWR);
	t = GMAC_TIMEOUT;
	while ((gmac_rd(base, GMAC_DMA_BUS_MODE) & GMAC_BUS_SWR) != 0u) {
		if (--t == 0u) {
			board_log("gmac: software reset timeout\n");
			return -1;
		}
	}

	/* Post-reset: silence the MMC counters and every interrupt source,
	 * then put the MAC address back. */
	gmac_wr(base, GMAC_MMC_RX_INTR_MASK, 0xffffffffu);
	gmac_wr(base, GMAC_MMC_TX_INTR_MASK, 0xffffffffu);
	gmac_wr(base, GMAC_MMC_RX_IPC_INTR_MASK, 0xffffffffu);
	gmac_mask_all(base);
	gmac_addr_write(base, m->mac);

	/* MAC configuration: the reference driver's proven bit set - CRC
	 * strip for type frames, watchdog and jabber off, burst enable, no
	 * own-frame reception, automatic pad/CRC strip; checksum offload off
	 * (the stack verifies in software); 1000/full until the PHY answers. */
	conf = gmac_rd(base, GMAC_CONF);
	conf &= ~(GMAC_CONF_DCRS | GMAC_CONF_LOOPBACK |
		  GMAC_CONF_PORTSELECT | GMAC_CONF_FES | GMAC_CONF_IFG_MASK);
	conf |= GMAC_CONF_CST | GMAC_CONF_WD | GMAC_CONF_JD | GMAC_CONF_BE |
		GMAC_CONF_DO | GMAC_CONF_ACS | GMAC_CONF_DUPLEX;
	gmac_wr(base, GMAC_CONF, conf);

	/* Frame filter: the reset default - perfect unicast plus broadcast,
	 * no promiscuous mode, no hash multicast (the stack answers ARP; any
	 * multicast need arrives with a hash entry round). */
	gmac_clr(base, GMAC_FRAME_FILTER,
		 GMAC_FF_RA | GMAC_FF_PR | GMAC_FF_PM | GMAC_FF_HMC |
		 GMAC_FF_HUC | GMAC_FF_DBF | GMAC_FF_DAIF);
	gmac_wr(base, GMAC_HASH_HIGH, 0u);
	gmac_wr(base, GMAC_HASH_LOW, 0u);

	/* Flow control off (the bench runs full rate both ways). */
	gmac_set(base, GMAC_FLOW_CTRL, GMAC_FLOW_DZPQ);
	gmac_clr(base, GMAC_FLOW_CTRL, GMAC_FLOW_RFE | GMAC_FLOW_TFE);

	/* DMA bus mode: fixed burst, PBL/RPBL 16 (the board's own OS
	 * configuration values), separate RX PBL, address-aligned beats. */
	bus = gmac_rd(base, GMAC_DMA_BUS_MODE);
	bus |= GMAC_BUS_FB | GMAC_BUS_PBL(16) | GMAC_BUS_RPBL(16) |
	       GMAC_BUS_USP | GMAC_BUS_AAL;
	gmac_wr(base, GMAC_DMA_BUS_MODE, bus);

	/* Operating mode: store-and-forward both directions (the board's
	 * force-sf configuration), transmit threshold max, operate on second
	 * frame. */
	op = gmac_rd(base, GMAC_DMA_OP_MODE);
	op &= ~(GMAC_OP_TTC(7));
	op |= GMAC_OP_RSF | GMAC_OP_TSF | GMAC_OP_TTC(7) | GMAC_OP_OSF;
	gmac_wr(base, GMAC_DMA_OP_MODE, op);

	/* Flush the transmit FIFO after the mode change. */
	gmac_set(base, GMAC_DMA_OP_MODE, GMAC_OP_FTF);
	t = GMAC_TIMEOUT;
	while ((gmac_rd(base, GMAC_DMA_OP_MODE) & GMAC_OP_FTF) != 0u) {
		if (--t == 0u) {
			board_log("gmac: tx fifo flush timeout\n");
			return -1;
		}
	}

	/* AXI burst lengths 16/8/4 (the board's own OS configuration). */
	gmac_wr(base, GMAC_DMA_AXI_MODE,
		GMAC_AXI_BLEN16 | GMAC_AXI_BLEN8 | GMAC_AXI_BLEN4);

	/* PHY: detect only. The negotiation state is whatever the boot
	 * loader's tftp session left (typically already up at 1000/full); no
	 * PHY reset, no autoneg restart unless the link reads down. */
	if (gmac_phy_detect(m) != 0) {
		board_log("gmac: no clause-22 phy on the mdio bus\n");
		return -1;
	}

	board_log("gmac: controller reset, mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		  m->mac[0], m->mac[1], m->mac[2], m->mac[3], m->mac[4],
		  m->mac[5]);
	return 0;
}

int gmac_phy_step(struct gmac *m)
{
	unsigned int up = 0u;
	uint16_t bmcr = 0;

	if (m->phy_addr == MII_PHY_ADDR_NONE) {
		return -1;
	}
	if (gmac_phy_link(m, &up) != 0) {
		return -1;
	}

	if (up == 0u) {
		/* Link down: restart autonegotiation once, then keep
		 * reporting in-progress while the PHY settles. */
		if (m->an_restarted == 0) {
			if (gmac_phy_read(m, m->phy_addr, MII_BMCR, &bmcr) != 0) {
				return -1;
			}
			bmcr |= MII_BMCR_ANEG_EN | MII_BMCR_RESTART_ANEG;
			if (gmac_phy_write(m, m->phy_addr, MII_BMCR,
					   bmcr) != 0) {
				return -1;
			}
			m->an_restarted = 1;
			board_log("gmac: link down, autoneg restarted\n");
		}
		m->link.up = 0u;
		return 0;
	}

	if (gmac_phy_result(m, &m->link) != 0) {
		return -1;
	}
	m->link.up = 1u;
	gmac_apply_link(m, &m->link);
	return 1;
}

void gmac_start(struct gmac *m)
{
	uintptr_t base = m->base;

	gmac_rx_ring_build(base);
	gmac_tx_ring_build(base);
	gmac_wr(base, GMAC_DMA_STATUS, GMAC_DMA_INTR_ALL);
	gmac_set(base, GMAC_DMA_OP_MODE, GMAC_OP_SR | GMAC_OP_ST);
	gmac_set(base, GMAC_CONF, GMAC_CONF_RX_EN | GMAC_CONF_TX_EN);
}

void gmac_stop(struct gmac *m)
{
	uintptr_t base = m->base;

	gmac_clr(base, GMAC_DMA_OP_MODE, GMAC_OP_SR | GMAC_OP_ST);
	gmac_clr(base, GMAC_CONF, GMAC_CONF_RX_EN | GMAC_CONF_TX_EN);
}

int gmac_tx_frame(struct gmac *m, const void *frame, size_t len)
{
	struct gmac_desc *d;

	if (len == 0u || len > BOARD_GMAC_BUF_BYTES) {
		return -1;
	}

	d = &gmac_tx_desc[gmac_tx_idx];
	if ((d->status & GMAC_TDES0_OWN) != 0u) {
		/* One step back and re-check the whole ring once: the DMA may
		 * have completed earlier frames since this descriptor was
		 * last looked at. */
		uint32_t walk = gmac_tx_idx;
		uint32_t i;

		for (i = 0u; i < BOARD_GMAC_TX_DESCS; i++) {
			struct gmac_desc *w = &gmac_tx_desc[walk];

			if ((w->status & GMAC_TDES0_OWN) == 0u) {
				gmac_tx_idx = walk;
				d = w;
				break;
			}
			walk = (walk + 1u) % BOARD_GMAC_TX_DESCS;
		}
		if ((d->status & GMAC_TDES0_OWN) != 0u) {
			m->stats.tx_busy++;
			gmac_set(m->base, GMAC_DMA_TX_POLL, 0u);
			return -1;
		}
	}

	memcpy(gmac_tx_buf[gmac_tx_idx], frame, len);
	d->ctrl &= ~GMAC_TDES1_SIZE1_MASK;
	d->ctrl |= ((uint32_t)len & GMAC_TDES1_SIZE1_MASK) |
		   GMAC_TDES1_FS | GMAC_TDES1_LS;
	gmac_handover();
	d->status = GMAC_TDES0_OWN;
	gmac_tx_idx = (gmac_tx_idx + 1u) % BOARD_GMAC_TX_DESCS;
	gmac_wr(m->base, GMAC_DMA_TX_POLL, 0u);

	m->stats.tx_frames++;
	return 0;
}

static void gmac_rx_release(uint32_t from, uint32_t to_excl)
{
	uint32_t i = from;

	for (;;) {
		gmac_rx_desc[i].ctrl &= ~GMAC_RDES1_SIZE1_MASK;
		gmac_rx_desc[i].ctrl |= BOARD_GMAC_BUF_BYTES &
					GMAC_RDES1_SIZE1_MASK;
		gmac_handover();
		gmac_rx_desc[i].status = GMAC_RDES0_OWN;
		if (i == to_excl) {
			break;
		}
		i = (i + 1u) % BOARD_GMAC_RX_DESCS;
	}
}

int gmac_rx_frame(struct gmac *m, uint8_t *buf, size_t buf_size)
{
	uint32_t i = gmac_rx_idx;
	uint32_t first = gmac_rx_idx;
	uint32_t walked = 0u;
	uint32_t len = 0u;
	int have_first = 0;

	while (walked < BOARD_GMAC_RX_DESCS) {
		struct gmac_desc *d = &gmac_rx_desc[i];

		if ((d->status & GMAC_RDES0_OWN) != 0u) {
			break;
		}
		walked++;

		if ((d->status & GMAC_RDES0_FS) != 0u) {
			first = i;
			have_first = 1;
		}
		if ((d->status & GMAC_RDES0_LS) != 0u) {
			if (!have_first) {
				/* Torn frame (missed its first segment):
				 * release and resynchronise. */
				gmac_rx_release(gmac_rx_idx, i);
				gmac_rx_idx = (i + 1u) % BOARD_GMAC_RX_DESCS;
				gmac_wr(m->base, GMAC_DMA_RX_POLL, 0u);
				return 0;
			}
			len = (d->status & GMAC_RDES0_LEN_MASK) >> 16;
			if ((d->status & GMAC_RDES0_ES) != 0u) {
				m->stats.rx_crc_errors++;
			} else if (len > buf_size) {
				m->stats.rx_dropped++;
			} else {
				/* Single-segment frames cover everything the
				 * bench sends; the multi-segment path walks
				 * the ring anyway. */
				size_t left = len;
				uint32_t seg = first;

				while (left > 0u) {
					size_t n = left > BOARD_GMAC_BUF_BYTES
						   ? BOARD_GMAC_BUF_BYTES
						   : left;

					memcpy(buf, gmac_rx_buf[seg], n);
					buf += n;
					left -= n;
					if (seg == i) {
						break;
					}
					seg = (seg + 1u) %
					      BOARD_GMAC_RX_DESCS;
				}
				m->stats.rx_frames++;
				m->stats.rx_bytes += len;
			}
			gmac_rx_release(first, i);
			gmac_rx_idx = (i + 1u) % BOARD_GMAC_RX_DESCS;
			gmac_wr(m->base, GMAC_DMA_RX_POLL, 0u);
			return (int)len;
		}
		i = (i + 1u) % BOARD_GMAC_RX_DESCS;
	}

	/* No complete frame: leave the walk for the next call. */
	return 0;
}

void gmac_link_poll(struct gmac *m)
{
	unsigned int up = 0u;

	if (m->phy_addr == MII_PHY_ADDR_NONE) {
		return;
	}
	if (gmac_phy_link(m, &up) != 0) {
		return;
	}
	if (up != 0u && m->link.up == 0u) {
		(void)gmac_phy_result(m, &m->link);
		m->link.up = 1u;
		gmac_apply_link(m, &m->link);
	} else if (up == 0u && m->link.up != 0u) {
		m->link.up = 0u;
	}
}

void gmac_get_stats(const struct gmac *m, struct gmac_stats *out)
{
	*out = m->stats;
}
