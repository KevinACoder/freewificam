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

#include "arch_asm.h"
#include "cache.h"
#include "os/cmsis2/cmsis2.h"
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
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
	/* Zero-copy bookkeeping: which slot's buffer each descriptor points
	 * at (re-arm pulls any spare, so the binding drifts), the spare-slot
	 * free list (pushed by dwc_eqos_rx_release in thread context under
	 * eqos_lock, popped only by the ISR sweep), and the ready ring of
	 * consumer handles the wrap hook returned. */
	uint8_t   rx_desc_slot[DWC_EQOS_RX_DESC_COUNT];
	uint8_t   rx_free_slots[DWC_EQOS_RX_SLOT_COUNT];
	uint16_t  rx_free_head;
	uint16_t  rx_free_count;
	void     *rx_ready[DWC_EQOS_RX_SLOT_COUNT];
	uint16_t  rx_ready_head;
	uint16_t  rx_ready_count;
#endif
	/* Arms the IOC bit on every DWC_EQOS_TX_IOC_DIV-th descriptor, so a
	 * burst costs one completion interrupt instead of one per frame. */
	unsigned int tx_ioc_seq;

	struct dwc_eqos_stats stats;

	/* --- N7-stab IRQ forensics (2026-10-02 sustained-TX freeze round) ----
	 * The 15-30 s freeze left no evidence anywhere: counters were all
	 * healthy up to the last heartbeat, then silence. These words are the
	 * interrupt-level black box: irq_n counts handler entries, irq_odd
	 * counts passes whose status word carried ONLY bits this driver does
	 * not service (the interrupt-storm signature - the embox-era incident
	 * on this same IP), last_status caches the most recent non-zero
	 * status word. Written in the ISR (single-word stores; the diag
	 * getter reads them racily - fine for forensics). */
	uint32_t irq_n;
	uint32_t irq_odd;
	uint32_t last_status;
	uint32_t irq_breaker;	/* set once AIE is dropped (storm guard) */
	uint32_t irq_odd_t0;	/* odd-pass burst window bookkeeping */
	uint32_t irq_odd_win;

	/* Signalled by the completion sweep (freed > 0): a sender parked on a
	 * full TX ring waits on this instead of polling a 1 ms tick. */
	osSemaphoreId_t tx_sem;
};

/* Per-instance state and DMA memory. Static, not allocated: the DMA needs
 * naturally-aligned, physically contiguous buffers.
 *
 * The descriptor rings live in the section the linker places into the
 * non-cacheable window (arch/aarch64/baremetal.ld `.noncacheable`,
 * mmu.c mmu_map_noncacheable). A 16-byte descriptor inside a 64-byte cache
 * line cannot be maintained by hand, because arming one descriptor flushes
 * the stale ARMED copy of its already-completed neighbours right over the
 * DMA's writeback, and the completion sweep then waits forever on an OWN bit
 * nothing will clear. Frame buffers stay in cacheable .bss: each is a whole
 * multiple of cache lines and has a single software writer, so its
 * flush/invalidate discipline is sound as-is. */
static struct dwc_eqos_dev devs[DWC_EQOS_PORT_COUNT];

#define NOC_SECTION \
	__attribute__((section(".noncacheable"), aligned(DWC_EQOS_BUF_ALIGN)))

static struct dwc_eqos_desc rx_rings[DWC_EQOS_PORT_COUNT][DWC_EQOS_RX_DESC_COUNT]
	NOC_SECTION;
static struct dwc_eqos_desc tx_rings[DWC_EQOS_PORT_COUNT][DWC_EQOS_TX_DESC_COUNT]
	NOC_SECTION;
static uint8_t rx_buffers[DWC_EQOS_PORT_COUNT][DWC_EQOS_RX_SLOT_COUNT][DWC_EQOS_BUF_STRIDE]
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
 * slot on RX. */
