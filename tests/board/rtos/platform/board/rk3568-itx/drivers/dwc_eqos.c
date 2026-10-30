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
 * Threading in this image: the contexts are the service thread (watchdog
 * sweep) and this port's interrupt. The port-line mask is the whole mutual
 * exclusion - masking the port's own line covers thread-vs-interrupt, and
 * nothing else in the system touches these rings. The completion sweep also
 * runs from dwc_eqos_watchdog(), invoked by the service thread; osDelay is
 * the CMSIS-RTOS2 delay the port layer provides.
 *
 * @author zhugengyu
 * @date   16.09.2026 (board-verified core) / 02.10.2026 (port re-arrangement)
 */

#include <stdint.h>

#include "arch_asm.h"
#include "cache.h"
#include "generic_timer.h"
#include "os/cmsis2/cmsis2.h"
#include "gicv3.h"
#include "log.h"
#include <string.h>

#include "board_conf.h"
#include "mmio.h"

#include "dwc_eqos.h"

/* The line's GIC priority: the board's default SPI class, or the kernel's
 * API-call ceiling when the build has to respect one (the FreeRTOS board
 * image passes 0xd0u, the same value its USB controller line carries). */
#ifndef EQOS_BOARD_IRQ_PRIORITY_RAW
#define EQOS_BOARD_IRQ_PRIORITY_RAW BOARD_SPI_DEFAULT_PRIORITY_RAW
#endif

/* Bus clock the MAC sees: pclk_gmac is 100 MHz after the CRU dividers. It sets
 * the 1us tick counter and the MDIO divider (see DWC_EQOS_MDIO_CR). */
#define DWC_EQOS_PCLK_HZ	100000000u

/* Programmable burst lengths, as in the verified configuration. With PBLx8
 * programmed (eqos_hw_start) the field means bursts of PBL * 8 beats, so the
 * receive side moves 256 bytes per burst and the transmit side 1024. The two
 * sides are not symmetric here, and the receive one is the only register
 * difference from the kernel driver on this board (whose register dump carries
 * 32/32) - so the receive value is a build lever for the throughput round:
 *   LEVERS=-DCONFIG_GMAC_RXPBL=32
 * A burst costs a fixed arbitration/FIFO turnaround, so a 1600-byte frame's
 * burst count is what a rate is divided by. */
#ifndef CONFIG_GMAC_TXPBL
#define DWC_EQOS_TXPBL		32u
#else
#define DWC_EQOS_TXPBL		((uint32_t)CONFIG_GMAC_TXPBL)
#endif
#ifndef CONFIG_GMAC_RXPBL
#define DWC_EQOS_RXPBL		8u
#else
#define DWC_EQOS_RXPBL		((uint32_t)CONFIG_GMAC_RXPBL)
#endif

/* How long SendFrame waits for a descriptor before reporting busy. At line
 * rate a full 64-slot ring drains in under a millisecond, so a couple of
 * milliseconds is generous; longer than that means the link is not moving. */
#define DWC_EQOS_TX_WAIT_MS	4u

/* RX sweep context. Default (2026-10-17) = the BOTTOM HALF: the ISR signals and
 * the RX thread runs the sweep, because holding the interrupt across an
 * O(ring) walk over non-cacheable memory (28 us measured) is what starved the
 * path - the split measured +24% uplink at zero cost. The interrupt-context
 * sweep stays reachable as a reverse lever (-DCONFIG_GMAC_RX_BH=0), since every
 * measurement before 2026-10-17 was taken with it. CONFIG_GMAC_RX_NAPI implies
 * the bottom half and adds the interrupt mask + budget on top (dwc_eqos_rx_poll). */
#if !defined(CONFIG_GMAC_RX_BH)
#define DWC_EQOS_RX_BH		1
#else
#define DWC_EQOS_RX_BH		CONFIG_GMAC_RX_BH
#endif
#if defined(CONFIG_GMAC_RX_NAPI) && CONFIG_GMAC_RX_NAPI
#undef DWC_EQOS_RX_BH
#define DWC_EQOS_RX_BH		1
#endif

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
	/* Sweep cursor: the first slot that may hold a frame the DMA has not
	 * written back yet. The DMA writes back in ring order, so everything
	 * from here up to the first descriptor it still owns is complete - the
	 * bounded walk (CONFIG_GMAC_RX_SCAN=2) starts here and stops there
	 * instead of probing all 128 slots. Advanced by the sweep only. */
	unsigned int rx_scan;
	/* Last slot the sweep re-armed (-1 = none), for the ring snapshot. */
	int       rx_last_armed;
	/* Sticky sweep telemetry: what the LAST sweep saw. The phase-boundary
	 * snapshots cannot see a failure's steady state (traffic stops and the
	 * 100 ms watchdog drains the ring), so the numbers that explain a
	 * collapse have to be recorded while it happens. */
	uint32_t  sw_sweeps;		/* sweeps that ran */
	uint32_t  sw_clear_seen;	/* OWN-clear slots this sweep saw */
	uint32_t  sw_max_clear;		/* worst case of the above */
	int32_t   sw_curdesc;		/* CUR_RX_DESC slot read at sweep entry */
	int32_t   sw_last_armed;	/* the same sweep's last_armed */
	int32_t   rx_dma_pos;		/* DMA frontier (SCAN=3): last position read */
	uint32_t  sw_span;		/* slots the SCAN=3 walk covered */
	/* NAPI shape: 1 while the RX interrupt is masked and the RX thread owns
	 * the drain (dwc_eqos_rx_poll). */
	uint8_t   rx_masked;
	/* Interrupt-shape runtime state (2026-10-19 census round; the setters
	 * carry the contract). rx_ioc_off stops arming IOC on re-armed RX
	 * descriptors, irq_oddmode picks the TBU/ETI treatment, tx_ioc_div is
	 * the TX completion interval. Defaults come from the build macros. */
	uint8_t   irq_oddmode;
	uint8_t   rx_ioc_off;
	unsigned int tx_ioc_div;
	/* One-sweep-at-a-time guard (LDREX/STREX): the RX thread and the
	 * service thread's watchdog both call eqos_rx_complete, and eqos_lock
	 * does not stop a thread switch, so without this the two sweeps
	 * interleave and corrupt the shared cursor and tail publish. */
	uint8_t   rx_sweep_busy;
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

	/* TX-ladder discriminators (see struct dwc_eqos_diag). Written from the
	 * sender thread only; the diag getter reads them as a forensic
	 * snapshot, exactly like the IRQ black box above. */
	uint32_t tx_waits;	/* sends that found the ring full */
	uint32_t tx_wait_ticks;	/* kernel ticks spent parked on tx_sem */
	uint32_t tx_occ_max;	/* peak descriptors in flight */

	/* CONFIG_GMAC_TX_CSUM capability, latched at hw_start: the CIC tagging
	 * only happens when the core actually has the inserter (HW_FEATURE1
	 * TXCOSEL), because a zero-checksum TCP frame without it would leave
	 * broken - the one failure mode this feature must not have. */
	uint8_t tx_csum_cap;
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

/* Service-time profile storage (see struct dwc_eqos_prof in the header and
 * dwc_eqos_get_prof below). The counter is the 24 MHz system counter, so one
 * tick is 41.7 ns: a bucket is meaningful in aggregate (a phase is seconds
 * long), not per event. Reads cost tens of cycles, which is why the buckets
 * sit at operation boundaries (per interrupt, per frame), not inside loops. */
static struct dwc_eqos_prof eqos_prof[DWC_EQOS_PORT_COUNT];

/* The switch that decides whether a measurement boot carries the cost
 * instrument at all. Set CONFIG_GMAC_PROF=0 and the 24 MHz counter is never
 * read: every t0()/accumulate pair folds to zero and the compiler drops the
 * stores with it. The instrument is otherwise always on, because a number
 * whose own cost is unknown is not a measurement (AGENTS 1.2: an instrument's
 * overhead has to be part of its acceptance, not a footnote). */
