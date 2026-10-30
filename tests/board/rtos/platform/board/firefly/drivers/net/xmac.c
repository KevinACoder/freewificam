/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   xmac.c
 * @brief  First-party driver for the firefly v2 board's on-SoC Ethernet MAC
 *         (Cadence GEM-class controller behind an SGMII SerDes).
 *
 * Register facts were taken from the SoC parameter map and cross-checked
 * against the board's own mainline driver, which runs this exact port at line
 * rate; the code below - the ring ownership discipline, the polled copy data
 * path, the clause-22 PHY handling and the diagnostic surface - is this
 * project's (see LICENSES.md next to this file).
 *
 * Layout: MAC registers at +0x0000 (MDIO at +0x34, station address at +0x88,
 * high-speed config at +0x50), DMA queue pointers at +0x0018/+0x001c, and the
 * SerDes clock-select bank at +0x1c00. Descriptors are two words - buffer
 * address, then control/status - and this image runs them 32-bit (the whole
 * ring sits below 4 GiB, and the board's own configuration leaves the
 * 64-bit-descriptor bit clear).
 *
 * Ring ownership, which differs from the usual convention and is the one
 * mistake that hangs reception:
 *   - RX: the ADDRESS word's bit 0 is the used flag. Zero means the DMA owns
 *     the buffer; the hardware sets it when it has written a frame. Software
 *     hands a buffer back by storing the address again with bit 0 clear.
 *   - TX: the CONTROL word's bit 31 is the used flag, set by the hardware on
 *     completion and by software at init. A set bit means the slot is free.
 *   - If the receiver ever walks onto a buffer still marked used it stops and
 *     raises "used bit read"; the drain writes that status back to restart it.
 *
 * DMA memory: descriptors live in the `.noncacheable` window (an 8-byte
 * descriptor inside a 64-byte line cannot be maintained by hand - flushing to
 * arm one would push a stale copy of its completed neighbours over the DMA's
 * writeback). Frame buffers stay in cacheable .bss and are cleaned before a
 * transmit and invalidated before a receive is read, so the driver is correct
 * whether the port snoops or not.
 */

#include <string.h>

#include "cache.h"
#include "log.h"

#include "xmac.h"

/* --- register offsets (MAC block) ----------------------------------------- */

#define XMAC_NCR			0x0000u
#define XMAC_NCFGR			0x0004u
#define XMAC_NSR			0x0008u
#define XMAC_TSR			0x0014u
#define XMAC_TBQP			0x001cu
#define XMAC_RSR			0x0020u
#define XMAC_ISR			0x0024u
#define XMAC_IDR			0x002cu
#define XMAC_IER			0x0028u
#define XMAC_IMR			0x0030u
/* Receive-complete is the one line the drain needs: it fires when a descriptor
 * has been written back, i.e. exactly when there is work for the RX thread.
 * RLE/ROVR/RXUBR are the error companions - they must be in the mask too, or a
 * single overrun latches the status bit and the receiver stops raising
 * anything for the rest of the boot. */
#define XMAC_IXR_RCOMP			(1u << 1)
#define XMAC_IXR_RXUBR			(1u << 2)
#define XMAC_IXR_RLE			(1u << 4)
#define XMAC_IXR_ROVR			(1u << 10)
#define XMAC_MAN			0x0034u
#define XMAC_HS_MAC_CONFIG		0x0050u
#define XMAC_AXI_PIPE			0x0054u
#define XMAC_PCS_CONTROL		0x0200u
#define XMAC_SA1B			0x0088u
#define XMAC_SA1T			0x008cu
#define XMAC_SA2B			0x0090u
#define XMAC_SA4T			0x00a4u
#define XMAC_DCFG1			0x0280u

/* --- register offsets (DMA block: legacy queue-0 mapping) ------------------ */

#define XMAC_DMACFG			0x0010u
#define XMAC_RBQP			0x0018u

/* --- SerDes line-network / clock-select bank ------------------------------- */

#define XMAC_SRC_SEL_LN			0x1c04u
#define XMAC_DIV_SEL0_LN		0x1c08u
#define XMAC_DIV_SEL1_LN		0x1c0cu
#define XMAC_PMA_POWER_STATE		0x1c10u
#define XMAC_TX_CLK_SEL0		0x1c20u
#define XMAC_TX_CLK_SEL1		0x1c24u
#define XMAC_TX_CLK_SEL2		0x1c28u
#define XMAC_TX_CLK_SEL3		0x1c2cu
#define XMAC_RX_CLK_SEL0		0x1c30u
#define XMAC_RX_CLK_SEL1		0x1c34u
#define XMAC_TX_CLK_SEL3_0		0x1c70u
#define XMAC_TX_CLK_SEL4_0		0x1c74u
#define XMAC_RX_CLK_SEL3_0		0x1c78u
#define XMAC_RX_CLK_SEL4_0		0x1c7cu

/* --- network control ------------------------------------------------------- */

#define XMAC_NCR_RE			(1u << 2)
#define XMAC_NCR_TE			(1u << 3)
#define XMAC_NCR_MPE			(1u << 4)	/* MDIO master enable */
#define XMAC_NCR_CLRSTAT		(1u << 5)
#define XMAC_NCR_TSTART			(1u << 9)

/* --- network config -------------------------------------------------------- */