static inline void eqos_dsb_st(void)
{
	ARCH_DSB_ST();
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

static void eqos_setup_rx_desc(struct dwc_eqos_dev *dev, int idx,
			       unsigned int slot)
{
	struct dwc_eqos_desc *desc = &dev->rx_ring[idx];
	uint8_t *buf = rx_buffers[dev->plat->index][slot];
	uint32_t ctrl = DWC_EQOS_DESC_OWN | DWC_EQOS_DESC_RX_BUF1V;

	/* The buffer was written by software (.bss init) or by the previous
	 * DMA transfer: no stale dirty line may survive before the DMA starts
	 * writing into it again. */
	cache_dcache_flush((uintptr_t)buf, DWC_EQOS_RX_BUF_SIZE);
	cache_dcache_invalidate((uintptr_t)buf, DWC_EQOS_RX_BUF_SIZE);

#if defined(CONFIG_GMAC_RX_COALESCE) && CONFIG_GMAC_RX_COALESCE
	/* IOC off: RI is asserted on RX watchdog expiry instead of per frame
	 * (N7 R3 coalescing; the watchdog is programmed in eqos_hw_start). */
#else
	ctrl |= DWC_EQOS_DESC_RX_IOC;
#endif

	desc->des0 = (uint32_t)(uintptr_t)buf;
	desc->des1 = 0;
	desc->des2 = 0;
	desc->des3 = ctrl;
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
	dev->rx_desc_slot[idx] = (uint8_t)slot;
#endif
	/* No flush: the ring is in non-cacheable memory. The barrier keeps the
	 * grant visible to the DMA only after the buffer address and length. */
	eqos_dsb_st();
}

#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
/* Spare-slot free list. Popped only by the ISR sweep (atomic by nature);
 * pushed from dwc_eqos_rx_release under eqos_lock and, for refused frames,
 * from the sweep itself. */
static int eqos_slot_pop(struct dwc_eqos_dev *dev, unsigned int *slot)
{
	if (dev->rx_free_count == 0u) {
		return 0;
	}
	*slot = dev->rx_free_slots[dev->rx_free_head];
	dev->rx_free_head = (uint16_t)((dev->rx_free_head + 1u)
				       % DWC_EQOS_RX_SLOT_COUNT);
	dev->rx_free_count--;
	return 1;
}

static void eqos_slot_push(struct dwc_eqos_dev *dev, unsigned int slot)
{
	if (dev->rx_free_count >= DWC_EQOS_RX_SLOT_COUNT) {
		return;	/* double release: better leaked than wrapped */
	}
	dev->rx_free_slots[(dev->rx_free_head + dev->rx_free_count)
			   % DWC_EQOS_RX_SLOT_COUNT] = (uint8_t)slot;
	dev->rx_free_count++;
}
#endif

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

#if defined(CONFIG_GMAC_RX_COALESCE) && CONFIG_GMAC_RX_COALESCE
	/* RX interrupt coalescing (N7 R3): with every RX descriptor's IOC off,
	 * the channel asserts RI when the RX watchdog expires instead of once
	 * per frame - at an 800 Mbit/s reverse flow that is 66k IRQ/s down to
	 * ~3k. Unit 256 DMA clock ticks; 0xa0 is the Linux default on this
	 * board (DEF_DMA_RIWT). */
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_WATCHDOG, CONFIG_GMAC_RX_RIWT);
#endif

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

	/* MMC counter interrupts: mask ALL of them. The 2026-10-02 freeze
	 * root cause (N7-stab, board-measured): the MMC block raises its own
	 * interrupt when a 32-bit counter crosses its half-way point (bit 31);
	 * the TX octet counter does so after exactly 2 GiB of transmitted
	 * traffic - ~26 s at the 650 Mbit/s stream, which is the measured
	 * freeze onset. The event latches into GMAC_INT_STATUS bit 10
	 * (mmc_tx_irq, read-clear at 0x0B0) AND holds through the MMC status
	 * word; the silicon reset value of the three masks is 0 (unmasked),
	 * and this driver never touched them, so the very first 2 GiB TX run
	 * deadlocked the machine: the DMA-status handler saw an empty status
	 * word, never cleared the MAC latch, and the level line re-entered it
	 * forever below the tick's priority. Linux takes the same measure -
	 * stmmac_mmc_setup() ("this masks the MMC irq, in fact, the counters
	 * are managed in SW"; mmc_core.c dwmac_mmc_intr_all_mask writes
	 * 0xffffffff to all three). The counters stay readable in software
	 * (dwc_eqos_get_mmc_stats); only their interrupt signal is cut. */
	eqos_wr(dev, DWC_EQOS_MMC_RX_INTR_MASK, 0xffffffffu);
	eqos_wr(dev, DWC_EQOS_MMC_TX_INTR_MASK, 0xffffffffu);
	eqos_wr(dev, DWC_EQOS_MMC_RX_IPC_INTR_MASK, 0xffffffffu);
	/* Clear whatever the firmware/counters latched before the mask went
	 * up; the MMC RX/TX interrupt status registers are read-clear. */
	(void)eqos_rd(dev, DWC_EQOS_MMC_RX_INTR);
	(void)eqos_rd(dev, DWC_EQOS_MMC_TX_INTR);

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