static inline uint64_t eqos_prof_t0(void)
{
#if defined(CONFIG_GMAC_PROF) && CONFIG_GMAC_PROF
	return gtimer_count_get();
#else
	return 0u;
#endif
}

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

	LOG_INFO("%s: link %s Mbps %s duplex\n", eqos_dev_name(dev), str,
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
		LOG_WARNING("%s: dma soft reset timeout, mode=0x%08x\n",
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

	/* IOC arming: build-defaulted (CONFIG_GMAC_RX_COALESCE turns per-frame
	 * RI off and leaves the RX watchdog as the only RX source - N7 R3), but
	 * runtime-flippable through dwc_eqos_set_rx_coalesce so a measurement
	 * boot sweeps coalescing without a rebuild. The flip lands as the RX
	 * thread re-arms descriptors, one ring turnover. */
	if (!dev->rx_ioc_off) {
		ctrl |= DWC_EQOS_DESC_RX_IOC;
	}

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

#if defined(CONFIG_GMAC_RX_POLL) && CONFIG_GMAC_RX_POLL
	/* Pure polling (2026-10-16): no channel interrupt at all. The RX service
	 * thread calls dwc_eqos_watchdog in a loop instead, so the whole path -
	 * the descriptor sweep included - runs in THREAD context, where it can
	 * yield, and no interrupt ever preempts the TCP/IP thread. */
	eqos_wr(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE, 0);
#else
	eqos_wr(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE, DWC_EQOS_CH0_INTR_DEFAULT);
#endif

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

	/* Sweep bookkeeping restarts with the channel: the DMA fetches from the
	 * ring base, so that is the frontier until the first sweep reads it. */
	dev->rx_scan = 0;
	dev->rx_dma_pos = 0;
	dev->rx_last_armed = -1;

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
	LOG_INFO("%s: mac intr status=0x%08x enable=0x%08x\n", eqos_dev_name(dev),
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
#if defined(CONFIG_GMAC_TX_CSUM) && CONFIG_GMAC_TX_CSUM
	dev->tx_csum_cap =
		((eqos_rd(dev, DWC_EQOS_MAC_HW_FEATURE0) & DWC_EQOS_HW_TXCOSEL) != 0u)
		? 1u : 0u;
	if (dev->tx_csum_cap == 0u) {
		LOG_WARNING("%s: tx checksum offload requested but the core "
			    "has no inserter (HW_FEATURE0 TXCOSEL=0); CIC stays 0\n",
			    eqos_dev_name(dev));
	}
#endif
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
	LOG_INFO("%s: rx hw checksum (IPC) on\n", eqos_dev_name(dev));
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
	struct dwc_eqos_prof *prof = &eqos_prof[dev->plat->index];

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

	/* Arrival rhythm + reader lag (see struct dwc_eqos_prof): the staging
	 * instant is the DMA's arrival seen by software (the completion sweep
	 * runs within a wake of the writeback), and the ring occupancy right
	 * after this put is how far the reader trails the producer. */
	{
		uint64_t now = eqos_prof_t0();
		unsigned int occ = (dev->rx_stage_head + dev->rx_stage_count
				    - dev->rx_stage_tail) % dev->rx_stage_count;

		if (prof->rx_last_stage_ticks != 0u) {
			prof->rx_gap_hist[gmac_prof_hist_bucket(
				now - prof->rx_last_stage_ticks)]++;
			prof->rx_gap_samples++;
		}
		prof->rx_last_stage_ticks = now;
		if (occ > prof->rx_stage_high_water) {
			prof->rx_stage_high_water = occ;
		}
	}

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

static int eqos_ring_slot(const struct dwc_eqos_dev *dev, uint32_t addr);

/* Sweep the descriptors the DMA has written back and re-arm each one.
 *
 * TWO SHAPES, AND ONLY ONE OF THEM WORKS - board-measured 2026-10-17, do not
 * "fix" this by shortening the walk:
 *
 *  - whole-ring scan (default): probe all 128 slots, re-arm every one the DMA
 *    wrote back, and write RX_END = the HIGHEST index re-armed.
 *  - bounded walk (CONFIG_GMAC_RX_SCAN=2, FALSIFIED): start at the software
 *    cursor, stop at the first descriptor the DMA still owns.
 *
 * The bounded walk is correct about *coverage* - the `gmac ring` snapshot
 * shows `stale=0` throughout, i.e. it never left an OWN-clear slot outside its
 * run - and it still collapsed the downlink to 2.0-2.6 Mbit/s (uplink 30.8)
 * with `ovf=173`, the third such failure (the 2026-10-16 cursor and linux-shape
 * arms and the 2026-10-17 bounded-walk arm). The working arm reproduces the
 * baseline exactly (269.5 up / 481.7 down, zero loss) and its snapshot shows
 * the tail invariant that matters: RX_END = cursor-1 = last re-armed, with
 * CUR_RX_DESC sitting exactly ON the cursor, and zero OWN-clear slots at rest.
 * The bounded arm's snapshot in the failing state shows the same relative
 * geometry with five slots outstanding and the DMA already at CUR_RX_DESC=17
 * while the cursor is 12 - so what breaks is the *interaction* between the
 * landing point and how far the DMA is allowed to run ahead, not the coverage.
 * Settling that needs the controller's tail semantics, which is an open
 * question (consulted 2026-10-17); until then the whole-ring scan ships.
 *
 * What the scan costs is the point of the exercise: 128 uncached descriptor
 * probes per sweep measured 101 probes per interrupt and 17.6 us per delivered
 * frame (walkslots/rxwalk, crc 23b5b7f8) - 1.4 s of every wall-clock second at
 * 940 Mbit/s, i.e. the whole gap to the Linux lane. */

#if defined(CONFIG_GMAC_RX_HWCSUM) && CONFIG_GMAC_RX_HWCSUM
/*
 * The IPC block's receive verdict, applied only where it means something.
 *
 * The MAC validates the L3/L4 checksum of IP frames, but only for the protocols
 * it was told about: TCP and UDP carry a pseudo-header and are what this port
 * asks it to verify, while ICMP has no pseudo-header and the block reports its
 * payload check as an error. Dropping on that verdict alone cost a whole arm
 * (2026-10-25: every echo reply died in the driver, ping 0/10, while TCP and UDP
 * were healthy). So:
 *   - no RDES1_VALID  -> not an IP frame, pass;
 *   - IP header error  -> a real error whatever the protocol, drop;
 *   - payload error, TCP/UDP -> drop (that is the check we asked for);
 *   - payload error, anything else -> no verdict was taken, pass.
 * The protocol byte lives in the buffer, so the frame's first cache line has to
 * be invalidated before it is read; that is one extra op on the error path only.
 */
static int eqos_rx_ipc_ok(const struct dwc_eqos_desc *desc, uint32_t d3,
			  uint8_t *buf)
{
	uint8_t proto;

	if ((d3 & DWC_EQOS_DESC_RX_RDES1_VALID) == 0u) {
		return 1;
	}
	if ((desc->des1 & DWC_EQOS_RDES1_IP_HDR_ERR) != 0u) {
		return 0;
	}
	if ((desc->des1 & DWC_EQOS_RDES1_IP_PAYLOAD_ERR) == 0u) {
		return 1;
	}
	cache_dcache_invalidate((uintptr_t)buf, 64u);
	proto = buf[14u + 9u];
	return ((proto != 6u) && (proto != 17u)) ? 1 : 0;
}
#endif

static int eqos_rx_complete(struct dwc_eqos_dev *dev)
{
	unsigned int port = dev->plat->index;
	int delivered = 0;
	int last_armed = -1;
	int max_armed = -1;	/* highest index re-armed this pass: the tail
				   publish value (SCAN=5); in a wrap pass it is an
				   old high slot, NOT the last processed young one */
	int stop_slot = -1;	/* first descriptor still owned when a walk breaks */
	int i;
	int left;
	uint64_t t_walk = eqos_prof_t0();

	/* ONE SWEEP AT A TIME (2026-10-19). Two contexts call this: the RX
	 * thread (signalled per interrupt) and the service thread's 100 ms
	 * watchdog. eqos_lock only masks interrupts - it does NOT stop a
	 * thread switch, so the two sweeps interleave on this single core and
	 * corrupt the shared cursor, the last-armed bookmark and the tail
	 * publish (codex round-2 named the watchdog's unconditional RX_END
	 * poke as a second writer). The loser of the race returns 0: it has
	 * no work that the winner is not already doing, and the next
	 * interrupt or watchdog tick calls again. LDREX/STREX on A55 makes
	 * the test-and-set a single indivisible step. */
#if defined(__aarch64__)
	if (__atomic_test_and_set(&dev->rx_sweep_busy, __ATOMIC_ACQUIRE)) {
		return 0;
	}
#endif

	eqos_prof[port].rx_sweeps++;
#if defined(CONFIG_GMAC_SWEEP_WIDE) && CONFIG_GMAC_SWEEP_WIDE
	/* One pure-load pass over the non-cacheable ring, then process from
	 * these values: with nothing but loads in the loop the core can keep
	 * several misses in flight at once, so the same 128 values arrive
	 * sooner. Semantics are unchanged - same probes, same tail landing
	 * point. 512 bytes of stack, and the array cannot be static: the ISR
	 * and the RX thread can both sweep (RXBH), and the ISR can preempt the
	 * thread's sweep. */
	uint32_t own[DWC_EQOS_RX_DESC_COUNT];
#endif

#if defined(CONFIG_GMAC_RX_SCAN) && (CONFIG_GMAC_RX_SCAN == 3)
	/* Hardware-frontier walk: the controller's own position decides the
	 * range, not the software's cursor. The walk covers the slots between
	 * the position read last sweep and the position read now - i.e. exactly
	 * what the DMA wrote in between - which is the same set the whole-ring
	 * scan consumes, but reached in O(frames) probes and immune to the
	 * software losing track of the DMA through a ring wrap. One extra slot
	 * is covered because CUR_RX_DESC may report either the descriptor being
	 * fetched or the next one; the OWN test rejects the extra, and `continue`
	 * (not `break`) is used so the span bound is what stops the walk. */
	{
		int slot = eqos_ring_slot(dev,
			eqos_rd(dev, DWC_EQOS_DMA_CH0_CUR_RX_DESC));

		if (slot < 0) {
			slot = 0;
		}
		if (dev->rx_dma_pos < 0) {
			dev->rx_dma_pos = slot;
		}
		dev->sw_span = (uint32_t)((slot - dev->rx_dma_pos
					   + DWC_EQOS_RX_DESC_COUNT)
					  % DWC_EQOS_RX_DESC_COUNT) + 1u;
		dev->sw_curdesc = slot;
		i = (int)dev->rx_dma_pos;
		dev->rx_dma_pos = slot;
	}
	left = (int)dev->sw_span;
#elif defined(CONFIG_GMAC_RX_SCAN) && (CONFIG_GMAC_RX_SCAN == 4)
	/* FIXED bounded walk (2026-10-17): probe from the cursor to the first
	 * still-owned descriptor, but publish the tail as the STOP POINT, not as
	 * the last slot re-armed. See the tail rule below. */
	i = (int)dev->rx_scan;
	left = DWC_EQOS_RX_DESC_COUNT;
#elif defined(CONFIG_GMAC_RX_SCAN) && (CONFIG_GMAC_RX_SCAN == 6)
	/* ARRIVAL-ORDERED FULL PASS (2026-10-19 round 4). The default walk
	 * starts at slot 0 every pass, so a pending set straddling the ring
	 * wrap ([old high..127] + [young 0..k]) is processed young-first: the
	 * stack sees a hole it never lost, emits duplicate ACKs and the peer's
	 * fast retransmits collapse the downlink (wire-measured 2026-10-19:
	 * 15-57% duplicate ACKs, 13.9k spurious retransmits, 556 stalls
	 * resolved with no retransmit - delivery-order inversion). Starting
	 * the same FULL pass at the persistent cursor instead delivers in
	 * arrival order, wrap included, while keeping the exact probe pattern
	 * of the default (all COUNT slots per pass) - the early-stop variants
	 * (SCAN=2/4/5) all parked this DMA (MMC FIFO overflow, frames lost to
	 * retransmit-filled holes), so the full pass is the part proven safe.
	 * The publish keeps the default's value class: the highest index
	 * processed (max_armed). */
	i = (int)dev->rx_scan;
	left = DWC_EQOS_RX_DESC_COUNT;
#elif defined(CONFIG_GMAC_RX_SCAN) && (CONFIG_GMAC_RX_SCAN == 5)
	i = (int)dev->rx_scan;
	left = DWC_EQOS_RX_DESC_COUNT;
#elif defined(CONFIG_GMAC_RX_SCAN) && (CONFIG_GMAC_RX_SCAN == 2)
	i = (int)dev->rx_scan;
	left = DWC_EQOS_RX_DESC_COUNT;
#else
	i = 0;
	left = DWC_EQOS_RX_DESC_COUNT;
#endif
	dev->sw_sweeps++;
	dev->sw_clear_seen = 0;
#if defined(CONFIG_GMAC_SWEEP_WIDE) && CONFIG_GMAC_SWEEP_WIDE
	{
		int n;
		int k = i;	/* same start as the walk, so the two levers combine */

		for (n = 0; n < left; n++, k = (k + 1) % DWC_EQOS_RX_DESC_COUNT) {
			own[k] = dev->rx_ring[k].des3;
		}
	}
#endif
	for (; left > 0; left--, i = (i + 1) % DWC_EQOS_RX_DESC_COUNT) {
		struct dwc_eqos_desc *desc;
		uint8_t *buf;
		unsigned int len;
		uint32_t d3;
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
		unsigned int slot_now = dev->rx_desc_slot[i];
#else
		const unsigned int slot_now = (unsigned int)i;
#endif

		eqos_prof[port].rx_walk_slots++;
		if (dev->rx_in_flight[i] != 1) {
			continue;
		}
		desc = &dev->rx_ring[i];
		/* No invalidate: non-cacheable ring, the read hits memory. */
#if defined(CONFIG_GMAC_SWEEP_WIDE) && CONFIG_GMAC_SWEEP_WIDE
		d3 = own[i];
#else
		d3 = desc->des3;
#endif
		if (d3 & DWC_EQOS_DESC_OWN) {
#if defined(CONFIG_GMAC_RX_SCAN) && ((CONFIG_GMAC_RX_SCAN == 2) || (CONFIG_GMAC_RX_SCAN == 4) || (CONFIG_GMAC_RX_SCAN == 5))
			/* Still the DMA's: not written back yet, and it writes
			 * back in ring order, so nothing past here is complete
			 * either. Sweep done. */
			stop_slot = i;
			break;
#else
			/* Scan modes 1 and 3 probe a fixed range: an owned
			 * descriptor inside it is simply not ours yet. */
			continue;
#endif
		}
		dev->sw_clear_seen++;

		dev->rx_in_flight[i] = 2;
		buf = rx_buffers[dev->plat->index][slot_now];
		len = d3 & DWC_EQOS_DESC_RX_LEN_MASK;

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
					   /* IPC verdict first (descriptor reads plus, on
					    * the error path only, one cache line for the
					    * protocol byte). See eqos_rx_ipc_ok. */
					   eqos_rx_ipc_ok(desc, d3, buf)
#else
					   1
#endif
				) {
					uint64_t t_cache = eqos_prof_t0();

					cache_dcache_invalidate((uintptr_t)buf, len);
					eqos_prof[port].irq_rx_cache_ticks +=
						eqos_prof_t0() - t_cache;
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
					{
						uint64_t t_copy = eqos_prof_t0();

						if (eqos_stage_put(dev, buf, len) == 0) {
							delivered++;
						}
						eqos_prof[port].irq_rx_copy_ticks +=
							eqos_prof_t0() - t_copy;
					}
#endif
				} else {
					dev->stats.rx_csum_drops++;
				}

				{
					uint64_t t_rearm = eqos_prof_t0();

					eqos_setup_rx_desc(dev, i, rearm);
					eqos_prof[port].irq_rx_cache_ticks +=
						eqos_prof_t0() - t_rearm;
				}
			}
		dev->rx_in_flight[i] = 1;
		eqos_prof[port].rx_frames++;
		last_armed = i;
		if (i > max_armed) {
			max_armed = i;
		}
	}
	eqos_prof[port].rx_walk_ticks += eqos_prof_t0() - t_walk;

	dev->rx_last_armed = last_armed;
	dev->sw_last_armed = last_armed;
	if (dev->sw_clear_seen > dev->sw_max_clear) {
		dev->sw_max_clear = dev->sw_clear_seen;
	}
	if (last_armed >= 0) {
		/* The bounded walk resumes one past the last slot it re-armed. */
		dev->rx_scan =
			(unsigned int)(last_armed + 1) % DWC_EQOS_RX_DESC_COUNT;
	}

	/* Advance the RX tail: the DMA's fetch window is [base, tail) and it
	 * STOPS when it runs into the tail - a tail fixed at ring[LAST] gave
	 * a hard 127-frames-per-watchdog-poke ceiling (1270 frames/s at the
	 * 100 ms watchdog cadence, 95% of a UDP flood dropped to FIFO
	 * overflow, N7 R2b board-measured). The Linux driver's RX_END is
	 * moving for the same reason.
	 *
	 * WHICH SLOT IS PUBLISHED IS THE WHOLE BALLGAME (2026-10-17). The
	 * healthy whole-ring scan publishes the highest index it re-armed, which
	 * for a full sweep is by construction the global frontier - everything
	 * below it has been re-armed, so the channel is always granted the right
	 * to walk on. A PARTIAL sweep has no such property: its last re-armed
	 * index can sit BEHIND the descriptor the DMA is already parked on, and
	 * writing it retracts the grant. The channel then only moves on the
	 * crumbs the 100 ms watchdog leaves (it pokes ring[LAST]), which is
	 * exactly the 2 Mbit/s the first bounded walk measured, with the ring
	 * snapshot showing the tell: cursor 12 / last re-armed 11 while the DMA
	 * sat at 17.
	 *
	 * SCAN=4 therefore publishes the STOP POINT - the first descriptor the
	 * DMA still owns - because everything before it was processed and
	 * re-armed, which makes it the frontier by the same argument the full
	 * sweep uses. Probes stay O(frames); the grant never lags.
	 *
	 * The whole-ring scan keeps its last-re-armed landing point: that is
	 * what every working measurement was taken with. */
#if defined(CONFIG_GMAC_RX_SCAN) && (CONFIG_GMAC_RX_SCAN == 6)
	if (max_armed >= 0) {
		/* Same value class as the default's last_armed (the highest
		 * index processed in an index-ordered full pass): the cursor
		 * changes the ORDER of processing, not the publish. */
		eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR,
			(uint32_t)(uintptr_t)&dev->rx_ring[max_armed]);
	}
#elif defined(CONFIG_GMAC_RX_SCAN) && (CONFIG_GMAC_RX_SCAN == 5)
	if (delivered > 0) {
		eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR,
			(uint32_t)(uintptr_t)&dev->rx_ring[DWC_EQOS_RX_DESC_COUNT - 1]);
	}
