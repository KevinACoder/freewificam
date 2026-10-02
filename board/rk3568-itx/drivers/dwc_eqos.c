/*
 * @file   dwc_eqos.c
 * @brief  DesignWare Ethernet QoS (dwmac-4.x) MAC core: one instance per
 *         GMAC port.
 *
 * One combined mac/dma interrupt per port, a single RX/TX channel, MDIO
 * through the dwmac4 register block, and 16-byte legacy descriptors in static
 * rings. The register sequences, the ring disciplines and the cache
 * maintenance in this file are the ones verified on this board by the emBox
 * port of the same IP - they are carried over as they were, not re-derived.
 *
 * THREE THINGS ABOUT THIS CORE THAT COST DAYS TO LEARN
 *
 * 1. The RX fetch window is [ring base, tail) - the DMA STOPS when its
 *    fetch pointer runs into the tail. (This port once carried the belief
 *    that the DMA wraps to base on reaching END and the tail is written
 *    once: board-measured wrong on 2026-10-02 - a fixed tail gave a hard
 *    127-frames-per-watchdog-poke reception ceiling, see eqos_rx_complete.
 *    The prior-work port ran light lwiperf traffic through a ring that was
 *    never lapped, so the stall never showed.) The tail must advance past
 *    every slot as it is re-armed - the completion sweep does that; the
 *    watchdog re-poke is only a heal.
 *
 * 2. The RX DMA stops the instant it laps a slot that is not owned: the
 *    completion sweep must re-arm every completed slot right away (in
 *    interrupt context) or reception throttles to the reader's cadence -
 *    measured on this board as a hard 1280 frames/s ceiling with 95%
 *    MAC-level FIFO-overflow loss when re-arming was moved into the RX
 *    thread (N7 R2b; the fix restored the ISR re-arm and deepened the
 *    staging ring 8 -> 128). A slot that was never granted stops the DMA
 *    the same way - the failure in (1) by another route.
 *
 * 3. Cache maintenance is explicit, per access: descriptors and buffers are
 *    reached by the DMA over a non-coherent port. A `dsb` orders a write, it
 *    does not write it back - every descriptor is flushed after software
 *    writes it and invalidated before software reads the hardware writeback,
 *    and every received buffer is invalidated before software reads it.
 *    Getting this wrong does not fault; it delivers stale frames.
 *
 * The other two facts worth not re-learning: the MAC must NOT strip the FCS
 * (software strips exactly one, and double stripping kills ICMP while ARP
 * survives), and short transmit frames must be zero-padded to 60 bytes or ARP
 * replies are silently discarded by the peer.
 *
 * Threading in the NO_SYS=1 image (specs/lwip-adapt §3.1 条 5): the only
 * contexts are the main loop and this port's interrupt. The old mutex +
 * IRQ-mask pair collapses to the IRQ mask alone - masking the port's own line
 * covers main-loop-vs-interrupt, and nothing else in the system touches these
 * rings. The osTimer watchdog became dwc_eqos_watchdog(), invoked from the
 * main loop; osDelay stays (the cmsis2 time subset implements it on both the
 * baremetal and threadx bases).
 *
 * @author zhugengyu
 * @date   16.09.2026 (board-verified core) / 02.10.2026 (port re-arrangement)
 */

#include <stdint.h>

#include "cache.h"
#include "cmsis_os2.h"
#include "gicv3.h"
#include "log.h"
#include "minilibc.h"

#include "board_conf.h"
#include "mmio.h"

#include "dwc_eqos.h"

/* Bus clock the MAC sees: pclk_gmac is 100 MHz after the CRU dividers. It sets
 * the 1us tick counter and the MDIO divider (see DWC_EQOS_MDIO_CR). */
#define DWC_EQOS_PCLK_HZ	100000000u

/* Programmable burst lengths, as in the verified configuration. */
#define DWC_EQOS_TXPBL		32u
#define DWC_EQOS_RXPBL		8u

/* How long SendFrame waits for a descriptor before reporting busy. At line
 * rate a full 64-slot ring drains in under a millisecond, so a couple of
 * milliseconds is generous; longer than that means the link is not moving. */
#define DWC_EQOS_TX_WAIT_MS	4u

/* Staged received frame (the ISR copies and re-arms immediately - the RX
 * DMA stops on any un-owned slot it laps, so the copy is what keeps the
 * DMA running at line rate; N7 R2b restored this after the no-copy R2
 * variant measured a 1280 frames/s DMA-stall ceiling and a 95% MAC-level
 * FIFO overflow loss under UDP flood). */
struct dwc_eqos_stage {
	uint16_t len;
	uint8_t  data[DWC_EQOS_RX_BUF_SIZE];
};

struct dwc_eqos_dev {
	const struct dwc_eqos_plat *plat;
	uintptr_t base_addr;
	uint8_t   macaddr[DWC_EQOS_MAC_ADDR_LEN];
	uint8_t   initialized;
	uint8_t   powered;
	uint8_t   started;
	/* Last CONFIGURE argument: applied when the port is powered, and
	 * re-applied when the link changes speed. */
	uint32_t  configure;

	struct dwc_eqos_desc *rx_ring;
	struct dwc_eqos_desc *tx_ring;
	int       tx_head;
	int       tx_tail;
	/* Software ownership per RX slot: 1 = handed to the DMA, 2 = the DMA
	 * wrote the frame back and the sweep has staged (copied) it but the
	 * reader has not consumed the copy yet, 0 = slot free. The sweep
	 * walks the whole ring because the DMA laps past software rather
	 * than stopping at a moving head. */
	uint8_t   rx_in_flight[DWC_EQOS_RX_DESC_COUNT];
	/* Next slot the reader consumes from; the DMA writes back in ring
	 * order, so staged slots always run consecutively from here. */
	unsigned int rx_read_tail;
	/* Frames already copied out of the ring by the sweep, waiting to be
	 * read (the burst buffer; see DWC_EQOS_RX_STAGE_COUNT). */
	struct dwc_eqos_stage *rx_stage;
	unsigned int rx_stage_count;
	unsigned int rx_stage_head;
	unsigned int rx_stage_tail;
	/* Arms the IOC bit on every DWC_EQOS_TX_IOC_DIV-th descriptor, so a
	 * burst costs one completion interrupt instead of one per frame. */
	unsigned int tx_ioc_seq;