#if defined(CONFIG_GMAC_RX_HWCSUM) && CONFIG_GMAC_RX_HWCSUM
	/* RX checksum offload (N7 R3): the IPC block validates the L3/L4
	 * checksums of every IP frame and reports through RDES1 (valid only
	 * under the des3 RDES1_VALID flag); the completion sweep drops flagged
	 * frames and lwipopts disables the stack-side software verification in
	 * lockstep. Bit 27 = Linux dwmac4.h GMAC_CONFIG_IPC; the running Linux
	 * on this board dumps Reg0 with exactly this bit as the only MAC_CONFIG
	 * difference. */
	reg = eqos_rd(dev, DWC_EQOS_MAC_CONFIGURATION);
	reg |= DWC_EQOS_MAC_CONF_IPC;
	eqos_wr(dev, DWC_EQOS_MAC_CONFIGURATION, reg);
	board_log("%s: rx hw checksum (IPC) on\n", eqos_dev_name(dev));
#endif


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
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
	/* Spare-slot list armed with every slot first; arming the descriptors
	 * then consumes the first ring-depth entries, leaving exactly the
	 * spare population the zero-copy handoff draws from. */
	dev->rx_free_head = 0;
	dev->rx_free_count = 0;
	dev->rx_ready_head = 0;
	dev->rx_ready_count = 0;
	for (i = 0; i < DWC_EQOS_RX_SLOT_COUNT; i++) {
		eqos_slot_push(dev, (unsigned int)i);
	}