#define XMAC_NCFGR_SPD			(1u << 0)	/* 100 Mbit */
#define XMAC_NCFGR_FD			(1u << 1)
#define XMAC_NCFGR_CAF			(1u << 4)	/* copy all frames */
#define XMAC_NCFGR_NBC			(1u << 5)	/* no broadcast */
#define XMAC_NCFGR_BIG			(1u << 8)	/* 1536-byte frames */
#define XMAC_NCFGR_GBE			(1u << 10)	/* 1000 Mbit */
#define XMAC_NCFGR_PCSSEL		(1u << 11)
#define XMAC_NCFGR_PAE			(1u << 13)
#define XMAC_NCFGR_DRFCS		(1u << 17)	/* strip the FCS */
#define XMAC_NCFGR_CLK_MASK		(7u << 18)
#define XMAC_NCFGR_CLK(x)		(((x) & 7u) << 18)
#define XMAC_NCFGR_DBW_MASK		(3u << 21)
#define XMAC_NCFGR_DBW(x)		(((x) & 3u) << 21)
#define XMAC_NCFGR_RXCOEN		(1u << 24)
#define XMAC_NCFGR_SGMIIEN		(1u << 27)

/* --- DMA configuration ------------------------------------------------------ */

#define XMAC_DMACFG_FBLDO(x)		((x) & 0x1fu)
#define XMAC_DMACFG_ENDIA_DESC		(1u << 6)
#define XMAC_DMACFG_ENDIA_PKT		(1u << 7)
#define XMAC_DMACFG_RXBMS_MASK		(3u << 8)
#define XMAC_DMACFG_TXPBMS		(1u << 10)
#define XMAC_DMACFG_TXCOEN		(1u << 11)
#define XMAC_DMACFG_RXBS_MASK		(0xffu << 16)
#define XMAC_DMACFG_RXBS(x)		(((x) & 0xffu) << 16)
#define XMAC_DMACFG_ADDR64		(1u << 30)

/* --- descriptors ------------------------------------------------------------ */

struct xmac_desc {
	volatile uint32_t addr;		/* buffer address; bit 0 = RX used */
	volatile uint32_t ctrl;		/* RX status / TX control */
};

/* RX address word */
#define XMAC_RXD_USED			(1u << 0)	/* set by hw on completion */
#define XMAC_RXD_WRAP			(1u << 1)

/* RX control word */
#define XMAC_RXC_LEN_MASK		0x0fffu
#define XMAC_RXC_SOF			(1u << 14)
#define XMAC_RXC_EOF			(1u << 15)

/* TX control word */
#define XMAC_TXC_LEN_MASK		0x3fffu
#define XMAC_TXC_LAST			(1u << 15)
#define XMAC_TXC_WRAP			(1u << 30)
#define XMAC_TXC_USED			(1u << 31)

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
#define MII_LPA_100			(1u << 7)
#define MII_STAT1000_LP_FULL		(1u << 11)
#define MII_STAT1000_LP_HALF		(1u << 10)
#define MII_PHY_ADDR_NONE		0xffu

/* PCS control: enable autonegotiation on the SGMII side. */
#define XMAC_PCS_ANEG_ENABLE		(1u << 12)

/* --- MDIO frame fields ------------------------------------------------------ */

#define XMAC_MAN_SOF			(1u << 30)	/* clause 22 */
#define XMAC_MAN_WRITE			(1u << 28)
#define XMAC_MAN_READ			(2u << 28)
#define XMAC_MAN_PHYA(x)		(((x) & 0x1fu) << 23)
#define XMAC_MAN_REGA(x)		(((x) & 0x1fu) << 18)
#define XMAC_MAN_CODE			(2u << 16)
#define XMAC_MAN_DATA_MASK		0xffffu

/* NSR: MDIO logic idle. */
#define XMAC_NSR_IDLE			(1u << 2)

/* --- SerDes / high-speed programming constants ------------------------------ */

/* The SerDes clock tree feeding an SGMII link: line source select, the two
 * dividers and the PMA power state, plus the RX clock pair. Proven on this
 * port at both 1 Gbit/s and 100 Mbit/s. */
#define XMAC_LN_SRC_SEL			1u
#define XMAC_LN_DIV0			4u
#define XMAC_LN_DIV1			8u
#define XMAC_LN_PMA_ON			1u
#define XMAC_HS_SPEED_100M		0u
#define XMAC_HS_SPEED_1000M		1u

/* AXI pipeline depth the port needs before its clock selects are valid. */
#define XMAC_AXI_PIPE_VALUE		0x1010u

#define XMAC_TIMEOUT			200000u
#define XMAC_MDC_DIV			BOARD_XMAC0_MDC_DIV

/* --- the rings (driver-owned, one buffer per descriptor) -------------------- */

#define XMAC_NOC_SECTION \
	__attribute__((section(".noncacheable"), aligned(64)))

static struct xmac_desc xmac_rx_desc[BOARD_XMAC_RX_DESCS] XMAC_NOC_SECTION;static struct xmac_desc xmac_tx_desc[BOARD_XMAC_TX_DESCS] XMAC_NOC_SECTION;
static uint8_t xmac_rx_buf[BOARD_XMAC_RX_DESCS][BOARD_XMAC_BUF_BYTES]
	__attribute__((aligned(64)));