	struct dwc_eqos_stats stats;

	/* Signalled by the completion sweep (freed > 0): a sender parked on a
	 * full TX ring waits on this instead of polling a 1 ms tick. */
	osSemaphoreId_t tx_sem;
};

/* Per-instance state and DMA memory. Static, not allocated: the DMA needs
 * naturally-aligned, physically contiguous buffers.
 *
 * The descriptor rings live in the .dma_noc section - the 2MiB block the
 * link script pins at 0x0c000000 and the MMU maps Normal non-cacheable
 * (baremetal.ld / mmu.c). The upstream drivers reach the same property with
 * dma_alloc_coherent; a 16-byte descriptor inside a 64-byte cache line
 * cannot be maintained by hand, because arming one descriptor flushes the
 * stale ARMED copy of its already-completed neighbours right over the DMA's
 * writeback, and the completion sweep then waits forever on an OWN bit
 * nothing will clear (the prior-work port ran only light lwiperf traffic and
 * never hit the window - the iperf3 round did, ~700 frames in).
 * Frame buffers stay in cacheable .bss: each is a whole multiple of cache
 * lines and has a single software writer, so its flush/invalidate discipline
 * is sound as-is. */
static struct dwc_eqos_dev devs[DWC_EQOS_PORT_COUNT];

#define DMA_NOC_SECTION \
	__attribute__((section(".dma_noc"), aligned(DWC_EQOS_BUF_ALIGN)))

static struct dwc_eqos_desc rx_rings[DWC_EQOS_PORT_COUNT][DWC_EQOS_RX_DESC_COUNT]
	DMA_NOC_SECTION;
static struct dwc_eqos_desc tx_rings[DWC_EQOS_PORT_COUNT][DWC_EQOS_TX_DESC_COUNT]
	DMA_NOC_SECTION;
static uint8_t rx_buffers[DWC_EQOS_PORT_COUNT][DWC_EQOS_RX_DESC_COUNT][DWC_EQOS_BUF_STRIDE]
	__attribute__((aligned(DWC_EQOS_BUF_ALIGN)));
static uint8_t tx_buffers[DWC_EQOS_PORT_COUNT][DWC_EQOS_TX_DESC_COUNT][DWC_EQOS_BUF_STRIDE]
	__attribute__((aligned(DWC_EQOS_BUF_ALIGN)));
static struct dwc_eqos_stage rx_stages[DWC_EQOS_PORT_COUNT][DWC_EQOS_RX_STAGE_COUNT]
	__attribute__((aligned(8)));

/* --- register and log plumbing -------------------------------------------- */

static inline uint32_t eqos_rd(const struct dwc_eqos_dev *dev, unsigned int reg)
{
	return mmio_rd32(dev->base_addr + reg);
}

/* Descriptor OWN-grant barrier: every store to the (non-cacheable) ring must
 * reach memory before the DMA is allowed to see the grant - before the end
 * pointer register write on TX, and before the DMA's ring scan runs past the
 * slot on RX. The per-descriptor cache flush this discipline used to rely on
 * carried a dsb; the non-cacheable ring states it explicitly. */
static inline void eqos_dsb_st(void)
{
	__asm__ __volatile__("dsb st" ::: "memory");
}

static inline void eqos_wr(const struct dwc_eqos_dev *dev, unsigned int reg,
			   uint32_t value)
{
	mmio_wr32(dev->base_addr + reg, value);
}

/* One line per event. The console is polled and slow: nothing here is allowed
 * to sit in a per-packet path. */
static const char *eqos_dev_name(const struct dwc_eqos_dev *dev)
{
	return (dev->plat->index == 0u) ? "gmac0" : "gmac1";
}

/* Serialize the main loop against this port's interrupt: masking the port's
 * own line is enough - the interrupt cannot nest on itself (it runs IRQ-
 * masked), and no other context touches these rings. */
static void eqos_lock(struct dwc_eqos_dev *dev)
{
	(void)irq_disable(dev->plat->irq_num);
}

static void eqos_unlock(struct dwc_eqos_dev *dev)
{
	(void)irq_enable(dev->plat->irq_num);
}

/* --- MDIO ----------------------------------------------------------------- */

/* Read a PHY register. The management interface is also how the PHY driver
 * reaches the chip, so this works as soon as the clocks are up - before the
 * DMA is initialized and before the PHY driver exists. */
int32_t dwc_eqos_phy_read(unsigned int index, uint8_t phy_addr, uint8_t reg_addr,
			  uint16_t *data)
{
	struct dwc_eqos_dev *dev;
	uint32_t mii_addr;
	int cnt;

	if ((index >= DWC_EQOS_PORT_COUNT) || (data == NULL)) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];

	mii_addr = ((uint32_t)phy_addr << DWC_EQOS_MDIO_PA_SHIFT)
		 | ((uint32_t)reg_addr << DWC_EQOS_MDIO_RDA_SHIFT)
		 | (DWC_EQOS_MDIO_CR << DWC_EQOS_MDIO_CR_SHIFT)
		 | DWC_EQOS_MDIO_GOC_READ
		 | DWC_EQOS_MDIO_GB;

	eqos_wr(dev, DWC_EQOS_MAC_MDIO_ADDRESS, mii_addr);
	for (cnt = 0; cnt < 100000; cnt++) {
		if (!(eqos_rd(dev, DWC_EQOS_MAC_MDIO_ADDRESS) & DWC_EQOS_MDIO_GB)) {
			*data = (uint16_t)(eqos_rd(dev, DWC_EQOS_MAC_MDIO_DATA) & 0xffffu);
			return EQOS_OK;
		}
	}

	return EQOS_ERROR_TIMEOUT;
}