#elif defined(CONFIG_GMAC_RX_SCAN) && (CONFIG_GMAC_RX_SCAN == 4)
	if (stop_slot >= 0) {
		eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR,
			(uint32_t)(uintptr_t)&dev->rx_ring[stop_slot]);
	} else if (last_armed >= 0) {
		eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR,
			(uint32_t)(uintptr_t)&dev->rx_ring[last_armed]);
	}
#else
	if (last_armed >= 0) {
		eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR,
			(uint32_t)(uintptr_t)&dev->rx_ring[last_armed]);
	}
#endif

#if defined(__aarch64__)
	__atomic_clear(&dev->rx_sweep_busy, __ATOMIC_RELEASE);
#endif
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

/* Runtime RX-watchdog knob: RIWT is the coalescing batch size (unit 256 DMA
 * clock ticks), so a measurement round sweeps it inside one boot instead of
 * one boot per value. Only the register moves; the descriptor IOC bits stay
 * as the build set them. */
void dwc_eqos_set_rx_riwt(unsigned int index, uint32_t riwt)
{
	struct dwc_eqos_dev *dev;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return;
	}
	dev = &devs[index];
	if (!dev->powered) {
		return;
	}
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_WATCHDOG, riwt);
}

uint32_t dwc_eqos_get_rx_riwt(unsigned int index)
{
	if (index >= DWC_EQOS_PORT_COUNT) {
		return 0u;
	}
	if (!devs[index].powered) {
		return 0u;
	}
	return eqos_rd(&devs[index], DWC_EQOS_DMA_CH0_RX_WATCHDOG);
}