static uint8_t xmac_tx_buf[BOARD_XMAC_TX_DESCS][BOARD_XMAC_BUF_BYTES]
	__attribute__((aligned(64)));

static uint32_t xmac_rx_idx;		/* next descriptor to examine */
static uint32_t xmac_tx_idx;		/* next free descriptor */

/* --- register access -------------------------------------------------------- */

static inline uint32_t xmac_rd(uintptr_t base, uint32_t off)
{
	return *(volatile uint32_t *)(base + off);
}

static inline void xmac_wr(uintptr_t base, uint32_t off, uint32_t val)
{
	*(volatile uint32_t *)(base + off) = val;
}

static inline void xmac_set(uintptr_t base, uint32_t off, uint32_t bits)
{
	xmac_wr(base, off, xmac_rd(base, off) | bits);
}

static inline void xmac_clr(uintptr_t base, uint32_t off, uint32_t bits)
{
	xmac_wr(base, off, xmac_rd(base, off) & ~bits);
}

/* The handover barrier: descriptor and buffer stores must reach memory before
 * the hardware is allowed to act on them. */
static inline void xmac_handover(void)
{
	__asm__ __volatile__("dsb st" ::: "memory");
}

/* --- MDIO ------------------------------------------------------------------- */

static int xmac_mdio_wait(uintptr_t base)
{
	uint32_t t = XMAC_TIMEOUT;

	while ((xmac_rd(base, XMAC_NSR) & XMAC_NSR_IDLE) == 0u) {
		if (--t == 0u) {
			return -1;
		}
	}
	return 0;
}

static int xmac_phy_read(struct xmac *m, uint8_t addr, uint16_t reg,
			 uint16_t *val)
{
	uintptr_t base = m->base;

	if (xmac_mdio_wait(base) != 0) {
		return -1;
	}
	xmac_wr(base, XMAC_MAN,
		XMAC_MAN_SOF | XMAC_MAN_READ | XMAC_MAN_PHYA(addr) |
		XMAC_MAN_REGA(reg) | XMAC_MAN_CODE);
	if (xmac_mdio_wait(base) != 0) {
		return -1;
	}
	*val = (uint16_t)(xmac_rd(base, XMAC_MAN) & XMAC_MAN_DATA_MASK);
	return 0;
}

static int xmac_phy_write(struct xmac *m, uint8_t addr, uint16_t reg,
			  uint16_t val)
{
	uintptr_t base = m->base;

	if (xmac_mdio_wait(base) != 0) {
		return -1;
	}
	xmac_wr(base, XMAC_MAN,
		XMAC_MAN_SOF | XMAC_MAN_WRITE | XMAC_MAN_PHYA(addr) |
		XMAC_MAN_REGA(reg) | XMAC_MAN_CODE |
		(uint32_t)val);
	return xmac_mdio_wait(base);
}

/* --- MAC address ------------------------------------------------------------ */

static void xmac_addr_read(uintptr_t base, uint8_t mac[6])
{
	uint32_t lo = xmac_rd(base, XMAC_SA1B);
	uint32_t hi = xmac_rd(base, XMAC_SA1T);

	mac[0] = (uint8_t)(lo >> 0);
	mac[1] = (uint8_t)(lo >> 8);
	mac[2] = (uint8_t)(lo >> 16);
	mac[3] = (uint8_t)(lo >> 24);
	mac[4] = (uint8_t)(hi >> 0);
	mac[5] = (uint8_t)(hi >> 8);
}