int32_t dwc_eqos_phy_write(unsigned int index, uint8_t phy_addr, uint8_t reg_addr,
			   uint16_t data)
{
	struct dwc_eqos_dev *dev;
	uint32_t mii_addr;
	int cnt;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];

	/* Data first: the address register write starts the transaction. */
	eqos_wr(dev, DWC_EQOS_MAC_MDIO_DATA, data);
	mii_addr = ((uint32_t)phy_addr << DWC_EQOS_MDIO_PA_SHIFT)
		 | ((uint32_t)reg_addr << DWC_EQOS_MDIO_RDA_SHIFT)
		 | (DWC_EQOS_MDIO_CR << DWC_EQOS_MDIO_CR_SHIFT)
		 | DWC_EQOS_MDIO_GOC_WRITE
		 | DWC_EQOS_MDIO_GB;

	eqos_wr(dev, DWC_EQOS_MAC_MDIO_ADDRESS, mii_addr);
	for (cnt = 0; cnt < 100000; cnt++) {
		if (!(eqos_rd(dev, DWC_EQOS_MAC_MDIO_ADDRESS) & DWC_EQOS_MDIO_GB)) {
			return EQOS_OK;
		}
	}

	return EQOS_ERROR_TIMEOUT;
}

/* --- MAC configuration ---------------------------------------------------- */

static void eqos_set_macaddr_regs(const struct dwc_eqos_dev *dev,
				  const uint8_t *addr)
{
	eqos_wr(dev, DWC_EQOS_MAC_ADDRESS0_LOW,
		addr[0] | (addr[1] << 8) | (addr[2] << 16) | ((uint32_t)addr[3] << 24));
	eqos_wr(dev, DWC_EQOS_MAC_ADDRESS0_HIGH, addr[4] | (addr[5] << 8));
}

/* Speed and duplex into MAC_CONFIGURATION.
 *
 * BE|JD|JE|DCRS are set, and ACS/CST deliberately are NOT: the received frame
 * keeps its FCS (software strips exactly one) and the MAC's own CRC-strip
 * would make it two, which kills ICMP while leaving ARP working - a
 * combination that reads like a routing problem. Same configuration as the
 * netbsd/emBox ports on this board. */
static void eqos_apply_configure(struct dwc_eqos_dev *dev)
{
	uint32_t reg = eqos_rd(dev, DWC_EQOS_MAC_CONFIGURATION);
	uint32_t arg = dev->configure;
	uint32_t speed = arg & EQOS_SPEED_Msk;
	const char *str;

	reg |= DWC_EQOS_MAC_CONF_BE | DWC_EQOS_MAC_CONF_JD
	     | DWC_EQOS_MAC_CONF_JE | DWC_EQOS_MAC_CONF_DCRS;

	switch (speed) {
	case EQOS_SPEED_1G:
		reg &= ~(DWC_EQOS_MAC_CONF_PS | DWC_EQOS_MAC_CONF_FES);
		str = "1000";
		break;
	case EQOS_SPEED_100M:
		reg |= DWC_EQOS_MAC_CONF_PS | DWC_EQOS_MAC_CONF_FES;
		str = "100";
		break;
	case EQOS_SPEED_10M:
		reg |= DWC_EQOS_MAC_CONF_PS;
		reg &= ~DWC_EQOS_MAC_CONF_FES;
		str = "10";
		break;
	default:
		/* Nothing programmed yet: leave the speed bits alone and do
		 * not report a speed that was never set. */
		eqos_wr(dev, DWC_EQOS_MAC_CONFIGURATION, reg);
		return;
	}

	if ((arg & EQOS_DUPLEX_Msk) == EQOS_DUPLEX_HALF) {
		reg &= ~DWC_EQOS_MAC_CONF_DM;
	} else {
		reg |= DWC_EQOS_MAC_CONF_DM;
	}
	eqos_wr(dev, DWC_EQOS_MAC_CONFIGURATION, reg);

	board_log("%s: link %s Mbps %s duplex\n", eqos_dev_name(dev), str,
		  ((arg & EQOS_DUPLEX_Msk) == EQOS_DUPLEX_HALF) ? "half" : "full");
}

/* --- hardware bring-up ---------------------------------------------------- */

static int eqos_hw_stop(struct dwc_eqos_dev *dev)
{
	uint32_t reg;

	eqos_wr(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE, 0);

	reg = eqos_rd(dev, DWC_EQOS_DMA_CH0_TX_CONTROL);
	reg &= ~DWC_EQOS_TX_CTRL_ST;
	eqos_wr(dev, DWC_EQOS_DMA_CH0_TX_CONTROL, reg);

	reg = eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_CONTROL);
	reg &= ~DWC_EQOS_RX_CTRL_SR;
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_CONTROL, reg);

	reg = eqos_rd(dev, DWC_EQOS_MAC_CONFIGURATION);
	reg &= ~(DWC_EQOS_MAC_CONF_TE | DWC_EQOS_MAC_CONF_RE);
	eqos_wr(dev, DWC_EQOS_MAC_CONFIGURATION, reg);

	dev->started = 0;

	return EQOS_OK;
}

static int eqos_hw_reset(struct dwc_eqos_dev *dev)
{
	const struct dwc_eqos_plat *plat = dev->plat;
	int cnt;
	int cleared = 0;

	if (plat->soc_swr_quirk != NULL) {
		plat->soc_swr_quirk(plat, 1);
	}

	eqos_wr(dev, DWC_EQOS_DMA_MODE, DWC_EQOS_DMA_MODE_SWR);
	/* The reset normally clears within a few DMA clock cycles: spin on the
	 * register first, then fall back to sleeping so a wedged clock does not
	 * lock the CPU. */
	for (cnt = 0; (cnt < 1000000) && !cleared; cnt++) {
		cleared = !(eqos_rd(dev, DWC_EQOS_DMA_MODE) & DWC_EQOS_DMA_MODE_SWR);
	}
	for (cnt = 0; (cnt < 500) && !cleared; cnt++) {
		osDelay(10);
		cleared = !(eqos_rd(dev, DWC_EQOS_DMA_MODE) & DWC_EQOS_DMA_MODE_SWR);
	}

	if (plat->soc_swr_quirk != NULL) {
		plat->soc_swr_quirk(plat, 0);
	}

	if (!cleared) {
		board_log("%s: dma soft reset timeout, mode=0x%08x\n",
			  eqos_dev_name(dev),
			  (unsigned int)eqos_rd(dev, DWC_EQOS_DMA_MODE));
		return EQOS_ERROR;
	}

	return EQOS_OK;
}