/* Interrupt-shape runtime seams (2026-10-19 census round; the header carries
 * the contract). The short settling note for all three: the RX IOC flip and
 * the TX completion interval are arming choices taken per descriptor, so a
 * change lands over one ring turnover of traffic rather than instantly. */
void dwc_eqos_set_irq_oddmode(unsigned int index, unsigned int mode)
{
	if (index >= DWC_EQOS_PORT_COUNT) {
		return;
	}
	if (!devs[index].powered) {
		return;
	}
	devs[index].irq_oddmode = (mode != 0u) ? 1u : 0u;
}

unsigned int dwc_eqos_get_irq_oddmode(unsigned int index)
{
	if (index >= DWC_EQOS_PORT_COUNT) {
		return 0u;
	}
	return (unsigned int)devs[index].irq_oddmode;
}

void dwc_eqos_set_rx_coalesce(unsigned int index, uint32_t riwt)
{
	struct dwc_eqos_dev *dev;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return;
	}
	dev = &devs[index];
	if (!dev->powered) {
		return;
	}
	dev->rx_ioc_off = (riwt != 0u) ? 1u : 0u;
	eqos_wr(dev, DWC_EQOS_DMA_CH0_RX_WATCHDOG, riwt);
}

void dwc_eqos_set_tx_ioc_div(unsigned int index, unsigned int div)
{
	if (index >= DWC_EQOS_PORT_COUNT) {
		return;
	}
	if (!devs[index].powered) {
		return;
	}
	devs[index].tx_ioc_div = (div != 0u) ? div : 1u;
}

unsigned int dwc_eqos_get_tx_ioc_div(unsigned int index)
{
	if (index >= DWC_EQOS_PORT_COUNT) {
		return 0u;
	}
	return devs[index].tx_ioc_div;
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
#if defined(CONFIG_GMAC_RX_NAPI) && CONFIG_GMAC_RX_NAPI
	/* Liveness heal: never leave the RX interrupt masked across a watchdog
	 * tick. Under saturation the drain re-masks it immediately, so the cost
	 * is one interrupt window per 100 ms; if the RX thread ever stalls, this
	 * is what keeps the ring moving. */
	if (dev->rx_masked) {
		eqos_wr(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE,
			eqos_rd(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE)
			| DWC_EQOS_CH0_INTR_RIE);
		dev->rx_masked = 0;
	}
#endif
	eqos_unlock(dev);

	/* N7 R1 fix: the ISR only notifies when IT moved frames; frames the
	 * watchdog itself staged went unannounced, so the RX thread slept on
	 * holding staged traffic until some future interrupt (the millisecond
	 * sawtooth the first iperf rounds showed). Same contract as dwc_eqos_irq. */
	if ((rx_frames > 0) && (s_rx_notify != NULL)) {
		s_rx_notify(index);
	}
}

#if defined(CONFIG_GMAC_RX_NAPI) && CONFIG_GMAC_RX_NAPI
int dwc_eqos_rx_poll(unsigned int index, int budget)
{
	struct dwc_eqos_dev *dev;
	int delivered = 0;

	if ((index >= DWC_EQOS_PORT_COUNT) || (budget <= 0)) {
		return 0;
	}
	dev = &devs[index];
	if (!dev->started) {
		return 0;
	}

	eqos_lock(dev);
	while (budget > 0) {
		int n = eqos_rx_complete(dev);

		if (n <= 0) {
			/* An empty sweep is the drained signal: the bounded walk
			 * stopped on the first descriptor the DMA still owns. */
			break;
		}
		delivered += n;
		budget -= n;
	}
	/* Hand the ring back to the interrupt in BOTH exits: drained means the
	 * next completion raises RI, and budget-exhausted means RI is already
	 * pending (latched level), so re-enabling the bit makes the line fire
	 * again immediately and this thread returns for the next batch - the
	 * NAPI "poll again" shape, riding the interrupt line itself. The first
	 * version re-enabled only on the drained exit and parked the RX path
	 * until the 100 ms liveness heal whenever a budget ran out under load,
	 * which is the 2026-10-17 NAPI arm's ovf=3338. */
	eqos_wr(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE,
		eqos_rd(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE)
		| DWC_EQOS_CH0_INTR_RIE);
	dev->rx_masked = 0;
	eqos_unlock(dev);

	return delivered;
}
#endif

/* Combined MAC/DMA interrupt. Registered through gicv3 with the instance
 * index as the handler argument. The NO_SYS image has no event callback: the
 * frames land in the staging ring and the main loop finds them there. */