static void xmac_addr_write(uintptr_t base, const uint8_t mac[6])
{
	uint32_t i;

	xmac_wr(base, XMAC_SA1B,
		((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
		((uint32_t)mac[1] << 8) | (uint32_t)mac[0]);
	xmac_wr(base, XMAC_SA1T,
		((uint32_t)mac[5] << 8) | (uint32_t)mac[4]);

	/* The spare station-address slots stay empty, or the port would answer
	 * for addresses nobody assigned it. */
	for (i = XMAC_SA2B; i <= XMAC_SA4T; i += 4u) {
		xmac_wr(base, i, 0u);
	}
}

static int xmac_addr_valid(const uint8_t mac[6])
{
	int all_zero = (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0;
	int all_ones = (mac[0] & mac[1] & mac[2] & mac[3] & mac[4] & mac[5]) == 0xff;
	int is_multicast = (mac[0] & 1u) != 0u;

	return !all_zero && !all_ones && !is_multicast;
}

/* --- SerDes clock selects and speed ----------------------------------------- */

static void xmac_serdes_clocks(struct xmac *m, unsigned int speed_mbps)
{
	uintptr_t base = m->base;

	xmac_wr(base, XMAC_SRC_SEL_LN, XMAC_LN_SRC_SEL);
	xmac_wr(base, XMAC_DIV_SEL0_LN, XMAC_LN_DIV0);
	xmac_wr(base, XMAC_DIV_SEL1_LN, XMAC_LN_DIV1);
	xmac_wr(base, XMAC_PMA_POWER_STATE, XMAC_LN_PMA_ON);
	xmac_wr(base, XMAC_RX_CLK_SEL0, 1u);
	xmac_wr(base, XMAC_RX_CLK_SEL1, 0u);

	if (speed_mbps >= 1000u) {
		xmac_wr(base, XMAC_TX_CLK_SEL0, 0u);
		xmac_wr(base, XMAC_TX_CLK_SEL1, 0u);
		xmac_wr(base, XMAC_TX_CLK_SEL2, 0u);
		xmac_wr(base, XMAC_TX_CLK_SEL3, 1u);
		xmac_wr(base, XMAC_TX_CLK_SEL3_0, 0u);
		xmac_wr(base, XMAC_TX_CLK_SEL4_0, 0u);
		xmac_wr(base, XMAC_RX_CLK_SEL3_0, 0u);
		xmac_wr(base, XMAC_RX_CLK_SEL4_0, 0u);
	} else {
		xmac_wr(base, XMAC_TX_CLK_SEL0, 0u);
		xmac_wr(base, XMAC_TX_CLK_SEL1, 0u);
		xmac_wr(base, XMAC_TX_CLK_SEL2, 1u);
		xmac_wr(base, XMAC_TX_CLK_SEL3, 1u);
		xmac_wr(base, XMAC_TX_CLK_SEL3_0, 1u);
		xmac_wr(base, XMAC_TX_CLK_SEL4_0, 0u);
		xmac_wr(base, XMAC_RX_CLK_SEL3_0, 0u);
		xmac_wr(base, XMAC_RX_CLK_SEL4_0, 1u);
	}

	/* The high-speed port macro must be told the line rate too, or the
	 * SerDes runs at its default rate while the MAC times the wire at
	 * another. */
	if (speed_mbps >= 1000u) {
		xmac_wr(base, XMAC_HS_MAC_CONFIG,
			(xmac_rd(base, XMAC_HS_MAC_CONFIG) & ~7u) |
			XMAC_HS_SPEED_1000M);
	} else {
		xmac_wr(base, XMAC_HS_MAC_CONFIG,
			(xmac_rd(base, XMAC_HS_MAC_CONFIG) & ~7u) |
			XMAC_HS_SPEED_100M);
	}
}

/* The MAC's own speed/duplex bits, kept in step with the SerDes. */
static void xmac_apply_link(struct xmac *m, const struct xmac_link *link)
{
	uintptr_t base = m->base;
	uint32_t cfg = xmac_rd(base, XMAC_NCFGR);

	cfg &= ~(XMAC_NCFGR_SPD | XMAC_NCFGR_FD | XMAC_NCFGR_GBE);

	if (link->speed_mbps >= 1000u) {
		cfg |= XMAC_NCFGR_GBE;
	} else if (link->speed_mbps == 100u) {
		cfg |= XMAC_NCFGR_SPD;
	}

	if (link->full_duplex) {
		cfg |= XMAC_NCFGR_FD;
	}
	xmac_wr(base, XMAC_NCFGR, cfg);

	/* The SerDes side follows in the same step: at 1 Gbit/s the line clock
	 * divides differently than at 100 Mbit/s. */
	if (link->up) {
		xmac_serdes_clocks(m, link->speed_mbps);
	}
}

/* --- PHY detect and readback ------------------------------------------------ */

static int xmac_phy_detect(struct xmac *m)
{
	uint16_t id1 = 0xffffu;
	uint16_t id2 = 0xffffu;
	uint16_t bmsr = 0xffffu;
	uint8_t addr;

	for (addr = 0u; addr < 32u; addr++) {
		if (xmac_phy_read(m, addr, MII_BMSR, &bmsr) != 0) {
			board_log("xmac: mdio idle timeout at addr %u "
				  "(ncr=%08x nsr=%08x man=%08x)\n",
				  addr, xmac_rd(m->base, XMAC_NCR),
				  xmac_rd(m->base, XMAC_NSR),
				  xmac_rd(m->base, XMAC_MAN));
			return -1;
		}
		if (xmac_phy_read(m, addr, MII_PHYSID1, &id1) != 0 ||
		    xmac_phy_read(m, addr, MII_PHYSID2, &id2) != 0) {
			return -1;
		}
		/* Presence is the IDENTIFIER PAIR, not the top half: this
		 * board's PHY (a Motorcomm part) reports PHYSID1 = 0x0000 and
		 * puts the whole model number in PHYSID2, so requiring a
		 * non-zero ID1 rejects a live PHY while every other read looks
		 * healthy. All-ones on both halves is the real "nobody there". */
		if (id2 != 0xffffu && !(id1 == 0xffffu && id2 == 0xffffu)) {
			m->phy_addr = addr;
			m->phy_id1 = id1;
			board_log("xmac: phy addr=%u id=%04x:%04x\n",
				  addr, id1, id2);
			return 0;
		}
	}
	{
		uint16_t r0 = 0, r1 = 0, r2 = 0, r3 = 0, r16 = 0;
		int wok;

		(void)xmac_phy_read(m, 0u, MII_BMCR, &r0);
		(void)xmac_phy_read(m, 0u, MII_BMSR, &r1);
		(void)xmac_phy_read(m, 0u, MII_PHYSID1, &r2);
		(void)xmac_phy_read(m, 0u, MII_PHYSID2, &r3);
		(void)xmac_phy_read(m, 0u, 16u, &r16);
		/* A write that reads back proves the frame is clocking at all;
		 * a write that vanishes proves it is not. */
		wok = xmac_phy_write(m, 0u, MII_BMCR, 0x8000u);
		(void)xmac_phy_read(m, 0u, MII_BMCR, &r0);
		board_log("xmac: mdio raw a0 bmcr=%04x bmsr=%04x id=%04x:%04x "
			  "r16=%04x wr_rc=%d wr_back=%04x man=%08x nsr=%08x "
			  "ncr=%08x ncfgr=%08x dcfg1=%08x\n",
			  r0, r1, r2, r3, r16, wok, r0,
			  xmac_rd(m->base, XMAC_MAN),
			  xmac_rd(m->base, XMAC_NSR),
			  xmac_rd(m->base, XMAC_NCR),
			  xmac_rd(m->base, XMAC_NCFGR),
			  xmac_rd(m->base, XMAC_DCFG1));
	}
	return -1;
}

/* BMSR's link bit latches low, so it is read twice before it is believed
 * (the clause-22 read protocol). */
static int xmac_phy_link(struct xmac *m, unsigned int *up)
{
	uint16_t bmsr = 0;

	if (xmac_phy_read(m, m->phy_addr, MII_BMSR, &bmsr) != 0 ||
	    xmac_phy_read(m, m->phy_addr, MII_BMSR, &bmsr) != 0) {
		return -1;
	}
	*up = (bmsr & MII_BMSR_LINK) != 0u;
	return 0;
}

static int xmac_phy_result(struct xmac *m, struct xmac_link *out)
{
	uint16_t stat1000 = 0;
	uint16_t lpa = 0;

	if (xmac_phy_read(m, m->phy_addr, MII_STAT1000, &stat1000) != 0 ||
	    xmac_phy_read(m, m->phy_addr, MII_LPA, &lpa) != 0) {
		return -1;
	}

	if ((stat1000 & (MII_STAT1000_LP_FULL | MII_STAT1000_LP_HALF)) != 0u) {
		out->speed_mbps = 1000u;
		out->full_duplex = (stat1000 & MII_STAT1000_LP_FULL) != 0u;
	} else if ((lpa & (MII_LPA_100 | 0x0180u)) != 0u) {
		out->speed_mbps = 100u;
		out->full_duplex = (lpa & MII_LPA_DUPLEX) != 0u;
	} else {
		out->speed_mbps = 10u;
		out->full_duplex = (lpa & MII_LPA_DUPLEX) != 0u;
	}
	out->up = 1u;
	return 0;
}

/* --- ring construction ------------------------------------------------------- */

/* Hand one receive buffer back to the DMA: the address with the used flag
 * clear (and the wrap flag on the last slot). */
static void xmac_desc_set_addr(struct xmac_desc *d, uint32_t buf, int wrap)
{
	d->addr = (buf & ~3u) | (wrap ? XMAC_RXD_WRAP : 0u);
}

static void xmac_rx_ring_build(uintptr_t base)
{
	uint32_t i;

	memset(xmac_rx_desc, 0, sizeof(xmac_rx_desc));
	for (i = 0u; i < BOARD_XMAC_RX_DESCS; i++) {
		xmac_desc_set_addr(&xmac_rx_desc[i],
				   (uint32_t)(uintptr_t)xmac_rx_buf[i],
				   i == BOARD_XMAC_RX_DESCS - 1u);
		xmac_rx_desc[i].ctrl = 0u;
	}
	xmac_rx_idx = 0u;
	xmac_handover();
	xmac_wr(base, XMAC_RBQP, (uint32_t)(uintptr_t)xmac_rx_desc);
}

static void xmac_tx_ring_build(uintptr_t base)
{
	uint32_t i;

	memset(xmac_tx_desc, 0, sizeof(xmac_tx_desc));
	for (i = 0u; i < BOARD_XMAC_TX_DESCS; i++) {
		xmac_tx_desc[i].addr = 0u;
		xmac_tx_desc[i].ctrl = XMAC_TXC_USED;
		if (i == BOARD_XMAC_TX_DESCS - 1u) {
			xmac_tx_desc[i].ctrl |= XMAC_TXC_WRAP;
		}
	}
	xmac_tx_idx = 0u;
	xmac_handover();
	xmac_wr(base, XMAC_TBQP, (uint32_t)(uintptr_t)xmac_tx_desc);
}

/* --- public API --------------------------------------------------------------- */

int xmac_init(struct xmac *m, uintptr_t base)
{
	uint32_t cfg;
	uint32_t dmacfg;
	uint32_t t;
	uint8_t mac[6];

	memset(m, 0, sizeof(*m));
	m->base = base;
	m->phy_addr = MII_PHY_ADDR_NONE;

	/* MAC address: back it up before anything resets it. The boot loader
	 * programmed a working one (it transferred this image over this port);
	 * an invalid reading falls back to a locally administered address. */
	xmac_addr_read(base, mac);
	if (xmac_addr_valid(mac)) {
		memcpy(m->mac, mac, sizeof(m->mac));
	} else {
		m->mac[0] = 0x02u;
		m->mac[1] = 0x11u;
		m->mac[2] = 0x22u;
		m->mac[3] = 0x33u;
		m->mac[4] = 0x44u;
		m->mac[5] = 0x02u;
	}

	/* Quiesce: every interrupt masked (the port is fully polled), both
	 * directions off, and the MDIO master enabled so the PHY can be
	 * reached below. */
	xmac_wr(base, XMAC_IDR, 0xffffffffu);
	xmac_wr(base, XMAC_ISR, 0xffffffffu);
	xmac_clr(base, XMAC_NCR, XMAC_NCR_RE | XMAC_NCR_TE);
	xmac_set(base, XMAC_NCR, XMAC_NCR_MPE | XMAC_NCR_CLRSTAT);

	/* Clear the stale transmit/receive status, or a "used bit read" left
	 * over from the previous owner halts the rings on the first frame. */
	xmac_wr(base, XMAC_TSR, 0xffffffffu);
	xmac_wr(base, XMAC_RSR, 0xffffffffu);

	xmac_addr_write(base, m->mac);

	/* Network config: SGMII on a PCS-selected line, FCS stripped by the
	 * MAC (software must not strip a second one), 1536-byte frames, the
	 * MDC divisor, and the bus width the design reports. Speed/duplex are
	 * set again from the PHY result. */
	dmacfg = xmac_rd(base, XMAC_DCFG1);
	cfg = xmac_rd(base, XMAC_NCFGR);
	cfg &= ~(XMAC_NCFGR_SPD | XMAC_NCFGR_NBC | XMAC_NCFGR_PAE |
		 XMAC_NCFGR_RXCOEN | XMAC_NCFGR_CLK_MASK |
		 XMAC_NCFGR_DBW_MASK);
	cfg |= XMAC_NCFGR_SGMIIEN | XMAC_NCFGR_PCSSEL | XMAC_NCFGR_DRFCS |
	       XMAC_NCFGR_BIG | XMAC_NCFGR_GBE | XMAC_NCFGR_FD |
	       XMAC_NCFGR_CLK(XMAC_MDC_DIV);
	switch ((dmacfg >> 25) & 7u) {
	case 4u:
		cfg |= XMAC_NCFGR_DBW(2u);
		break;
	case 2u:
		cfg |= XMAC_NCFGR_DBW(1u);
		break;
	default:
		cfg |= XMAC_NCFGR_DBW(0u);
		break;
	}
	xmac_wr(base, XMAC_NCFGR, cfg);

	/* The port's AXI pipeline depth, then the SerDes clock selects for the
	 * rate the boot loader's transfer already left the line at. */
	xmac_wr(base, XMAC_AXI_PIPE, XMAC_AXI_PIPE_VALUE);
	xmac_serdes_clocks(m, 1000u);

	/* DMA config: burst length 16, receive buffer size in 64-byte units,
	 * full packet-buffer memory, native endianness both sides, no
	 * checksum offload (the stack verifies in software) and 32-bit
	 * descriptors. */
	dmacfg &= ~(XMAC_DMACFG_RXBMS_MASK | XMAC_DMACFG_TXPBMS |
		    XMAC_DMACFG_ENDIA_DESC | XMAC_DMACFG_ENDIA_PKT |
		    XMAC_DMACFG_TXCOEN | XMAC_DMACFG_ADDR64 |
		    XMAC_DMACFG_RXBS_MASK);
	dmacfg |= XMAC_DMACFG_FBLDO(16u) | XMAC_DMACFG_RXBMS_MASK |
		  XMAC_DMACFG_TXPBMS |
		  XMAC_DMACFG_RXBS(BOARD_XMAC_BUF_BYTES / 64u);
	xmac_wr(base, XMAC_DMACFG, dmacfg);

	/* The MAC's own PCS runs the SGMII side's autonegotiation once PCSSEL
	 * has routed the line through it; without this bit the SerDes never
	 * exchanges the configuration word and the link stays down however
	 * healthy the copper side looks. */
	xmac_set(base, XMAC_PCS_CONTROL, XMAC_PCS_ANEG_ENABLE);

	/* Drain the statistics counters so the first phase's numbers are this
	 * boot's. */
	xmac_set(base, XMAC_NCR, XMAC_NCR_CLRSTAT);
	t = XMAC_TIMEOUT;
	while ((xmac_rd(base, XMAC_NCR) & XMAC_NCR_CLRSTAT) != 0u) {
		if (--t == 0u) {
			break;	/* not fatal: statistics only */
		}
	}

	if (xmac_phy_detect(m) != 0) {
		board_log("xmac: no clause-22 phy on the mdio bus\n");
		return -1;
	}

	board_log("xmac: controller reset, mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		  m->mac[0], m->mac[1], m->mac[2], m->mac[3], m->mac[4],
		  m->mac[5]);
	return 0;
}

int xmac_phy_step(struct xmac *m)
{
	unsigned int up = 0u;
	uint16_t bmcr = 0;

	if (m->phy_addr == MII_PHY_ADDR_NONE) {
		return -1;
	}
	if (xmac_phy_link(m, &up) != 0) {
		return -1;
	}

	if (up == 0u) {
		/* Link down: restart autonegotiation once, then keep reporting
		 * in-progress while the PHY settles. */
		if (m->an_restarted == 0) {
			if (xmac_phy_read(m, m->phy_addr, MII_BMCR, &bmcr) != 0) {
				return -1;
			}
			bmcr &= ~MII_BMCR_RESET;
			bmcr |= MII_BMCR_ANEG_EN | MII_BMCR_RESTART_ANEG;
			if (xmac_phy_write(m, m->phy_addr, MII_BMCR,
					   bmcr) != 0) {
				return -1;
			}
			m->an_restarted = 1;
			board_log("xmac: link down, autoneg restarted\n");
		}
		m->link.up = 0u;
		return 0;
	}

	if (xmac_phy_result(m, &m->link) != 0) {
		return -1;
	}
	m->link.up = 1u;
	xmac_apply_link(m, &m->link);
	return 1;
}

void xmac_start(struct xmac *m)
{
	uintptr_t base = m->base;

	xmac_rx_ring_build(base);
	xmac_tx_ring_build(base);
	xmac_wr(base, XMAC_ISR, 0xffffffffu);
	/* Raise the receive line as soon as a descriptor completes. The drain
	 * runs on a thread that then sleeps; without this mask the thread has to
	 * poll, and polling in this kernel means one wake per tick (or a
	 * spin that starves the stack - both were measured on the bench). */
	xmac_wr(base, XMAC_IER,
		XMAC_IXR_RCOMP | XMAC_IXR_RXUBR | XMAC_IXR_RLE |
			XMAC_IXR_ROVR);
	xmac_set(base, XMAC_NCR, XMAC_NCR_RE | XMAC_NCR_TE);
}

/* Interrupt entry: acknowledge the controller's status word and report whether
 * the receive line was among the sources. The stack side arms nothing itself -
 * the drain is signalled from here, so this stays a leaf that touches no RTOS
 * object and can run in exception context. */
int xmac_irq_ack(struct xmac *m)
{
	uintptr_t base = m->base;
	uint32_t status = xmac_rd(base, XMAC_ISR);

	if (status == 0u || status == 0xffffffffu) {
		return 0;
	}
	xmac_wr(base, XMAC_ISR, status);

	if ((status & (XMAC_IXR_RCOMP | XMAC_IXR_RLE | XMAC_IXR_ROVR |
		       XMAC_IXR_RXUBR)) == 0u) {
		return 0;
	}

	if ((status & (XMAC_IXR_RLE | XMAC_IXR_ROVR)) != 0u) {
		/* The receiver stalled and dropped: restart it from the ring
		 * head so the next frame has a buffer to land in. */
		m->stats.rx_dropped++;
		xmac_wr(base, XMAC_RSR, 0xffffffffu);
	}

	/* Mask the line until the ring is drained. RCOMP is level-triggered off
	 * "a descriptor completed", and it stays asserted for as long as any
	 * completed descriptor is still owned by software - so acknowledging it
	 * without draining re-enters this handler immediately and the drain
	 * never gets to run. The drain re-enables it at the end (xmac_rx_rearm),
	 * which is the same arm/mask pair the mainline driver uses around its
	 * NAPI schedule. */
	xmac_wr(base, XMAC_IDR, XMAC_IXR_RCOMP);
	return 1;
}

void xmac_rx_rearm(struct xmac *m)
{
	xmac_wr(m->base, XMAC_IER, XMAC_IXR_RCOMP);
}

void xmac_stop(struct xmac *m)
{
	uintptr_t base = m->base;

	xmac_clr(base, XMAC_NCR, XMAC_NCR_RE | XMAC_NCR_TE);
	xmac_wr(base, XMAC_IDR, 0xffffffffu);
}

int xmac_tx_frame(struct xmac *m, const void *frame, size_t len)
{
	struct xmac_desc *d;
	uint32_t ctrl;
	uint32_t idx = xmac_tx_idx;

	if (len == 0u || len > BOARD_XMAC_BUF_BYTES) {
		return -1;
	}

	d = &xmac_tx_desc[idx];
	if ((d->ctrl & XMAC_TXC_USED) == 0u) {
		/* The DMA has not finished this slot yet: walk the ring once and
		 * take the first slot it has released. */
		uint32_t walk = idx;
		uint32_t i;

		for (i = 0u; i < BOARD_XMAC_TX_DESCS; i++) {
			struct xmac_desc *w = &xmac_tx_desc[walk];

			if ((w->ctrl & XMAC_TXC_USED) != 0u) {
				idx = walk;
				d = w;
				break;
			}
			walk = (walk + 1u) % BOARD_XMAC_TX_DESCS;
		}
		if ((d->ctrl & XMAC_TXC_USED) == 0u) {
			m->stats.tx_busy++;
			return -1;
		}
	}

	memcpy(xmac_tx_buf[idx], frame, len);
	cache_dcache_flush((uintptr_t)xmac_tx_buf[idx],
			   (unsigned long)len);

	d->addr = (uint32_t)(uintptr_t)xmac_tx_buf[idx];
	xmac_handover();

	ctrl = (uint32_t)len & XMAC_TXC_LEN_MASK;
	ctrl |= XMAC_TXC_LAST;
	if (idx == BOARD_XMAC_TX_DESCS - 1u) {
		ctrl |= XMAC_TXC_WRAP;
	}
	d->ctrl = ctrl;
	xmac_tx_idx = (idx + 1u) % BOARD_XMAC_TX_DESCS;
	xmac_handover();

	/* Kick the transmitter: after the descriptors are handed over, STARTTX
	 * in the network control register is the only "go" this MAC takes -
	 * the legacy register set has no doorbell.
	 *
	 * Then acknowledge the transmit status. The transmitter stops when a
	 * status bit is raised (that is what makes transmit completion an
	 * interrupt), so the bits are read and written straight back to clear
	 * them - before the ring is reclaimed, not before every frame.
	 *
	 * What the ring pointer must never see is a write. The hardware walks
	 * the descriptor chain and wraps on the WRAP bit; the queue base is
	 * programmed once, when the port is brought up, and is not writable
	 * while the transmitter runs. Re-pointing it mid-stream
	 * desynchronises the DMA from the software cursor: the DMA walks into
	 * descriptors whose used bit is still set, raises the used-bit-read
	 * error for real, and the pointer is dragged back again on the next
	 * pass. That is what parked this leg under receive load - the ring
	 * pointer frozen at the base, ping at 100% loss, and the missing ACK
	 * stream then starving receive. */
	xmac_set(m->base, XMAC_NCR, XMAC_NCR_TSTART);

	ctrl = xmac_rd(m->base, XMAC_TSR);
	if (ctrl != 0u) {
		xmac_wr(m->base, XMAC_TSR, ctrl);
		xmac_handover();
		m->stats.tx_tsr = ctrl;
	}

	m->stats.tx_frames++;
	m->stats.tx_bytes += (uint32_t)len;
	return 0;
}

static void xmac_rx_release(uint32_t from, uint32_t to_excl)
{
	uint32_t i = from;

	for (;;) {
		xmac_desc_set_addr(&xmac_rx_desc[i],
				   (uint32_t)(uintptr_t)xmac_rx_buf[i],
				   i == BOARD_XMAC_RX_DESCS - 1u);
		xmac_rx_desc[i].ctrl = 0u;
		xmac_handover();
		if (i == to_excl) {
			break;
		}
		i = (i + 1u) % BOARD_XMAC_RX_DESCS;
	}
}

int xmac_rx_frame(struct xmac *m, uint8_t *buf, size_t buf_size)
{
	uint32_t i = xmac_rx_idx;
	uint32_t first = xmac_rx_idx;
	uint32_t walked = 0u;
	uint32_t ctrl;
	uint32_t len;
	int have_first = 0;

	while (walked < BOARD_XMAC_RX_DESCS) {
		struct xmac_desc *d = &xmac_rx_desc[i];

		if ((d->addr & XMAC_RXD_USED) == 0u) {
			break;		/* still the DMA's buffer */
		}
		walked++;

		/* The status word is only as fresh as the address word that
		 * announced it. */
		xmac_handover();
		ctrl = d->ctrl;

		if ((ctrl & XMAC_RXC_SOF) != 0u) {
			first = i;
			have_first = 1;
		}
		if ((ctrl & XMAC_RXC_EOF) != 0u) {
			if (!have_first) {
				/* Torn frame (its first segment was already
				 * recycled): release and resynchronise. */
				xmac_rx_release(xmac_rx_idx, i);
				xmac_rx_idx = (i + 1u) % BOARD_XMAC_RX_DESCS;
				m->stats.rx_dropped++;
				return 0;
			}

			len = ctrl & XMAC_RXC_LEN_MASK;
			if (len > buf_size) {
				m->stats.rx_dropped++;
			} else {
				size_t left = len;
				uint32_t seg = first;

				while (left > 0u) {
					size_t n = left > BOARD_XMAC_BUF_BYTES
						   ? BOARD_XMAC_BUF_BYTES
						   : left;

					cache_dcache_invalidate(
						(uintptr_t)xmac_rx_buf[seg],
						(unsigned long)n);
					memcpy(buf, xmac_rx_buf[seg], n);
					buf += n;
					left -= n;
					if (seg == i) {
						break;
					}
					seg = (seg + 1u) %
					      BOARD_XMAC_RX_DESCS;
				}
				m->stats.rx_frames++;
				m->stats.rx_bytes += len;
			}

			xmac_rx_release(first, i);
			xmac_rx_idx = (i + 1u) % BOARD_XMAC_RX_DESCS;

			/* Writing the receive status back clears "used bit
			 * read"; without it a single overrun parks the
			 * receiver for the rest of the boot. */
			xmac_wr(m->base, XMAC_RSR, 0xffffffffu);
			return (int)len;
		}
		i = (i + 1u) % BOARD_XMAC_RX_DESCS;
	}

	return 0;
}

void xmac_link_poll(struct xmac *m)
{
	unsigned int up = 0u;

	if (m->phy_addr == MII_PHY_ADDR_NONE) {
		return;
	}
	if (xmac_phy_link(m, &up) != 0) {
		return;
	}
	if (up != 0u && m->link.up == 0u) {
		(void)xmac_phy_result(m, &m->link);
		m->link.up = 1u;
		xmac_apply_link(m, &m->link);
	} else if (up == 0u && m->link.up != 0u) {
		m->link.up = 0u;
	}
}

void xmac_set_promisc(struct xmac *m, int on)
{
	if (on) {
		xmac_set(m->base, XMAC_NCFGR, XMAC_NCFGR_CAF);
	} else {
		xmac_clr(m->base, XMAC_NCFGR, XMAC_NCFGR_CAF);
	}
}

void xmac_get_stats(const struct xmac *m, struct xmac_stats *out)
{
	*out = m->stats;
}

uint32_t xmac_reg_read(const struct xmac *m, uint32_t off)
{
	return xmac_rd(m->base, off);
}