static void eqos_setup_rx_desc(struct dwc_eqos_dev *dev, int idx)
{
	struct dwc_eqos_desc *desc = &dev->rx_ring[idx];
	uint8_t *buf = rx_buffers[dev->plat->index][idx];

	/* The buffer was written by software (.bss init) or by the previous
	 * DMA transfer: no stale dirty line may survive before the DMA starts
	 * writing into it again. */
	cache_dcache_flush((uintptr_t)buf, DWC_EQOS_RX_BUF_SIZE);
	cache_dcache_invalidate((uintptr_t)buf, DWC_EQOS_RX_BUF_SIZE);

	desc->des0 = (uint32_t)(uintptr_t)buf;
	desc->des1 = 0;
	desc->des2 = 0;
	desc->des3 = DWC_EQOS_DESC_OWN | DWC_EQOS_DESC_RX_IOC | DWC_EQOS_DESC_RX_BUF1V;
	/* No flush: the ring is in non-cacheable memory. The barrier keeps the
	 * grant visible to the DMA only after the buffer address and length. */
	eqos_dsb_st();
}

/* Start the channel. TX and RX are one DMA channel and one interrupt on this
 * core, so they are started and stopped as a unit (see dwc_eqos_control). */
static int eqos_hw_start(struct dwc_eqos_dev *dev)
{
	uint32_t reg;

	/* Channel: PBLx8 and contiguous 16-byte descriptors (no skip). */
	reg = eqos_rd(dev, DWC_EQOS_DMA_CH0_CONTROL);
	reg |= DWC_EQOS_CH0_CTRL_PBLX8;
	eqos_wr(dev, DWC_EQOS_DMA_CH0_CONTROL, reg);

	eqos_wr(dev, DWC_EQOS_DMA_CH0_TX_CONTROL,
		(DWC_EQOS_TXPBL << DWC_EQOS_TX_CTRL_TXPBL_SHIFT) | DWC_EQOS_TX_CTRL_OSP);
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_CONTROL,
		(DWC_EQOS_RXPBL << DWC_EQOS_RX_CTRL_RXPBL_SHIFT)
		| (DWC_EQOS_RX_BUF_SIZE << DWC_EQOS_RX_CTRL_RBSZ_SHIFT));

	/* RX end-pointer, written once and left alone. The fetch window is
	 * [base, END): the DMA wraps to the ring base at END and never fetches
	 * the END descriptor itself. Moving the boundary with every refill is
	 * what wedges the ring when the DMA catches up with software. */
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR,
		(uint32_t)(uintptr_t)&dev->rx_ring[DWC_EQOS_RX_DESC_COUNT - 1]);

	eqos_wr(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE, DWC_EQOS_CH0_INTR_DEFAULT);

	eqos_wr(dev, DWC_EQOS_DMA_CH0_TX_CONTROL,
		eqos_rd(dev, DWC_EQOS_DMA_CH0_TX_CONTROL) | DWC_EQOS_TX_CTRL_ST);
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_CONTROL,
		eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_CONTROL) | DWC_EQOS_RX_CTRL_SR);

	reg = eqos_rd(dev, DWC_EQOS_MAC_CONFIGURATION);
	reg |= DWC_EQOS_MAC_CONF_TE | DWC_EQOS_MAC_CONF_RE;
	eqos_wr(dev, DWC_EQOS_MAC_CONFIGURATION, reg);

	dev->started = 1;

	return EQOS_OK;
}