void dwc_eqos_irq(void *arg)
{
	struct dwc_eqos_dev *dev = &devs[(unsigned int)(uintptr_t)arg];
	unsigned int port = dev->plat->index;
	uint64_t t_irq = eqos_prof_t0();
	unsigned int cls = 0u;	/* entry class, from the first non-zero word:
				 * 0 none, 1 rx (RI|RWT), 2 tx (TI), 3 odd
				 * (no serviced bit), 4 fatal bus error. The
				 * census splits the whole-ISR bucket on it. */
	int pass;

	dev->irq_n++;
	eqos_prof[port].irq_entries++;
	for (pass = 0; pass < 8; pass++) {
		uint32_t status = eqos_rd(dev, DWC_EQOS_DMA_CH0_STATUS);

		eqos_prof[port].irq_passes++;
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
			eqos_prof[port].irq_pass_zero++;
			if (eqos_rd(dev, DWC_EQOS_MAC_INTERRUPT_STATUS) != 0u) {
				eqos_prof[port].irq_pass_mac++;
				dev->irq_odd++;
			}
			break;
		}
		/* Count the ring-full drop before clearing the latch: RU says
		 * the DMA had a frame and no owned descriptor to put it in, so
		 * the frame is gone and no MMC counter recorded it (it never
		 * sat in the MTL FIFO). This is the number that makes a deep
		 * ring look good while it is really moving the loss to a path
		 * the ledger could not see. */
		if (status & DWC_EQOS_CH0_STATUS_RU) {
			dev->stats.rx_buf_unavail++;
		}
		/* Write-one-to-clear everything latched: leaving e.g. the
		 * abnormal summary set keeps the line asserted forever. */
		eqos_wr(dev, DWC_EQOS_DMA_CH0_STATUS, status);
		dev->last_status = status;

		if (!(status & (DWC_EQOS_CH0_STATUS_RI | DWC_EQOS_CH0_STATUS_RWT
				| DWC_EQOS_CH0_STATUS_TI
				| DWC_EQOS_CH0_STATUS_FB))) {
			/* An "odd pass": the status word carried only bits this
			 * driver does not service. Two very different animals
			 * live here:
			 *  - TBU/ETI-only words are ordinary traffic
			 *    conditions (TBU re-latches whenever the TX ring is
			 *    drained - it is what the W1C-plus-reread of this
			 *    very loop keeps meeting). Linux consumes the same
			 *    word as service hints (dwmac4_lib.c), and at the
			 *    measured ~1.9 us per entry this pass is pure
			 *    overhead on every IRQ. irq_oddmode=1 retires it to
			 *    a benign counter.
			 *  - anything else (unnamed bits, summary bits with no
			 *    cause) is the N7-stab signature: the embox era on
			 *    this same IP died this way - a re-latching
			 *    unserviced bit is a level-line interrupt storm, and
			 *    while it storms, nothing at or below GMAC priority
			 *    (tick included) ever runs again. Count it, print
			 *    the first occurrence (the lock-free emergency
			 *    outlet - a storm must not die silently), and bound
			 *    the damage with a burst gate. */
			if ((dev->irq_oddmode != 0u) &&
			    ((status & ~DWC_EQOS_CH0_STATUS_KNOWN_ODD) == 0u)) {
				eqos_prof[port].irq_benign++;
				if (cls == 0u) {
					cls = 3u;
				}
				break;
			}
			uint64_t t_odd = eqos_prof_t0();

			eqos_prof[port].irq_pass_odd++;

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
			uint32_t now = osKernelGetTickCount();

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
			if (cls == 0u) {
				cls = 3u;
			}
			eqos_prof[port].irq_odd_ticks += eqos_prof_t0() - t_odd;
			break;
		}

		if (status & DWC_EQOS_CH0_STATUS_FB) {
			/* Fatal bus error: shut the channel down. No sleeping
			 * is allowed here, so no full re-initialization. */
			dev->stats.dma_errors++;
			LOG_INFO("%s: dma fatal bus error, channel stopped\n",
				  eqos_dev_name(dev));
			eqos_hw_stop(dev);
			cls = 4u;
			break;
		}

		if (status & (DWC_EQOS_CH0_STATUS_RI | DWC_EQOS_CH0_STATUS_RWT)) {
			uint64_t t_rx = eqos_prof_t0();

			eqos_prof[port].irq_pass_rx++;
			if (cls == 0u) {
				cls = 1u;
			}

			/* RWT (watchdog expiry) is the coalesced RI: with the
			 * RX descriptor IOC off it is the only RX interrupt
			 * source. Counted here, not at entry, so TX-only
			 * passes do not dilute the coalescing gauge. */
			dev->stats.rx_irqs++;
#if defined(CONFIG_GMAC_RX_NAPI) && CONFIG_GMAC_RX_NAPI
			/* NAPI shape (2026-10-17): mask the RX interrupt ONLY. TI and
			 * NIE stay enabled so TX completion keeps being reclaimed
			 * here - RXPOLL zeroed the whole enable register, the TX
			 * reclaim died with it and cost 40350 dropped frames. The RX
			 * thread drains under a budget and re-enables RI when the
			 * ring comes up empty (dwc_eqos_rx_poll). */
			if (!dev->rx_masked) {
				uint32_t ien = eqos_rd(dev,
					DWC_EQOS_DMA_CH0_INTR_ENABLE);

				eqos_wr(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE,
					ien & ~DWC_EQOS_CH0_INTR_RIE);
				dev->rx_masked = 1;
			}
			if (s_rx_notify != NULL) {
				s_rx_notify((unsigned int)(uintptr_t)arg);
			}
#elif DWC_EQOS_RX_BH
			/* Bottom half: signal only, the RX thread runs the sweep
			 * (dwc_eqos_watchdog -> eqos_rx_complete). */
			if (s_rx_notify != NULL) {
				s_rx_notify((unsigned int)(uintptr_t)arg);
			}
#else
			if (eqos_rx_complete(dev) > 0) {
				if (s_rx_notify != NULL) {
					s_rx_notify((unsigned int)(uintptr_t)arg);
				}
			}
#endif
			eqos_prof[port].irq_rx_sweep_ticks += eqos_prof_t0() - t_rx;
		}
		if (status & DWC_EQOS_CH0_STATUS_TI) {
			uint64_t t_tx = eqos_prof_t0();

			eqos_prof[port].irq_pass_tx++;
			if (cls == 0u) {
				cls = 2u;
			}
			(void)eqos_tx_complete(dev);
			eqos_prof[port].irq_tx_done_ticks += eqos_prof_t0() - t_tx;
		}
	}

	{
		uint64_t dt = eqos_prof_t0() - t_irq;

		eqos_prof[port].irq_ticks += dt;
		if (dt > eqos_prof[port].irq_max_ticks) {
			eqos_prof[port].irq_max_ticks = dt;
		}
		/* The census: the entry's FULL cost charged to the class its
		 * first non-zero status word named (0 = empty word all the way
		 * out), so the odd class carries its own GIC entry and status
		 * reads instead of only the bracket inside the branch. */
		switch (cls) {
		case 1u:
			eqos_prof[port].irq_ent_rx++;
			eqos_prof[port].irq_ent_rx_ticks += dt;
			break;
		case 2u:
			eqos_prof[port].irq_ent_tx++;
			eqos_prof[port].irq_ent_tx_ticks += dt;
			break;
		case 3u:
			eqos_prof[port].irq_ent_odd++;
			eqos_prof[port].irq_ent_odd_ticks += dt;
			break;
		case 4u:
			eqos_prof[port].irq_ent_fb++;
			eqos_prof[port].irq_ent_fb_ticks += dt;
			break;
		default:
			eqos_prof[port].irq_ent_none++;
			eqos_prof[port].irq_ent_none_ticks += dt;
			break;
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
	/* Interrupt-shape defaults from the build macros; the runtime seams
	 * (gmac oddmode/rxcoal/txdiv) move them without a rebuild. */
	dev->tx_ioc_div = DWC_EQOS_TX_IOC_DIV;
#if defined(CONFIG_GMAC_RX_COALESCE) && CONFIG_GMAC_RX_COALESCE
	dev->rx_ioc_off = 1u;
#endif

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
		LOG_INFO("%s: mac version 0x%08x before soc init\n",
			  eqos_dev_name(dev),
			  (unsigned int)eqos_rd(dev, DWC_EQOS_MAC_VERSION));

		/* Clocks, iomux, RGMII delays, then the PHY hard reset pulse
		 * (which re-latches the PHY's straps - the PHY driver clears
		 * the strapped delay right after this returns). */
		if (dev->plat->soc_init(dev->plat) != 0) {
			return EQOS_ERROR;
		}

		LOG_INFO("%s: mac version 0x%08x at 0x%08x irq %u\n",
			  eqos_dev_name(dev),
			  (unsigned int)eqos_rd(dev, DWC_EQOS_MAC_VERSION),
			  (unsigned int)dev->plat->base_addr,
			  (unsigned int)dev->plat->irq_num);

		if (eqos_hw_init(dev) != EQOS_OK) {
			return EQOS_ERROR;
		}

		/* The gicv3 registration carries the instance index in the
		 * handler argument; level mode and the line's priority class,
		 * then enable (the same arming shape as every other SPI line
		 * in this image).
		 *
		 * EQOS_BOARD_IRQ_PRIORITY_RAW: the handler releases the RX
		 * semaphore from the interrupt, so on a kernel that forbids
		 * API calls from interrupts more urgent than its ceiling
		 * (FreeRTOS: configMAX_API_CALL_INTERRUPT_PRIORITY) the line
		 * has to sit at that ceiling - the same seam, and the same
		 * raw value, as the USB controller line
		 * (ports/cherryusb/board/rk3568-itx/usb_glue.c:82-83). */
		(void)irq_register(dev->plat->irq_num, dwc_eqos_irq,
				   (void *)(uintptr_t)index);
		(void)irq_set_mode_level(dev->plat->irq_num);
		(void)irq_set_priority(dev->plat->irq_num,
				       EQOS_BOARD_IRQ_PRIORITY_RAW);
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

#if defined(CONFIG_GMAC_TX_CSUM) && CONFIG_GMAC_TX_CSUM
/* Checksum offload preparation for one frame. Returns 1 when the frame is
 * now ready for CIC=3 (the L4 checksum field holds S(pseudo-header) - the
 * plain sum - and the MAC inserts the final checksums), 0 to leave the frame
 * alone.
 *
 * The contract, and how it was pinned down:
 *  - lwIP has no TX-offload API. The one thing it can do, with
 *    LWIP_CHECKSUM_CTRL_PER_NETIF, is stop generating the L4 checksum for
 *    this netif, which leaves the field zeroed (tcp_out.c:1563,
 *    udp.c:788). Filling it back in is this function's job.
 *  - The field content and the tag position both come from the production
 *    code that drives this same core on this same board: Linux writes
 *    `th->check = ~tcp_v4_check(len, saddr, daddr, 0)` (include/net/tcp.h:
 *    2447), which is the plain pseudo sum after the double negation
 *    (csum_fold complements, so tcp_v4_check and the caller's ~ cancel),
 *    and tags CIC=3 in TDES3[17:16] (stmmac_main.c:4851,
 *    dwmac4_descs.c:321). The first board attempt wrote the same field
 *    content but put the tag in TDES2, where [17:16] is not the insertion
 *    field: insertion never engaged, every outbound TCP frame (data and
 *    ACKs) carried the bare seed as its checksum and was rejected - uplink
 *    aborted locally after 9.7 s, downlink stalled behind the bad ACKs.
 *    Position and content are two separate traps on this path.
 *  - With CIC=3 the MAC also computes the IP header checksum, so the IP
 *    header field needs no software help; the software's whole job is the
 *    12-byte L4 pseudo-header sum above.
 *
 * The shape rule stays narrow: Ethernet II + IPv4 + TCP/UDP, the header
 * without options, unfragmented, and the field still zero (GEN switched off
 * for this netif). Anything else goes out untouched; a zero-checksum TCP
 * frame that fails the shape rule is counted, because it would leave broken
 * (TCP cannot encode "no checksum") - acceptance reads that counter as 0.
 * ICMP is deliberately out: no pseudo-header in the protocol, so the MAC's
 * CIC=3 computation would produce a wrong one (Linux leaves ICMP to software
 * for the same reason). */
#define EQOS_TXCSUM_LEAVE		0
#define EQOS_TXCSUM_OFFLOAD		1
#define EQOS_TXCSUM_TCP_UNCHECKED	2

static uint16_t eqos_txcsum_add(uint32_t acc, uint16_t word)
{
	acc += word;
	/* End-around carry, twice: the first fold can itself produce 0x10000
	 * (0xffff + 1), which one more fold maps to 0x0001 instead of
	 * truncating to 0. */
	acc = (acc & 0xffffu) + (acc >> 16);
	acc = (acc & 0xffffu) + (acc >> 16);
	return (uint16_t)acc;
}

static int eqos_tx_csum_prepare(uint8_t *frame, unsigned int len)
{
	unsigned int ip_off = 14u;
	unsigned int ihl;
	unsigned int l4_off;
	unsigned int csum_off;
	unsigned int l4_len;
	uint32_t acc = 0u;
	uint16_t csum;
	uint8_t proto;
	uint16_t frag;
	uint16_t ip_total;
	unsigned int i;
	int is_tcp;

	if (len < 34u) {
		return EQOS_TXCSUM_LEAVE;
	}
	if ((frame[12] != 0x08u) || (frame[13] != 0x00u)) {
		return EQOS_TXCSUM_LEAVE;	/* not IPv4 (EtherType 0x0800) */
	}
	ihl = (unsigned int)(frame[ip_off] & 0x0fu) * 4u;
	if (((frame[ip_off] >> 4) != 4u) || (ihl < 20u) || (len < ip_off + ihl)) {
		return EQOS_TXCSUM_LEAVE;	/* version mismatch or a bad IHL */
	}
	proto = frame[ip_off + 9];
	if ((proto != 6u) && (proto != 17u)) {
		return EQOS_TXCSUM_LEAVE;	/* not TCP/UDP (ARP, ICMP, ...) */
	}
	is_tcp = (proto == 6u);
	ip_total = (uint16_t)(((uint16_t)frame[ip_off + 2] << 8)
			      | frame[ip_off + 3]);
	l4_off = ip_off + ihl;
	if ((ip_total < ihl + (is_tcp ? 20u : 8u))
	    || (l4_off + (is_tcp ? 20u : 8u) > len)) {
		return is_tcp ? EQOS_TXCSUM_TCP_UNCHECKED : EQOS_TXCSUM_LEAVE;
	}
	l4_len = (unsigned int)ip_total - ihl;
	csum_off = l4_off + (is_tcp ? 16u : 6u);
	csum = (uint16_t)(((uint16_t)frame[csum_off] << 8)
			  | frame[csum_off + 1]);
	if (csum != 0u) {
		/* Filled in by the stack for this frame: a correct checksum,
		 * leave the bytes alone. */
		return EQOS_TXCSUM_LEAVE;
	}
	frag = (uint16_t)(((uint16_t)frame[ip_off + 6] << 8)
			  | frame[ip_off + 7]);
	if ((ihl != 20u) || ((frag & 0x3fffu) != 0u)) {
		/* A zero L4 checksum and a shape the MAC's byte range cannot be
		 * trusted to match: leave it. TCP cannot encode "no checksum",
		 * so this frame would leave broken - counted for acceptance.
		 * UDP's zero is legal and stays legal. */
		return is_tcp ? EQOS_TXCSUM_TCP_UNCHECKED : EQOS_TXCSUM_LEAVE;
	}
	/* The field takes S(pseudo) - the plain one's-complement sum of the six
	 * pseudo-header words (src/dst, [zero|proto], length) - derived from the
	 * production code that drives this same core on this same board:
	 * include/net/tcp.h:2447 `th->check = ~tcp_v4_check(len, saddr, daddr,
	 * 0)`, and tcp_v4_check returns the FOLDED-AND-COMPLEMENTED sum
	 * (csum_fold returns ~sum, asm-generic/checksum.h), so the double
	 * negation lands on the plain sum. That is the classic offload seed:
	 * the MAC adds the payload and inserts the complement, under the
	 * "engine keeps the field as a base" reading; under the "engine
	 * computes the pseudo itself" reading the value is ignored. Either way
	 * S(pseudo) is the compatible content.
	 *
	 * The 2026-10-18 falsification was NOT here: the first arm wrote this
	 * same S(pseudo) but tagged CIC into TDES2, where bits [17:16] are not
	 * the insertion field on this layout - insertion never engaged, the
	 * seed went out as the frame's "checksum", and every outbound TCP
	 * frame (data and ACKs alike) was rejected, which killed the uplink
	 * (lwiperf aborted-local after 9.7 s) and the downlink with it. */
	for (i = 0u; i < 8u; i += 2u) {
		acc = eqos_txcsum_add(acc,
			(uint16_t)(((uint16_t)frame[ip_off + 12u + i] << 8)
				   | frame[ip_off + 13u + i]));
	}
	acc = eqos_txcsum_add(acc, (uint16_t)proto);
	acc = eqos_txcsum_add(acc, (uint16_t)l4_len);
	csum = (uint16_t)acc;
	if (csum == 0u) {
		csum = 0xffffu;	/* -0 is 0xffff in one's complement */
	}
	frame[csum_off] = (uint8_t)(csum >> 8);
	frame[csum_off + 1] = (uint8_t)(csum & 0xffu);
	return EQOS_TXCSUM_OFFLOAD;
}
#endif

int32_t dwc_eqos_send_frame(unsigned int index, const uint8_t *frame, uint32_t len)
{
	struct dwc_eqos_dev *dev;
	unsigned int tries;
	uint64_t t_tx;

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
	t_tx = eqos_prof_t0();

	for (tries = 0; tries <= DWC_EQOS_TX_WAIT_MS; tries++) {
		struct dwc_eqos_desc *desc;
		unsigned int tx_len;
		unsigned int ioc;
		uint32_t ioc_cic = 0u;
		uint8_t *buf;
		int cur;
		int next;
		uint64_t t_copy;

		eqos_lock(dev);
		cur = dev->tx_head;
		next = (cur + 1) % DWC_EQOS_TX_DESC_COUNT;
		if (next != dev->tx_tail) {
			buf = tx_buffers[index][cur];
			t_copy = eqos_prof_t0();
			memcpy(buf, frame, len);
			eqos_prof[index].tx_memcpy_ticks += eqos_prof_t0() - t_copy;

			/* Ethernet requires 60 bytes before the FCS: a
			 * shorter frame (a 42-byte ARP reply, for one) is
			 * discarded as a runt by every receiver, silently. */
			tx_len = len;
			if (tx_len < DWC_EQOS_MIN_TX_FRAME) {
				memset(buf + len, 0, DWC_EQOS_MIN_TX_FRAME - len);
				tx_len = DWC_EQOS_MIN_TX_FRAME;
			}

			/* One completion interrupt per IOC_DIV frames, plus
			 * the frame that empties the ring (cur == tx_tail
			 * means this is the only in-flight frame): a quiet
			 * line must never wait on the counter for its
			 * completion, a burst must not pay one IRQ per
			 * frame (N7 R2). */
			ioc = ((dev->tx_ioc_seq++ % dev->tx_ioc_div)
			       == (dev->tx_ioc_div - 1u)) ||
			      (cur == dev->tx_tail);

			desc = &dev->tx_ring[cur];
			desc->des0 = (uint32_t)(uintptr_t)buf;
			desc->des1 = 0;
			/* On this core the transmit descriptor carries the frame
			 * length in des3 (as the rx writeback does) and the
			 * completion-interrupt enable in des2 bit 31. No flush:
			 * non-cacheable ring, the OWN grant is visible to the
			 * DMA before the end pointer below moves.
			 *
			 * CONFIG_GMAC_TX_CSUM = checksum insertion offload: tag
			 * IPv4/TCP and IPv4/UDP frames with CIC=3 so the MAC
			 * computes and inserts both checksums (TRM Part2 ch.20
			 * TDES3[17:16]; Linux programs the same value, descs.h:179).
			 * The stack side is the other half of the same switch: lwIP
			 * leaves the checksum fields zero on those frames (per-netif
			 * NETIF_CHECKSUM_GEN_* off) and eqos_tx_csum_prepare seeds
			 * them with the pseudo-header sum; the MAC inserts the
			 * finals. */
#if defined(CONFIG_GMAC_TX_CSUM) && CONFIG_GMAC_TX_CSUM
			if (dev->tx_csum_cap != 0u) {
				switch (eqos_tx_csum_prepare(buf, tx_len)) {
				case EQOS_TXCSUM_OFFLOAD:
					ioc_cic = DWC_EQOS_DESC_TX_CIC_FULL;
					dev->stats.tx_csum_frames++;
					break;
				case EQOS_TXCSUM_TCP_UNCHECKED:
					/* A zero-checksum TCP frame the classifier can't
					 * vouch for: count it (arm acceptance reads zero).
					 * CIC stays 0 and the frame goes out as the stack
					 * wrote it. */
					dev->stats.tx_csum_uncovered++;
					break;
				default:
					break;
				}
			}
#endif
			/* The clean must come AFTER the checksum classifier: the
			 * seeded pseudo-header sum is a write into this frame, and
			 * tx_buffers live in cacheable .bss (no DMA snooping), so a
			 * clean taken before it would leave those bytes to reach
			 * memory by accident. Flush covers the final bytes the DMA
			 * reads, seed included. */
			{
				uint64_t t_flush = eqos_prof_t0();

				cache_dcache_flush((uintptr_t)buf, tx_len);
				eqos_prof[index].tx_flush_ticks +=
					eqos_prof_t0() - t_flush;
			}
			desc->des2 = (ioc ? DWC_EQOS_DESC_TX_IOC : 0u)
				   | (tx_len & 0x3fffu);
			desc->des3 = DWC_EQOS_DESC_OWN | DWC_EQOS_DESC_TX_FD
				   | DWC_EQOS_DESC_TX_LD | ioc_cic | tx_len;
			/* Grant reaches memory before the end pointer moves. */
			eqos_dsb_st();

			dev->tx_head = next;

			/* Occupancy after this enqueue: how deep the ring ran is
			 * the TX ladder's first discriminator (a ring that never
			 * gets near its depth is not the limiter). */
			{
				int occ = dev->tx_head - dev->tx_tail;

				if (occ < 0) {
					occ += DWC_EQOS_TX_DESC_COUNT;
				}
				if ((uint32_t)occ > dev->tx_occ_max) {
					dev->tx_occ_max = (uint32_t)occ;
				}
			}

			/* Kick the DMA: the fetch window is [base, END), so
			 * the end must point past the descriptor just armed. */
			eqos_wr(dev, DWC_EQOS_DMA_CH0_TX_END_ADDR,
				(uint32_t)(uintptr_t)&dev->tx_ring[dev->tx_head]);
			eqos_unlock(dev);

			dev->stats.tx_frames++;
			eqos_prof[index].tx_frames++;
			eqos_prof[index].tx_total_ticks += eqos_prof_t0() - t_tx;
			return EQOS_OK;
		}
		eqos_unlock(dev);

		/* Ring full. The completion sweep signals the semaphore as it
		 * frees slots, so the wait ends the moment space exists; the
		 * timeout is only the belt-and-braces against a lost signal
		 * (and bounds the tcpip thread's stall while the ring drains
		 * at line rate). */
		dev->tx_waits++;
#if defined(CONFIG_GMAC_TX_NONBLOCK) && CONFIG_GMAC_TX_NONBLOCK
		/* TX-ladder lever: hand the full ring straight back to the
		 * caller instead of parking the tcpip thread on the completion
		 * semaphore. lwIP retries the segment - the shape reference
		 * lwIP Ethernet ports use. */
		break;
#else
		{
			uint32_t wait_t0 = osKernelGetTickCount();

			if ((dev->tx_sem == NULL) ||
				(osSemaphoreAcquire(dev->tx_sem, DWC_EQOS_TX_WAIT_MS) != osOK)) {
				(void)osThreadYield();
			}
			dev->tx_wait_ticks += osKernelGetTickCount() - wait_t0;
		}
#endif
	}

	dev->stats.tx_dropped++;
	if (dev->stats.tx_dropped == 1) {
		LOG_INFO("%s: tx ring full, frame dropped\n", eqos_dev_name(dev));
	}

	eqos_prof[index].tx_total_ticks += eqos_prof_t0() - t_tx;
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

/* --- service-time profile (see struct dwc_eqos_prof) -----------------------
 * The counter is the 24 MHz system counter, so one tick is 41.7 ns: a bucket
 * is meaningful in aggregate (a phase is seconds long), not per event. Reads
 * are ~tens of cycles, which is why the buckets sit at operation boundaries
 * (per interrupt, per frame) and not inside loops. The storage sits with the
 * other per-port statics, above every user. */

void dwc_eqos_get_prof(unsigned int index, struct dwc_eqos_prof *prof)
{
	if ((index >= DWC_EQOS_PORT_COUNT) || (prof == NULL)) {
		return;
	}
	*prof = eqos_prof[index];
}

void dwc_eqos_reset_prof(unsigned int index)
{
	if (index >= DWC_EQOS_PORT_COUNT) {
		return;
	}
	memset(&eqos_prof[index], 0, sizeof(eqos_prof[index]));
}

void dwc_eqos_reset_rx_hist(unsigned int index)
{
	if (index >= DWC_EQOS_PORT_COUNT) {
		return;
	}
	memset(eqos_prof[index].rx_gap_hist, 0,
	       sizeof(eqos_prof[index].rx_gap_hist));
	eqos_prof[index].rx_gap_samples = 0u;
	eqos_prof[index].rx_last_stage_ticks = 0u;
	eqos_prof[index].rx_stage_high_water = 0u;
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
	diag->tx_waits = dev->tx_waits;
	diag->tx_wait_ticks = dev->tx_wait_ticks;
	diag->tx_occ_max = dev->tx_occ_max;
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

/* Map a DMA ring address back to its slot index; -1 when it is outside the
 * ring (the controller reports the descriptor it is ABOUT to fetch as well,
 * which is one past the last one and lands here). */
static int eqos_ring_slot(const struct dwc_eqos_dev *dev, uint32_t addr)
{
	uintptr_t base = (uintptr_t)&dev->rx_ring[0];
	uintptr_t a = (uintptr_t)addr;

	if ((a < base) ||
	    (a >= base + (DWC_EQOS_RX_DESC_COUNT * sizeof(struct dwc_eqos_desc)))) {
		return -1;
	}
	return (int)((a - base) / sizeof(struct dwc_eqos_desc));
}

int dwc_eqos_get_ring(unsigned int index, struct dwc_eqos_ring *ring)
{
	struct dwc_eqos_dev *dev;
	int n;

	if ((index >= DWC_EQOS_PORT_COUNT) || (ring == NULL)) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];
	if ((dev->plat == NULL) || (dev->rx_ring == NULL)) {
		return EQOS_ERROR;
	}

	memset(ring, 0, sizeof(*ring));

	eqos_lock(dev);
	ring->rx_base = eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_BASE_ADDR_LO);
	ring->rx_end = eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR);
	ring->rx_ring_len = eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_RING_LEN);
	ring->cur_rx_desc = eqos_rd(dev, DWC_EQOS_DMA_CH0_CUR_RX_DESC);
	ring->cur_rx_buf = eqos_rd(dev, DWC_EQOS_DMA_CH0_CUR_RX_BUF_ADDR);
	ring->rx_control = eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_CONTROL);
	ring->ch0_status = eqos_rd(dev, DWC_EQOS_DMA_CH0_STATUS);
	ring->rx_scan = (int32_t)dev->rx_scan;
	ring->last_armed = (int32_t)dev->rx_last_armed;
	ring->cur_desc_slot = eqos_ring_slot(dev, ring->cur_rx_desc);
	ring->end_slot = eqos_ring_slot(dev, ring->rx_end);
	if ((ring->cur_desc_slot >= 0) && (ring->end_slot >= 0)) {
		ring->end_gap = (int32_t)(((unsigned int)ring->cur_desc_slot
					   - (unsigned int)ring->end_slot
					   + DWC_EQOS_RX_DESC_COUNT)
					  % DWC_EQOS_RX_DESC_COUNT);
	} else {
		ring->end_gap = -1;
	}
	ring->sw_sweeps = dev->sw_sweeps;
	ring->sw_clear_seen = dev->sw_clear_seen;
	ring->sw_max_clear = dev->sw_max_clear;
	ring->sw_span = dev->sw_span;
	ring->sw_curdesc = dev->sw_curdesc;
	ring->sw_last_armed = dev->sw_last_armed;

	for (n = 0; n < DWC_EQOS_RX_DESC_COUNT; n++) {
		int word = n / 32;
		uint32_t bit = 1u << (n % 32);

		if (dev->rx_in_flight[n] == 1) {
			ring->in_flight_mask[word] |= bit;
			ring->ring_armed++;
		}
		if (dev->rx_ring[n].des3 & DWC_EQOS_DESC_OWN) {
			ring->dma_owned_mask[word] |= bit;
		} else {
			ring->own_clear++;
		}
	}

	/* What the bounded walk would cover from the cursor, and whether
	 * anything the DMA has finished sits outside that run (a non-zero
	 * stale count is the walk leaking a slot). */
	for (n = 0; n < DWC_EQOS_RX_DESC_COUNT; n++) {
		int slot = (int)((dev->rx_scan + (unsigned int)n)
				 % DWC_EQOS_RX_DESC_COUNT);

		if (dev->rx_ring[slot].des3 & DWC_EQOS_DESC_OWN) {
			break;
		}
		ring->walk_run++;
	}
	if (ring->own_clear > ring->walk_run) {
		ring->stale_slots = ring->own_clear - ring->walk_run;
	}
	eqos_unlock(dev);

	return EQOS_OK;
}