#endif

	for (i = 0; i < DWC_EQOS_RX_DESC_COUNT; i++) {
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
		unsigned int slot;

		(void)eqos_slot_pop(dev, &slot);
		eqos_setup_rx_desc(dev, i, slot);
#else
		eqos_setup_rx_desc(dev, i, (unsigned int)i);
#endif
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
 * the price of keeping the DMA continuous - unless the zero-copy mode
 * lends the buffer out and re-arms from the spare-slot list instead
 * (CONFIG_GMAC_RX_ZC, same invariant, zero copies).
 *
 * The sweep walks the whole ring: once the DMA's fetch pointer reaches END
 * it wraps to the ring base, so writebacks land wherever the next frame
 * went, not at a moving head. Callable from the interrupt and the
 * watchdog sweep. */
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
static dwc_eqos_rx_wrap_fn s_rx_zc;

/* Hand the completed frame up through the wrap hook and pick the re-arm
 * slot: a spare on success, the frame's own buffer on any refusal (the
 * frame is then dropped, never parked - the DMA never waits). */
static unsigned int eqos_zc_handoff(struct dwc_eqos_dev *dev, unsigned int own,
				    uint8_t *buf, unsigned int len, int *handed)
{
	unsigned int spare;
	void *h;

	*handed = 0;
	if ((s_rx_zc == NULL) || !eqos_slot_pop(dev, &spare)) {
		dev->stats.rx_dropped++;
		return own;
	}
	h = s_rx_zc(dev->plat->index, own, buf, len);
	if (h == NULL) {
		eqos_slot_push(dev, spare);
		dev->stats.rx_dropped++;
		return own;
	}
	if (dev->rx_ready_count >= DWC_EQOS_RX_SLOT_COUNT) {
		/* Consumer wedged (drain stopped): refuse, recycle. */
		eqos_slot_push(dev, spare);
		dev->stats.rx_dropped++;
		return own;
	}
	dev->rx_ready[(dev->rx_ready_head + dev->rx_ready_count)
		      % DWC_EQOS_RX_SLOT_COUNT] = h;
	dev->rx_ready_count++;
	dev->stats.rx_frames++;
	*handed = 1;
	return spare;
}
#endif

static int eqos_rx_complete(struct dwc_eqos_dev *dev)
{
	int delivered = 0;
	int last_armed = -1;
	int i;

	for (i = 0; i < DWC_EQOS_RX_DESC_COUNT; i++) {
		struct dwc_eqos_desc *desc;
		uint8_t *buf;
		unsigned int len;
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
		unsigned int slot_now = dev->rx_desc_slot[i];
#else
		const unsigned int slot_now = (unsigned int)i;
#endif

		if (dev->rx_in_flight[i] != 1) {
			continue;
		}
		desc = &dev->rx_ring[i];
		/* No invalidate: non-cacheable ring, the read hits memory. */
		if (desc->des3 & DWC_EQOS_DESC_OWN) {
			continue;
		}

		dev->rx_in_flight[i] = 2;
		buf = rx_buffers[dev->plat->index][slot_now];
		len = desc->des3 & DWC_EQOS_DESC_RX_LEN_MASK;

		/* The slot the descriptor re-arms with: a spare when the frame
		 * was lent out (zero-copy), its own buffer in every other
		 * case - the frame is dropped, never parked, so the DMA never
		 * waits on a descriptor. */
		{
			unsigned int rearm = slot_now;
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
			int handed = 0;
#endif

			if (len < 4) {
				dev->stats.rx_errors++;
			} else if (
#if defined(CONFIG_GMAC_RX_HWCSUM) && CONFIG_GMAC_RX_HWCSUM
				   /* IPC verdict first (descriptor reads only - a
				    * dropped frame costs no cache maintenance).
				    * RDES1_VALID is set for IP frames only, so
				    * ARP & friends pass this gate untouched. */
				   ((desc->des3 & DWC_EQOS_DESC_RX_RDES1_VALID) == 0) ||
				   ((desc->des1 & (DWC_EQOS_RDES1_IP_HDR_ERR |
						   DWC_EQOS_RDES1_IP_PAYLOAD_ERR)) == 0)
#else
				   1
#endif
			) {
				cache_dcache_invalidate((uintptr_t)buf, len);
				/* Strip the frame check sequence: the MAC was
				 * configured not to, and one strip is the
				 * right number. */
				len -= 4;
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
				rearm = eqos_zc_handoff(dev, slot_now, buf, len,
							&handed);
				if (handed) {
					delivered++;
				}
#else
				if (eqos_stage_put(dev, buf, len) == 0) {
					delivered++;
				}
#endif
			} else {
				dev->stats.rx_csum_drops++;
			}

			eqos_setup_rx_desc(dev, i, rearm);
		}
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

#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
void dwc_eqos_set_rx_zc(dwc_eqos_rx_wrap_fn fn)
{
	s_rx_zc = fn;
}

int dwc_eqos_rx_dequeue(unsigned int index, void **handle)
{
	struct dwc_eqos_dev *dev;

	if ((index >= DWC_EQOS_PORT_COUNT) || (handle == NULL)) {
		return 0;
	}
	dev = &devs[index];
	if (!dev->started) {
		return 0;
	}

	/* The ready ring's producer is the ISR; the port-line mask is the
	 * whole mutual exclusion on this single-core image (same contract as
	 * every other ring in this driver). */
	eqos_lock(dev);
	if (dev->rx_ready_count == 0u) {
		eqos_unlock(dev);
		return 0;
	}
	*handle = dev->rx_ready[dev->rx_ready_head];
	dev->rx_ready_head = (uint16_t)((dev->rx_ready_head + 1u)
					% DWC_EQOS_RX_SLOT_COUNT);
	dev->rx_ready_count--;
	eqos_unlock(dev);
	return 1;
}

void dwc_eqos_rx_release(unsigned int index, unsigned int slot)
{
	struct dwc_eqos_dev *dev;

	if ((index >= DWC_EQOS_PORT_COUNT) || (slot >= DWC_EQOS_RX_SLOT_COUNT)) {
		return;
	}
	dev = &devs[index];
	if (!dev->started) {
		return;
	}

	/* Thread context (the consumer's pbuf-free callback); the lock keeps
	 * the push atomic against the ISR sweep's pops. */
	eqos_lock(dev);
	eqos_slot_push(dev, slot);
	eqos_unlock(dev);
}
#endif

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

	dev->irq_n++;
	for (pass = 0; pass < 8; pass++) {
		uint32_t status = eqos_rd(dev, DWC_EQOS_DMA_CH0_STATUS);

		if (status == 0) {
			/* Line-source defense (N7-stab): a latch in the MAC/MMC
			 * blocks holds this shared level line with a clean DMA
			 * status word - the 2026-10-02 freeze, where the handler
			 * re-entered on an all-zero status forever. Reading
			 * GMAC_INT_STATUS clears its latch (read-clear); doing it
			 * on the empty-pass exit bounds any future unserviced
			 * source to one spurious entry per real interrupt instead
			 * of a deadlock. The known MMC source is masked at init;
			 * this is the belt for the next one. */
			if (eqos_rd(dev, DWC_EQOS_MAC_INTERRUPT_STATUS) != 0u) {
				dev->irq_odd++;
			}
			break;
		}
		/* Write-one-to-clear everything latched: leaving e.g. the
		 * abnormal summary set keeps the line asserted forever. */
		eqos_wr(dev, DWC_EQOS_DMA_CH0_STATUS, status);
		dev->last_status = status;

		if (!(status & (DWC_EQOS_CH0_STATUS_RI | DWC_EQOS_CH0_STATUS_RWT
				| DWC_EQOS_CH0_STATUS_TI
				| DWC_EQOS_CH0_STATUS_FB))) {
			/* N7-stab: an "odd pass" - the status word carried only
			 * bits this driver does not service (e.g. TBU/RBU under
			 * the abnormal summary). The embox era on this same IP
			 * died exactly this way: a re-latching unserviced bit is
			 * a level-line interrupt storm, and while it storms,
			 * nothing at or below GMAC priority (tick included) ever
			 * runs again. Count it, print the first occurrence (the
			 * lock-free emergency outlet - a storm must not die
			 * silently), and bound the damage with a burst gate. */
			uint32_t now;

			dev->irq_odd++;
			if ((dev->irq_odd == 1u) ||
			    ((dev->irq_odd & (dev->irq_odd - 1u)) == 0u)) {
				board_log_emergency(
					"gmac: irq odd pass n=%u status=%08x ien=%08x pend=%d",
					(unsigned int)dev->irq_odd,
					(unsigned int)status,
					(unsigned int)eqos_rd(dev,
						DWC_EQOS_DMA_CH0_INTR_ENABLE),
					(int)gic_irq_is_pending(dev->plat->irq_num));
			}
#if defined(CONFIG_GMAC_IRQ_BREAKER) && CONFIG_GMAC_IRQ_BREAKER
			/* Burst gate: >= 32 odd passes inside a 100 ms window is
			 * a storm, not noise. Drop the abnormal-summary enable
			 * (AIE) so the unserviceable class stops delivering;
			 * RI/RWT/TI/FBE stay armed (the RX watchdog sweep in the
			 * boot loop still drains RX if RWT turns out to ride
			 * AIE on this core). One-way, announced. */
			now = osKernelGetTickCount();
			if ((uint32_t)(now - dev->irq_odd_t0) > 100u) {
				dev->irq_odd_t0 = now;
				dev->irq_odd_win = 0u;
			}
			if (dev->irq_odd_win < 0xffffu) {
				dev->irq_odd_win++;
			}
			if ((dev->irq_odd_win >= 32u) && (dev->irq_breaker == 0u)) {
				uint32_t ien = eqos_rd(dev,
					DWC_EQOS_DMA_CH0_INTR_ENABLE);

				eqos_wr(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE,
					ien & ~DWC_EQOS_CH0_INTR_AIE);
				dev->irq_breaker = 1u;
				board_log_emergency(
					"gmac: irq storm breaker: %u odd in window, status=%08x ien=%08x->%08x",
					(unsigned int)dev->irq_odd_win,
					(unsigned int)status,
					(unsigned int)ien,
					(unsigned int)(ien &
						~DWC_EQOS_CH0_INTR_AIE));
			}
#endif
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

		if (status & (DWC_EQOS_CH0_STATUS_RI | DWC_EQOS_CH0_STATUS_RWT)) {
			/* RWT (watchdog expiry) is the coalesced RI: with the
			 * RX descriptor IOC off it is the only RX interrupt
			 * source. Counted here, not at entry, so TX-only
			 * passes do not dilute the coalescing gauge. */
			dev->stats.rx_irqs++;
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

void dwc_eqos_irq_dump(unsigned int index)
{
	struct dwc_eqos_dev *dev;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return;
	}
	dev = &devs[index];
	if (dev->plat == NULL) {
		return;
	}

	/* One line, all blocks sharing the line. The MAC status read is
	 * READ-CLEAR: its value is the latch evidence, and the clear itself
	 * is the unstick attempt (if the system resumes right after this
	 * dump, the latch WAS a line source). */
	board_log_emergency(
		"irqdump mac_st=%08x mtl_int=%08x mtl_c0=%08x mmc_rx=%08x mmc_tx=%08x mmc_masks=%08x/%08x ipc_mask=%08x",
		(unsigned int)eqos_rd(dev, DWC_EQOS_MAC_INTERRUPT_STATUS),
		(unsigned int)eqos_rd(dev, DWC_EQOS_MTL_INT_STATUS),
		(unsigned int)eqos_rd(dev, DWC_EQOS_MTL_CHAN0_INT_CTRL),
		(unsigned int)eqos_rd(dev, DWC_EQOS_MMC_RX_INTR),
		(unsigned int)eqos_rd(dev, DWC_EQOS_MMC_TX_INTR),
		(unsigned int)eqos_rd(dev, DWC_EQOS_MMC_RX_INTR_MASK),
		(unsigned int)eqos_rd(dev, DWC_EQOS_MMC_TX_INTR_MASK),
		(unsigned int)eqos_rd(dev, DWC_EQOS_MMC_RX_IPC_INTR_MASK));
}

void dwc_eqos_get_diag(unsigned int index, struct dwc_eqos_diag *diag)
{
	struct dwc_eqos_dev *dev;

	if ((index >= DWC_EQOS_PORT_COUNT) || (diag == NULL)) {
		return;
	}
	dev = &devs[index];
	if (dev->plat == NULL) {
		/* Pre-initialize probe call (the fast probe runs from boot):
		 * nothing to report yet, and eqos_lock would deref NULL. */
		memset(diag, 0, sizeof(*diag));
		return;
	}

	/* Ring indices under the port-line lock; register/black-box reads are
	 * single-word and taken as-is (forensic snapshots race by design). */
	eqos_lock(dev);
	diag->tx_head = (uint32_t)dev->tx_head;
	diag->tx_tail = (uint32_t)dev->tx_tail;
	eqos_unlock(dev);

	diag->tx_ioc_seq = dev->tx_ioc_seq;
	diag->irq_n = dev->irq_n;
	diag->irq_odd = dev->irq_odd;
	diag->last_status = dev->last_status;
	diag->irq_breaker = dev->irq_breaker;
	diag->ch0_status = eqos_rd(dev, DWC_EQOS_DMA_CH0_STATUS);
	diag->intr_enable = eqos_rd(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE);
	diag->gic_pending = gic_irq_is_pending(dev->plat->irq_num);

#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
	diag->rx_ready_count = (uint32_t)dev->rx_ready_count;
	diag->rx_free_count = (uint32_t)dev->rx_free_count;
#else
	diag->rx_ready_count = 0u;
	diag->rx_free_count = 0u;
#endif

	diag->tx_sem_count = (dev->tx_sem != NULL)
				     ? osSemaphoreGetCount(dev->tx_sem) : 0u;
}