static int eqos_hw_init(struct dwc_eqos_dev *dev)
{
	uint32_t reg;
	uint32_t hw_feat;
	int i;

	/* Stop the engines and mask the channel before the soft reset: the
	 * firmware may have left them running. */
	eqos_hw_stop(dev);

	if (eqos_hw_reset(dev) != EQOS_OK) {
		return EQOS_ERROR;
	}

	/* MAC-level interrupts off, latched MAC events cleared.
	 *
	 * This block is separate from the DMA channel's INTR_ENABLE (0x1134)
	 * and covers MAC events - a link status change is the usual one. A
	 * latched bit here with its enable still set from the firmware holds
	 * the interrupt line asserted no matter how thoroughly the DMA status
	 * is cleared, and since the line is level-triggered the handler is
	 * simply re-entered: nothing at a lower interrupt priority, the tick
	 * included, runs again. This driver polls the link over MDIO instead,
	 * so it wants none of these. */
	board_log("%s: mac intr status=0x%08x enable=0x%08x\n", eqos_dev_name(dev),
		  (unsigned int)eqos_rd(dev, DWC_EQOS_MAC_INTERRUPT_STATUS),
		  (unsigned int)eqos_rd(dev, DWC_EQOS_MAC_INTERRUPT_ENABLE));
	eqos_wr(dev, DWC_EQOS_MAC_INTERRUPT_STATUS,
		eqos_rd(dev, DWC_EQOS_MAC_INTERRUPT_STATUS));
	eqos_wr(dev, DWC_EQOS_MAC_INTERRUPT_ENABLE, 0);

	/* AXI bus: outstanding request limits and 4/8/16-beat bursts. */
	eqos_wr(dev, DWC_EQOS_DMA_SYSBUS_MODE,
		(4u << DWC_EQOS_SYSBUS_WR_OSR_SHIFT)
		| (8u << DWC_EQOS_SYSBUS_RD_OSR_SHIFT)
		| DWC_EQOS_SYSBUS_EAME
		| DWC_EQOS_SYSBUS_BLEN16 | DWC_EQOS_SYSBUS_BLEN8
		| DWC_EQOS_SYSBUS_BLEN4);

	/* MTL queue sizes from the hardware feature register: the FIFO size is
	 * encoded as log2(n/128), and TQS/RQS hold fifo/256 - 1. */
	hw_feat = eqos_rd(dev, DWC_EQOS_MAC_HW_FEATURE1);
	reg = DWC_EQOS_MTL_TXQ0_OP_TXQEN_EN | DWC_EQOS_MTL_TXQ0_OP_TSF;
	reg |= ((((128u << ((hw_feat & DWC_EQOS_HW_TXFIFOSZ_MASK)
			    >> DWC_EQOS_HW_TXFIFOSZ_SHIFT)) >> 8) - 1u)
		<< DWC_EQOS_MTL_TXQ0_OP_TQS_SHIFT)
		& DWC_EQOS_MTL_TXQ0_OP_TQS_MASK;
	eqos_wr(dev, DWC_EQOS_MTL_TXQ0_OPERATION_MODE, reg);

	reg = DWC_EQOS_MTL_RXQ0_OP_RSF | DWC_EQOS_MTL_RXQ0_OP_FEP
	    | DWC_EQOS_MTL_RXQ0_OP_FUP;
	reg |= ((((128u << (hw_feat & DWC_EQOS_HW_RXFIFOSZ_MASK)) >> 8) - 1u)
		<< DWC_EQOS_MTL_RXQ0_OP_RQS_SHIFT)
		& DWC_EQOS_MTL_RXQ0_OP_RQS_MASK;
	eqos_wr(dev, DWC_EQOS_MTL_RXQ0_OPERATION_MODE, reg);

	/* All traffic to RX queue 0, multicast/broadcast to queue 0. */
	eqos_wr(dev, DWC_EQOS_MAC_RXQ_CTRL0, DWC_EQOS_RXQ_CTRL0_RXQ0EN_DCB);
	reg = eqos_rd(dev, DWC_EQOS_MAC_RXQ_CTRL1);
	reg |= DWC_EQOS_RXQ_CTRL1_MCBCQEN;
	eqos_wr(dev, DWC_EQOS_MAC_RXQ_CTRL1, reg);

	/* Flow control off; promiscuous reception; ~1us tick for the watchdog
	 * timers. */
	eqos_wr(dev, DWC_EQOS_MAC_Q0_TX_FLOW_CTRL, 0);
	eqos_wr(dev, DWC_EQOS_MAC_RX_FLOW_CTRL, 0);
	eqos_wr(dev, DWC_EQOS_MAC_PACKET_FILTER, DWC_EQOS_PKT_FLT_PR);
	eqos_wr(dev, DWC_EQOS_MAC_1US_TIC_COUNTER, (DWC_EQOS_PCLK_HZ / 1000000u) - 1u);

	eqos_set_macaddr_regs(dev, dev->macaddr);

	/* Descriptor rings: static arrays in the non-cacheable DMA window, so
	 * the memset alone is what the DMA sees. */
	memset(dev->rx_ring, 0, DWC_EQOS_RX_DESC_COUNT * sizeof(struct dwc_eqos_desc));
	memset(dev->tx_ring, 0, DWC_EQOS_TX_DESC_COUNT * sizeof(struct dwc_eqos_desc));

	memset(dev->rx_in_flight, 1, DWC_EQOS_RX_DESC_COUNT);
	dev->tx_head = 0;
	dev->tx_tail = 0;
	dev->rx_read_tail = 0;
	dev->tx_ioc_seq = 0;
	dev->rx_stage = rx_stages[dev->plat->index];
	dev->rx_stage_count = DWC_EQOS_RX_STAGE_COUNT;
	dev->rx_stage_head = 0;
	dev->rx_stage_tail = 0;

	for (i = 0; i < DWC_EQOS_RX_DESC_COUNT; i++) {
		eqos_setup_rx_desc(dev, i);
	}

	eqos_wr(dev, DWC_EQOS_DMA_CH0_TX_BASE_ADDR_HI, 0);
	eqos_wr(dev, DWC_EQOS_DMA_CH0_TX_BASE_ADDR_LO,
		(uint32_t)(uintptr_t)dev->tx_ring);
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_BASE_ADDR_HI, 0);
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_BASE_ADDR_LO,
		(uint32_t)(uintptr_t)dev->rx_ring);
	eqos_wr(dev, DWC_EQOS_DMA_CH0_TX_RING_LEN, DWC_EQOS_TX_DESC_COUNT - 1);
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_RING_LEN, DWC_EQOS_RX_DESC_COUNT - 1);

	return EQOS_OK;
}

/* --- completion paths ----------------------------------------------------- */

static int eqos_stage_put(struct dwc_eqos_dev *dev, const uint8_t *buf, unsigned int len)
{
	unsigned int next = (dev->rx_stage_head + 1) % dev->rx_stage_count;
	struct dwc_eqos_stage *stage;

	if (next == dev->rx_stage_tail) {
		/* Full: drop this frame rather than delay the ring. */
		dev->stats.rx_dropped++;
		return -1;
	}

	stage = &dev->rx_stage[dev->rx_stage_head];
	stage->len = (uint16_t)len;
	memcpy(stage->data, buf, len);
	dev->rx_stage_head = next;
	dev->stats.rx_frames++;

	return 0;
}

/* Copy out and re-arm every frame the DMA has written back. Returns the
 * number of frames newly staged.
 *
 * The re-arm must happen HERE, in the caller's (interrupt/watchdog)
 * context, not in the reader: the RX DMA stops the instant it laps a slot
 * that is not owned, and a reader-paced re-arm therefore throttles the DMA
 * to the reader's scheduling cadence - measured on this board as a hard
 * 1280 frames/s ceiling with a 95% MAC-level FIFO-overflow loss (MMC
 * 0x7d4) under a 300 Mbit/s UDP flood, while the MAC itself received every
 * frame (N7 R2b evidence). One linear copy per frame in the interrupt is
 * the price of keeping the DMA continuous.
 *
 * The sweep walks the whole ring: once the DMA's fetch pointer reaches END
 * it wraps to the ring base, so writebacks land wherever the next frame
 * went, not at a moving head. Callable from the interrupt and the
 * watchdog sweep. */