/* The register snapshot the Linux-parity round diffs against the Linux lane's
 * own dump of the same offsets. Nothing here is written; this is the "what
 * did the hardware actually get told" half of every register-difference
 * claim, so it reads what the channel is running with right now (live, not
 * shadow copies of what we meant to write). */
int dwc_eqos_get_regs(unsigned int index, struct dwc_eqos_regs *regs)
{
	struct dwc_eqos_dev *dev;

	if ((index >= DWC_EQOS_PORT_COUNT) || (regs == NULL)) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &devs[index];
	if ((dev->plat == NULL) || (dev->rx_ring == NULL)) {
		return EQOS_ERROR;
	}

	memset(regs, 0, sizeof(*regs));

	eqos_lock(dev);
	regs->mac_configuration = eqos_rd(dev, DWC_EQOS_MAC_CONFIGURATION);
	regs->mac_packet_filter = eqos_rd(dev, DWC_EQOS_MAC_PACKET_FILTER);
	regs->mac_q0_tx_flow_ctrl = eqos_rd(dev, DWC_EQOS_MAC_Q0_TX_FLOW_CTRL);
	regs->mac_rx_flow_ctrl = eqos_rd(dev, DWC_EQOS_MAC_RX_FLOW_CTRL);
	regs->hw_feature0 = eqos_rd(dev, DWC_EQOS_MAC_HW_FEATURE0);
	regs->hw_feature1 = eqos_rd(dev, DWC_EQOS_MAC_HW_FEATURE1);
	regs->mtl_txq0_op = eqos_rd(dev, DWC_EQOS_MTL_TXQ0_OPERATION_MODE);
	regs->mtl_rxq0_op = eqos_rd(dev, DWC_EQOS_MTL_RXQ0_OPERATION_MODE);
	regs->dma_mode = eqos_rd(dev, DWC_EQOS_DMA_MODE);
	regs->sysbus_mode = eqos_rd(dev, DWC_EQOS_DMA_SYSBUS_MODE);
	regs->ch0_control = eqos_rd(dev, DWC_EQOS_DMA_CH0_CONTROL);
	regs->ch0_tx_control = eqos_rd(dev, DWC_EQOS_DMA_CH0_TX_CONTROL);
	regs->ch0_rx_control = eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_CONTROL);
	regs->ch0_tx_end = eqos_rd(dev, DWC_EQOS_DMA_CH0_TX_END_ADDR);
	regs->ch0_rx_end = eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_END_ADDR);
	regs->ch0_tx_ring_len = eqos_rd(dev, DWC_EQOS_DMA_CH0_TX_RING_LEN);
	regs->ch0_rx_ring_len = eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_RING_LEN);
	regs->ch0_intr_enable = eqos_rd(dev, DWC_EQOS_DMA_CH0_INTR_ENABLE);
	regs->ch0_rx_watchdog = eqos_rd(dev, DWC_EQOS_DMA_CH0_RX_WATCHDOG);
	regs->ch0_cur_rx_desc = eqos_rd(dev, DWC_EQOS_DMA_CH0_CUR_RX_DESC);
	regs->ch0_status = eqos_rd(dev, DWC_EQOS_DMA_CH0_STATUS);
	eqos_unlock(dev);

	return EQOS_OK;
}