static int eqos_rx_complete(struct dwc_eqos_dev *dev)
{
	int delivered = 0;
	int last_armed = -1;
	int i;

	for (i = 0; i < DWC_EQOS_RX_DESC_COUNT; i++) {
		struct dwc_eqos_desc *desc;
		uint8_t *buf;
		unsigned int len;

		if (dev->rx_in_flight[i] != 1) {
			continue;
		}
		desc = &dev->rx_ring[i];
		/* No invalidate: non-cacheable ring, the read hits memory. */
		if (desc->des3 & DWC_EQOS_DESC_OWN) {
			continue;
		}

		dev->rx_in_flight[i] = 2;
		buf = rx_buffers[dev->plat->index][i];
		len = desc->des3 & DWC_EQOS_DESC_RX_LEN_MASK;

		if (len < 4) {
			dev->stats.rx_errors++;
		} else {
			cache_dcache_invalidate((uintptr_t)buf, len);
			/* Strip the frame check sequence: the MAC was
			 * configured not to, and one strip is the right
			 * number. */
			len -= 4;
			if (eqos_stage_put(dev, buf, len) == 0) {
				delivered++;
			}
		}

		eqos_setup_rx_desc(dev, i);
		dev->rx_in_flight[i] = 1;
		last_armed = i;
	}

	/* Advance the RX tail: the DMA's fetch window is [base, tail) and it
	 * STOPS when it runs into the tail - a tail fixed at ring[LAST] gave
	 * a hard 127-frames-per-watchdog-poke ceiling (1270 frames/s at the
	 * 100 ms watchdog cadence, 95% of a UDP flood dropped to FIFO
	 * overflow, N7 R2b board-measured). The Linux driver's RX_END is
	 * moving for the same reason. Writing the last slot we re-armed
	 * extends the window over it; the next sweep extends it further. */
	if (last_armed >= 0) {
		eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR,
			(uint32_t)(uintptr_t)&dev->rx_ring[last_armed]);
	}

	return delivered;
}

/* Release descriptors the DMA has finished with. Callable from the interrupt
 * and from the watchdog sweep. */
static int eqos_tx_complete(struct dwc_eqos_dev *dev)
{
	int freed = 0;
	int cnt;

	for (cnt = 0; cnt < DWC_EQOS_TX_DESC_COUNT; cnt++) {
		struct dwc_eqos_desc *desc;

		if (dev->tx_tail == dev->tx_head) {
			break;
		}
		desc = &dev->tx_ring[dev->tx_tail];
		/* No invalidate: non-cacheable ring, the read hits memory. */
		if (desc->des3 & DWC_EQOS_DESC_OWN) {
			break;
		}
		if (desc->des3 & DWC_EQOS_DESC_TX_ERR) {
			dev->stats.tx_errors++;
		}
		dev->tx_tail = (dev->tx_tail + 1) % DWC_EQOS_TX_DESC_COUNT;
		freed++;
	}

	if (dev->tx_tail != dev->tx_head) {
		/* The DMA stops when it runs out of owned descriptors; point
		 * the end past the newest one so it resumes. */
		eqos_wr(dev, DWC_EQOS_DMA_CH0_TX_END_ADDR,
			(uint32_t)(uintptr_t)&dev->tx_ring[dev->tx_head]);
	}

	/* N7 R1: wake a ring-full sender immediately instead of letting it
	 * poll a 1 ms tick. One release per freed slot (the semaphore is
	 * counting); osSemaphoreRelease is ISR-safe, and this runs in the
	 * interrupt on the hot path. */
	if ((freed > 0) && (dev->tx_sem != NULL)) {
		(void)osSemaphoreRelease(dev->tx_sem);
	}

	return freed;
}

/* Belt and braces, called from the main loop at the old osTimer cadence.
 * Receiving is interrupt-driven and the sweep heals itself on the next
 * interrupt, but a lost interrupt with no further traffic would leave frames
 * staged-but-unpolled, and a transmit ring waiting on a completion that never
 * came would stay full. */
/* Optional RX-notify hook: one global (the single-port image has at most
 * one live port; the notify target is the adapter's RX wake, not per-port
 * state). */
static void (*s_rx_notify)(unsigned int index);

void dwc_eqos_set_rx_notify(void (*fn)(unsigned int index))
{
	s_rx_notify = fn;
}

void dwc_eqos_watchdog(unsigned int index)
{
	struct dwc_eqos_dev *dev;
	int rx_frames;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return;
	}
	dev = &devs[index];
	if (!dev->started) {
		return;
	}

	eqos_lock(dev);
	rx_frames = eqos_rx_complete(dev);
	(void)eqos_tx_complete(dev);
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR,
		(uint32_t)(uintptr_t)&dev->rx_ring[DWC_EQOS_RX_DESC_COUNT - 1]);
	eqos_unlock(dev);

	/* N7 R1 fix: the ISR only notifies when IT moved frames; frames the
	 * watchdog itself staged went unannounced, so the RX thread slept on
	 * holding staged traffic until some future interrupt (the millisecond
	 * sawtooth the first iperf rounds showed). Same contract as dwc_eqos_irq. */
	if ((rx_frames > 0) && (s_rx_notify != NULL)) {
		s_rx_notify(index);
	}
}

/* Combined MAC/DMA interrupt. Registered through gicv3 with the instance
 * index as the handler argument. The NO_SYS image has no event callback: the
 * frames land in the staging ring and the main loop finds them there. */
void dwc_eqos_irq(void *arg)
{
	struct dwc_eqos_dev *dev = &devs[(unsigned int)(uintptr_t)arg];
	int pass;

	for (pass = 0; pass < 8; pass++) {
		uint32_t status = eqos_rd(dev, DWC_EQOS_DMA_CH0_STATUS);

		if (status == 0) {
			break;
		}
		/* Write-one-to-clear everything latched: leaving e.g. the
		 * abnormal summary set keeps the line asserted forever. */
		eqos_wr(dev, DWC_EQOS_DMA_CH0_STATUS, status);

		if (!(status & (DWC_EQOS_CH0_STATUS_RI | DWC_EQOS_CH0_STATUS_TI
				| DWC_EQOS_CH0_STATUS_FB))) {
			break;
		}

		if (status & DWC_EQOS_CH0_STATUS_FB) {
			/* Fatal bus error: shut the channel down. No sleeping
			 * is allowed here, so no full re-initialization. */
			dev->stats.dma_errors++;
			board_log("%s: dma fatal bus error, channel stopped\n",
				  eqos_dev_name(dev));
			eqos_hw_stop(dev);
			return;
		}

		if (status & DWC_EQOS_CH0_STATUS_RI) {
			if (eqos_rx_complete(dev) > 0) {
				if (s_rx_notify != NULL) {
					s_rx_notify((unsigned int)(uintptr_t)arg);
				}
			}
		}
		if (status & DWC_EQOS_CH0_STATUS_TI) {
			(void)eqos_tx_complete(dev);
		}
	}
}

/* --- driver entry points --------------------------------------------------- */

int32_t dwc_eqos_initialize(unsigned int index, const struct dwc_eqos_plat *plat)
{
	struct dwc_eqos_dev *dev;

	if ((index >= DWC_EQOS_PORT_COUNT) || (plat == NULL)) {
		return EQOS_ERROR_PARAMETER;
	}
	if (plat->soc_init == NULL) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];
	if (dev->initialized) {
		return EQOS_ERROR;
	}

	memset(dev, 0, sizeof(*dev));
	dev->plat = plat;
	dev->base_addr = plat->base_addr;
	dev->configure = 0;
	memcpy(dev->macaddr, plat->mac_addr, DWC_EQOS_MAC_ADDR_LEN);
	dev->rx_ring = rx_rings[index];
	dev->tx_ring = tx_rings[index];

	/* TX ring-full wait (N7 R1): created here because initialize runs in
	 * task context (the app bring-up thread); a NULL semaphore downgrades
	 * the ring-full wait to a yield, so bring-up order is forgiving. */
	dev->tx_sem = osSemaphoreNew(0xFFFFU, 0U, NULL);

	dev->initialized = 1;

	return EQOS_OK;
}

int32_t dwc_eqos_uninitialize(unsigned int index)
{
	struct dwc_eqos_dev *dev;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];
	if (!dev->initialized) {
		return EQOS_ERROR;
	}

	(void)dwc_eqos_power_control(index, EQOS_POWER_OFF);
	(void)irq_disable(dev->plat->irq_num);
	dev->initialized = 0;

	return EQOS_OK;
}

int32_t dwc_eqos_power_control(unsigned int index, eqos_power_state_t state)
{
	struct dwc_eqos_dev *dev;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];
	if (!dev->initialized) {
		return EQOS_ERROR;
	}

	switch (state) {
	case EQOS_POWER_FULL:
		if (dev->powered) {
			return EQOS_OK;
		}

		/* Before the clocks are up the register block may read
		 * anything (the firmware usually leaves the GMAC clocks
		 * running, which is why this read is logged twice). */
		board_log("%s: mac version 0x%08x before soc init\n",
			  eqos_dev_name(dev),
			  (unsigned int)eqos_rd(dev, DWC_EQOS_MAC_VERSION));

		/* Clocks, iomux, RGMII delays, then the PHY hard reset pulse
		 * (which re-latches the PHY's straps - the PHY driver clears
		 * the strapped delay right after this returns). */
		if (dev->plat->soc_init(dev->plat) != 0) {
			return EQOS_ERROR;
		}

		board_log("%s: mac version 0x%08x at 0x%08x irq %u\n",
			  eqos_dev_name(dev),
			  (unsigned int)eqos_rd(dev, DWC_EQOS_MAC_VERSION),
			  (unsigned int)dev->plat->base_addr,
			  (unsigned int)dev->plat->irq_num);

		if (eqos_hw_init(dev) != EQOS_OK) {
			return EQOS_ERROR;
		}

		/* The gicv3 registration carries the instance index in the
		 * handler argument; level mode and the board's default SPI
		 * priority class, then enable (the same arming shape as every
		 * other SPI line in this image). */
		(void)irq_register(dev->plat->irq_num, dwc_eqos_irq,
				   (void *)(uintptr_t)index);
		(void)irq_set_mode_level(dev->plat->irq_num);
		(void)irq_set_priority(dev->plat->irq_num,
				       BOARD_SPI_DEFAULT_PRIORITY_RAW);
		(void)irq_enable(dev->plat->irq_num);

		dev->powered = 1;
		return EQOS_OK;

	case EQOS_POWER_OFF:
		if (!dev->powered) {
			return EQOS_OK;
		}
		eqos_lock(dev);
		(void)eqos_hw_stop(dev);
		eqos_unlock(dev);
		(void)irq_disable(dev->plat->irq_num);
		dev->powered = 0;
		return EQOS_OK;

	default:
		/* Low-power and full-power-retention modes are not
		 * implemented; saying so beats accepting them silently. */
		return EQOS_ERROR_UNSUPPORTED;
	}
}

int32_t dwc_eqos_get_mac_address(unsigned int index, uint8_t *addr)
{
	if ((index >= DWC_EQOS_PORT_COUNT) || (addr == NULL)) {
		return EQOS_ERROR_PARAMETER;
	}
	memcpy(addr, devs[index].macaddr, DWC_EQOS_MAC_ADDR_LEN);

	return EQOS_OK;
}

int32_t dwc_eqos_set_mac_address(unsigned int index, const uint8_t *addr)
{
	struct dwc_eqos_dev *dev;

	if ((index >= DWC_EQOS_PORT_COUNT) || (addr == NULL)) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];
	eqos_lock(dev);
	memcpy(dev->macaddr, addr, DWC_EQOS_MAC_ADDR_LEN);
	eqos_set_macaddr_regs(dev, dev->macaddr);
	eqos_unlock(dev);

	return EQOS_OK;
}

int32_t dwc_eqos_send_frame(unsigned int index, const uint8_t *frame, uint32_t len)
{
	struct dwc_eqos_dev *dev;
	unsigned int tries;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];
	if ((frame == NULL) || (len == 0) || (len > DWC_EQOS_RX_BUF_SIZE)) {
		return EQOS_ERROR_PARAMETER;
	}
	if (!dev->powered) {
		return EQOS_ERROR;
	}

	for (tries = 0; tries <= DWC_EQOS_TX_WAIT_MS; tries++) {
		struct dwc_eqos_desc *desc;
		unsigned int tx_len;
		unsigned int ioc;
		uint8_t *buf;
		int cur;
		int next;

		eqos_lock(dev);
		cur = dev->tx_head;
		next = (cur + 1) % DWC_EQOS_TX_DESC_COUNT;
		if (next != dev->tx_tail) {
			buf = tx_buffers[index][cur];
			memcpy(buf, frame, len);

			/* Ethernet requires 60 bytes before the FCS: a
			 * shorter frame (a 42-byte ARP reply, for one) is
			 * discarded as a runt by every receiver, silently. */
			tx_len = len;
			if (tx_len < DWC_EQOS_MIN_TX_FRAME) {
				memset(buf + len, 0, DWC_EQOS_MIN_TX_FRAME - len);
				tx_len = DWC_EQOS_MIN_TX_FRAME;
			}
			cache_dcache_flush((uintptr_t)buf, tx_len);

			/* One completion interrupt per IOC_DIV frames, plus
			 * the frame that empties the ring (cur == tx_tail
			 * means this is the only in-flight frame): a quiet
			 * line must never wait on the counter for its
			 * completion, a burst must not pay one IRQ per
			 * frame (N7 R2). */
			ioc = ((dev->tx_ioc_seq++ % DWC_EQOS_TX_IOC_DIV)
			       == (DWC_EQOS_TX_IOC_DIV - 1u)) ||
			      (cur == dev->tx_tail);

			desc = &dev->tx_ring[cur];
			desc->des0 = (uint32_t)(uintptr_t)buf;
			desc->des1 = 0;
			/* On this core the transmit descriptor carries the frame
			 * length in des3 (as the rx writeback does) and the
			 * completion-interrupt enable in des2 bit 31. No flush:
			 * non-cacheable ring, the OWN grant is visible to the
			 * DMA before the end pointer below moves. */
			desc->des2 = (ioc ? DWC_EQOS_DESC_TX_IOC : 0u)
				   | (tx_len & 0x3fffu);
			desc->des3 = DWC_EQOS_DESC_OWN | DWC_EQOS_DESC_TX_FD
				   | DWC_EQOS_DESC_TX_LD | tx_len;
			/* Grant reaches memory before the end pointer moves. */
			eqos_dsb_st();

			dev->tx_head = next;

			/* Kick the DMA: the fetch window is [base, END), so
			 * the end must point past the descriptor just armed. */
			eqos_wr(dev, DWC_EQOS_DMA_CH0_TX_END_ADDR,
				(uint32_t)(uintptr_t)&dev->tx_ring[dev->tx_head]);
			eqos_unlock(dev);

			dev->stats.tx_frames++;
			return EQOS_OK;
		}
		eqos_unlock(dev);

		/* Ring full. The completion sweep signals the semaphore as it
		 * frees slots, so the wait ends the moment space exists; the
		 * timeout is only the belt-and-braces against a lost signal
		 * (and bounds the tcpip thread's stall while the ring drains
		 * at line rate). */
		if ((dev->tx_sem == NULL) ||
			(osSemaphoreAcquire(dev->tx_sem, DWC_EQOS_TX_WAIT_MS) != osOK)) {
			(void)osThreadYield();
		}
	}

	dev->stats.tx_dropped++;
	if (dev->stats.tx_dropped == 1) {
		board_log("%s: tx ring full, frame dropped\n", eqos_dev_name(dev));
	}

	return EQOS_ERROR_BUSY;
}

uint32_t dwc_eqos_get_rx_frame_size(unsigned int index)
{
	struct dwc_eqos_dev *dev;
	uint32_t len;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return 0;
	}
	dev = &devs[index];

	eqos_lock(dev);
	if (dev->rx_stage_head == dev->rx_stage_tail) {
		len = 0;
	} else {
		len = dev->rx_stage[dev->rx_stage_tail].len;
	}
	eqos_unlock(dev);

	return len;
}

int32_t dwc_eqos_read_frame(unsigned int index, uint8_t *frame, uint32_t len)
{
	struct dwc_eqos_dev *dev;
	struct dwc_eqos_stage *stage;
	uint32_t copy;

	if ((index >= DWC_EQOS_PORT_COUNT) || (frame == NULL)) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];

	eqos_lock(dev);
	if (dev->rx_stage_head == dev->rx_stage_tail) {
		eqos_unlock(dev);
		return 0;
	}
	stage = &dev->rx_stage[dev->rx_stage_tail];
	copy = (stage->len < len) ? stage->len : len;
	memcpy(frame, stage->data, copy);
	dev->rx_stage_tail = (dev->rx_stage_tail + 1) % dev->rx_stage_count;
	eqos_unlock(dev);

	return (int32_t)copy;
}

int32_t dwc_eqos_control(unsigned int index, uint32_t control, uint32_t arg)
{
	struct dwc_eqos_dev *dev;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];

	switch (control) {
	case DWC_EQOS_CONFIGURE:
		/* Speed and duplex.
		 *
		 * Reception is promiscuous (PACKET_FILTER.PR), which is the
		 * configuration this board was verified with - there is no
		 * address filter to program. */
		eqos_lock(dev);
		dev->configure = arg;
		if (dev->powered) {
			eqos_apply_configure(dev);
		}
		eqos_unlock(dev);
		return EQOS_OK;

	case DWC_EQOS_CONTROL_TX:
	case DWC_EQOS_CONTROL_RX:
		if (!dev->powered) {
			return EQOS_ERROR;
		}
		/* TX and RX share one DMA channel and one interrupt on this
		 * core, so they start and stop as a unit: enabling either
		 * enables the channel, disabling either stops it. */
		eqos_lock(dev);
		if (arg != 0) {
			(void)eqos_hw_start(dev);
		} else {
			(void)eqos_hw_stop(dev);
		}
		eqos_unlock(dev);
		return EQOS_OK;

	default:
		return EQOS_ERROR_UNSUPPORTED;
	}
}

void dwc_eqos_get_stats(unsigned int index, struct dwc_eqos_stats *stats)
{
	struct dwc_eqos_dev *dev;

	if ((index >= DWC_EQOS_PORT_COUNT) || (stats == NULL)) {
		return;
	}
	dev = &devs[index];
	eqos_lock(dev);
	*stats = dev->stats;
	eqos_unlock(dev);
}

void dwc_eqos_get_mmc_stats(unsigned int index, struct dwc_eqos_mmc_stats *stats)
{
	struct dwc_eqos_dev *dev;
	uintptr_t mmc;

	if ((index >= DWC_EQOS_PORT_COUNT) || (stats == NULL)) {
		return;
	}
	dev = &devs[index];
	mmc = dev->base_addr + 0x700u;
	stats->rx_pkts_gb = mmio_rd32(mmc + 0x80u);
	stats->rx_octets_gb = mmio_rd32(mmc + 0x84u);
	stats->rx_crc_error = mmio_rd32(mmc + 0x94u);
	stats->rx_fifo_overflow = mmio_rd32(mmc + 0xd4u);
}
