/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   e1000e.c
 * @brief  First-party Intel 82574L (PCI 8086:10d3) driver: reset, PHY
 *         negotiation, descriptor rings and the polled frame path.
 * @author zhugengyu
 * @date   16.10.2026
 * @design legacy-descriptor ring DMA - Linux drivers/net/ethernet/intel/
 *          e1000e/netdev.c carries the discipline this follows (arm
 *          descriptors, write the tail doorbell, clean on descriptor-done);
 *          the pbuf-shaped RX wrap follows Zephyr drivers/ethernet/
 *          eth_e1000.c. DMA coherency is the SoC's (see e1000e.h): no
 *          cache maintenance on the data path, one store barrier before
 *          each doorbell so the device never fetches a half-written
 *          descriptor.
 */

#include <string.h>

#include "arch_asm.h"
#include "generic_timer.h"
#include "log.h"
#include "regs.h"

#include "e1000e.h"
#include "e1000e_regs.h"

/* MMIO access: 32-bit at byte offsets, the discipline every peripheral
 * frame on this board uses (arch/aarch64/regs.h). */
static uint32_t e1k_rd(const struct e1000e *nic, uint32_t off)
{
	return reg_rd32(nic->base + off);
}

uint32_t e1000e_reg_rd(const struct e1000e *nic, uint32_t off)
{
	return e1k_rd(nic, off);
}

static void e1k_wr(const struct e1000e *nic, uint32_t off, uint32_t val)
{
	reg_wr32(nic->base + off, val);
}

static void e1k_set(const struct e1000e *nic, uint32_t off, uint32_t bits)
{
	e1k_wr(nic, off, e1k_rd(nic, off) | bits);
}

/* --- timed waits (the System Counter, before/without any kernel) ------------- */

static void wait_us(uint32_t us)
{
	uint32_t freq = gtimer_freq_get();
	uint64_t until = gtimer_count_get() + (uint64_t)freq * us / 1000000u;

	while ((int64_t)(until - gtimer_count_get()) > 0) {
		ARCH_ISB();
	}
}

/* True when the counter passed `deadline` (64-bit wrap-safe compare). */
static int timed_out(uint64_t deadline)
{
	return (int64_t)(gtimer_count_get() - deadline) >= 0;
}

static uint64_t deadline_from_us(uint32_t us)
{
	return gtimer_count_get() + (uint64_t)gtimer_freq_get() * us / 1000000u;
}

/* --- MDI (PHY register access through the MDIC register) ---------------------- */

static int phy_rd(const struct e1000e *nic, uint8_t reg, uint16_t *val)
{
	uint64_t until = deadline_from_us(1000u);
	uint32_t mdic;

	e1k_wr(nic, E1000_MDIC,
	       E1000_MDIC_OP_READ |
	       (E1000_MDIC_PHY_ADDR << E1000_MDIC_PHYADD_SHIFT) |
	       ((uint32_t)reg << E1000_MDIC_REGADD_SHIFT));

	do {
		mdic = e1k_rd(nic, E1000_MDIC);
		if ((mdic & E1000_MDIC_READY) != 0u) {
			if ((mdic & E1000_MDIC_ERROR_MASK) != 0u) {
				return -1;
			}
			*val = (uint16_t)(mdic & E1000_MDIC_DATA_MASK);
			return 0;
		}
	} while (!timed_out(until));

	return -1;
}

static int phy_wr(const struct e1000e *nic, uint8_t reg, uint16_t val)
{
	uint64_t until = deadline_from_us(1000u);
	uint32_t mdic;

	e1k_wr(nic, E1000_MDIC,
	       E1000_MDIC_OP_WRITE |
	       (E1000_MDIC_PHY_ADDR << E1000_MDIC_PHYADD_SHIFT) |
	       ((uint32_t)reg << E1000_MDIC_REGADD_SHIFT) |
	       val);

	do {
		mdic = e1k_rd(nic, E1000_MDIC);
		if ((mdic & E1000_MDIC_READY) != 0u) {
			return (mdic & E1000_MDIC_ERROR_MASK) != 0u ? -1 : 0;
		}
	} while (!timed_out(until));

	return -1;
}

/* --- station MAC -------------------------------------------------------------- */

/* RAR[0] carries the station address when valid; the EEPROM words 0..2 are
 * the permanent address the NIC loads RAR[0] from (82574L datasheet). */
static void mac_read(struct e1000e *nic)
{
	uint32_t rah = e1k_rd(nic, E1000_RAH);

	if ((rah & E1000_RAH_AV) != 0u) {
		uint32_t ral = e1k_rd(nic, E1000_RAL);

		memcpy(nic->mac, &ral, 4);
		memcpy(nic->mac + 4, &rah, 2);
		return;
	}

	for (unsigned int w = 0u; w < 3u; w++) {
		uint64_t until = deadline_from_us(10000u);
		uint32_t eerd;

		e1k_wr(nic, E1000_EERD,
		       E1000_EERD_START | (w << E1000_EERD_ADDR_SHIFT));
		do {
			eerd = e1k_rd(nic, E1000_EERD);
			if ((eerd & E1000_EERD_DONE) != 0u) {
				uint16_t word = (uint16_t)(eerd >> E1000_EERD_DATA_SHIFT);

				nic->mac[w * 2u] = (uint8_t)(word & 0xffu);
				nic->mac[w * 2u + 1u] = (uint8_t)(word >> 8);
				break;
			}
		} while (!timed_out(until));
	}
}

static void mac_write(const struct e1000e *nic)
{
	uint32_t ral;
	uint16_t rah;

	memcpy(&ral, nic->mac, 4);
	memcpy(&rah, nic->mac + 4, 2);

	e1k_wr(nic, E1000_RAL, ral);
	e1k_wr(nic, E1000_RAH, (uint32_t)rah | E1000_RAH_AV);
}

/* --- ring plumbing -------------------------------------------------------------- */

/* Arm one RX descriptor with a fresh pool pbuf. Returns 0, -1 when the
 * pool is exhausted (the caller keeps the old buffer for a retry). */
static int rx_arm(struct e1000e *nic, uint32_t idx)
{
	struct pbuf *p = pbuf_alloc(PBUF_RAW, E1000E_FRAME_MAX, PBUF_POOL);

	if (p == NULL) {
		return -1;
	}
	/* Ownership hand-back: the legacy descriptor has no software-own bit,
	 * so the writeback flags must be cleared by SOFTWARE before the
	 * descriptor returns to the device (the reference SDK's completion
	 * step does exactly this clear). Without it every wrapped descriptor
	 * still shows its last DD and the poll path hands the old buffer up
	 * again and again - a self-sustaining ghost-frame flood that starves
	 * every equal-priority thread (board evidence 2026-10-16: the RX
	 * counter exploded 125 -> 3.58M the moment the ring first wrapped). */
	nic->rx[idx].sta = 0u;
	nic->rx[idx].err = 0u;
	nic->rx[idx].addr = (uint64_t)(uintptr_t)p->payload;
	nic->rx_buf[idx] = p;
	return 0;
}

/* --- bring-up -------------------------------------------------------------------- */

int e1000e_init(struct e1000e *nic, uintptr_t bar0,
		uint8_t bus, uint8_t dev, uint8_t fn)
{
	uint64_t until;

	memset(nic, 0, sizeof(*nic));
	nic->base = bar0;
	nic->bus = bus;
	nic->dev = dev;
	nic->fn = fn;

	/* The station address must survive the reset: read it before, restore
	 * it after (CTRL_RST clears the RAR). */
	mac_read(nic);

	/* Device reset: the bit self-clears; the datasheet asks for a delay
	 * after it before any access. */
	e1k_set(nic, E1000_CTRL, E1000_CTRL_RST);
	until = deadline_from_us(100000u);
	while ((e1k_rd(nic, E1000_CTRL) & E1000_CTRL_RST) != 0u) {
		if (timed_out(until)) {
			LOG_ERROR("e1000e: reset did not self-clear\n");
			return -1;
		}
		ARCH_ISB();
	}
	wait_us(1000u);

	mac_write(nic);

	/* Interrupts stay masked for good: the data plane is polled (the
	 * IMS write-1-to-set discipline makes IMC_ALL the clean "none"). */
	e1k_wr(nic, E1000_IMC, E1000_IMC_ALL);
	(void)e1k_rd(nic, E1000_ICR);

	/* Force the link state machine up (the PHY negotiates next). */
	e1k_set(nic, E1000_CTRL, E1000_CTRL_SLU);

	/* RX ring: descriptors armed with pool pbufs; the enable bits of
	 * RCTL itself are e1000e_start's business. */
	for (uint32_t i = 0u; i < E1000E_RX_DESCS; i++) {
		if (rx_arm(nic, i) != 0) {
			LOG_ERROR("e1000e: rx arm %u pool exhausted\n",
				  (unsigned)i);
			return -1;
		}
	}
	e1k_wr(nic, E1000_RDBAH,
	       (uint32_t)((uint64_t)(uintptr_t)nic->rx >> 32));
	e1k_wr(nic, E1000_RDBAL,
	       (uint32_t)(uintptr_t)nic->rx);
	e1k_wr(nic, E1000_RDLEN, sizeof(nic->rx));
	e1k_wr(nic, E1000_RDH, 0u);
	e1k_wr(nic, E1000_RDT, E1000E_RX_DESCS - 1u);
	e1k_set(nic, E1000_RXDCTL, E1000_RXDCTL_ENABLE);

	/* TX ring: empty, enabled, default inter-packet gaps. */
	e1k_wr(nic, E1000_TDBAH,
	       (uint32_t)((uint64_t)(uintptr_t)nic->tx >> 32));
	e1k_wr(nic, E1000_TDBAL,
	       (uint32_t)(uintptr_t)nic->tx);
	e1k_wr(nic, E1000_TDLEN, sizeof(nic->tx));
	e1k_wr(nic, E1000_TDH, 0u);
	e1k_wr(nic, E1000_TDT, 0u);
	e1k_set(nic, E1000_TXDCTL, E1000_TXDCTL_ENABLE);
	e1k_wr(nic, E1000_TIPG,
	       E1000_TIPG_IPGT_DEF | E1000_TIPG_IPGR1_DEF | E1000_TIPG_IPGR2_DEF);

	LOG_INFO("e1000e: %02x:%02x.%x bar0=0x%08lx mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		 bus, dev, fn, (unsigned long)bar0,
		 nic->mac[0], nic->mac[1], nic->mac[2],
		 nic->mac[3], nic->mac[4], nic->mac[5]);

	return 0;
}

/* Wait for the PHY to answer plausibly before its first access. The MAC
 * reset (CTRL.RST) also resets the PHY; a freshly powered PHY keeps
 * answering MDIC with all-zero/all-ones while its reset and PLL settle
 * (cold-boot board evidence: the link never came up within the caller's
 * budget on a cold boot while the firmware had just transferred over the
 * same PHY), and a BMCR write landing in that window is lost. 200 ms is
 * the bound; a PHY that never answers plausibly is the caller's timeout
 * to report. */
static void phy_wait_ready(struct e1000e *nic)
{
	uint64_t until = deadline_from_us(200000u);
	uint16_t bmsr = 0u;

	do {
		if ((phy_rd(nic, E1000_PHY_BMSR, &bmsr) == 0) &&
		    (bmsr != 0x0000u) && (bmsr != 0xffffu)) {
			nic->phy_bmsr = bmsr;
			return;
		}
		wait_us(5000u);
	} while (!timed_out(until));

	nic->phy_bmsr = bmsr;
	LOG_INFO("e1000e: phy not ready after 200 ms (bmsr=0x%04x)\n",
		 (unsigned)bmsr);
}

int e1000e_phy_autoneg_step(struct e1000e *nic)
{
	uint16_t bmcr;
	uint16_t bmsr;
	uint16_t pss;

	/* First call restarts the negotiation; later calls poll its completion
	 * and, once complete, latch speed/duplex from the PHY's own result. */
	if ((nic->link.speed_mbps == 0u) && !nic->started) {
		phy_wait_ready(nic);
		if (phy_rd(nic, E1000_PHY_BMCR, &bmcr) != 0) {
			return -1;
		}
		nic->phy_bmcr = bmcr;
		bmcr |= E1000_PHY_BMCR_ANEG_ENABLE | E1000_PHY_BMCR_ANEG_RESTART;
		if (phy_wr(nic, E1000_PHY_BMCR, bmcr) != 0) {
			return -1;
		}
		nic->started = 1;
		nic->phy_polls = 0u;
		return 0;
	}

	if (phy_rd(nic, E1000_PHY_BMSR, &bmsr) != 0) {
		return -1;
	}
	nic->phy_bmsr = bmsr;
	nic->phy_polls++;
	if ((bmsr & E1000_PHY_BMSR_ANEG_COMPLETE) == 0u) {
		/* One self-healing restart: if the link is still down after
		 * ~2 s of polling, the first restart write may have been lost
		 * to a settling PHY or a re-negotiating partner - re-issue it
		 * exactly once (repeated restarts would keep knocking the
		 * negotiation down). */
		if ((nic->phy_polls >= 40u) && (nic->phy_restarts == 0u)) {
			if (phy_rd(nic, E1000_PHY_BMCR, &bmcr) == 0) {
				nic->phy_bmcr = bmcr;
				nic->phy_restarts = 1u;
				(void)phy_wr(nic, E1000_PHY_BMCR,
					     bmcr | E1000_PHY_BMCR_ANEG_ENABLE |
					     E1000_PHY_BMCR_ANEG_RESTART);
			}
		}
		return 0;
	}
	if (phy_rd(nic, E1000_PHY_PSS, &pss) != 0) {
		return -1;
	}

	switch (pss & E1000_PHY_PSS_SPEED_MASK) {
	case E1000_PHY_PSS_SPEED_1000:
		nic->link.speed_mbps = 1000u;
		break;
	case E1000_PHY_PSS_SPEED_100:
		nic->link.speed_mbps = 100u;
		break;
	default:
		nic->link.speed_mbps = 10u;
		break;
	}
	nic->link.full_duplex =
		(pss & E1000_PHY_PSS_FULL_DUPLEX) != 0u;

	return 1;
}

void e1000e_start(struct e1000e *nic)
{
	uint32_t rctl;

	/* Receiver: enable, broadcast accepted (ARP), 2048-byte buffers, FCS
	 * stripped by the MAC so the frame the stack sees is header+payload. */
	rctl = e1k_rd(nic, E1000_RCTL);
	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 |
		E1000_RCTL_SECRC;
	e1k_wr(nic, E1000_RCTL, rctl);

	/* Transmitter: enable, pad short frames, 1G full-duplex collision
	 * defaults. */
	e1k_set(nic, E1000_TCTL,
		E1000_TCTL_EN | E1000_TCTL_PSP |
		E1000_TCTL_CT_DEF | E1000_TCTL_COLD_FULL_1G);
}

void e1000e_link_poll(struct e1000e *nic)
{
	nic->link.up = (e1k_rd(nic, E1000_STATUS) & E1000_STATUS_LU) != 0u;
}

/* --- transmit ---------------------------------------------------------------------- */

/* Reclaim completed writebacks from the clean cursor. The device sets the
 * DD bit of a descriptor it has finished (coherent: readable directly). */
static void tx_reclaim(struct e1000e *nic)
{
	while ((nic->tx_inflight > 0u) &&
	       ((nic->tx[nic->tx_clean].sta & E1000_TXD_STA_DD) != 0u)) {
		nic->tx_clean = (nic->tx_clean + 1u) % E1000E_TX_DESCS;
		nic->tx_inflight--;
	}
}

/* Wait bounded for the oldest descriptor to complete. A full ring at line
 * rate drains in ~1.5 ms; 50 ms means the device went away. */
static int tx_wait_free(struct e1000e *nic)
{
	uint64_t until = deadline_from_us(50000u);

	while ((nic->tx[nic->tx_clean].sta & E1000_TXD_STA_DD) == 0u) {
		if (timed_out(until)) {
			nic->stats.tx_wait_timeouts++;
			return -1;
		}
		ARCH_ISB();
	}
	tx_reclaim(nic);
	return 0;
}

int e1000e_tx_frame(struct e1000e *nic, struct pbuf *p)
{
	/* Chain fallback: the stack posts single-pbuf frames
	 * (LWIP_NETIF_TX_SINGLE_PBUF); a lever build that turns the contract
	 * off still gets correct frames, linearized here. */
	static uint8_t linear[E1000E_FRAME_MAX] __attribute__((aligned(8)));
	const void *frame;
	uint16_t len;
	uint32_t idx;

	if (p == NULL || p->tot_len == 0u || p->tot_len > E1000E_FRAME_MAX) {
		nic->stats.tx_errors++;
		return -1;
	}

	frame = p->payload;
	len = p->len;
	if (p->next != NULL) {
		if (pbuf_copy_partial(p, linear, p->tot_len, 0) != p->tot_len) {
			nic->stats.tx_errors++;
			return -1;
		}
		frame = linear;
		len = p->tot_len;
	}

	tx_reclaim(nic);
	if (nic->tx_inflight == E1000E_TX_DESCS) {
		nic->stats.tx_waits++;
		if (tx_wait_free(nic) != 0) {
			nic->stats.tx_errors++;
			return -1;
		}
	}

	idx = nic->tx_next;
	nic->tx[idx].addr = (uint64_t)(uintptr_t)frame;
	nic->tx[idx].len = len;
	nic->tx[idx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS |
		E1000_TXD_CMD_RS;
	nic->tx_next = (nic->tx_next + 1u) % E1000E_TX_DESCS;
	nic->tx_inflight++;

	/* The doorbell orders after the descriptor stores (the store barrier);
	 * the device owns the buffer until the writeback - which the next
	 * reclaim sees. The pbuf is only freed by the caller AFTER this
	 * returns, and its payload is referenced no longer than that (the
	 * single-pbuf contract). */
	ARCH_DSB_ST();
	e1k_wr(nic, E1000_TDT, nic->tx_next);

	nic->stats.tx_frames++;
	return 0;
}

/* --- receive ----------------------------------------------------------------------- */

struct pbuf *e1000e_rx_take(struct e1000e *nic)
{
	struct pbuf *frame;
	uint32_t idx = nic->rx_next;
	uint16_t len;

	if ((nic->rx[idx].sta & E1000_RXD_STA_DD) == 0u) {
		return NULL;
	}

	len = nic->rx[idx].len;
	frame = nic->rx_buf[idx];

	if ((nic->rx[idx].err != 0u) ||
	    ((nic->rx[idx].sta & E1000_RXD_STA_EOP) == 0u) ||
	    (len > E1000E_FRAME_MAX)) {
		/* Bad frame: the pbuf stays armed as this slot's buffer; the
		 * frame is dropped by not handing it out. The writeback flags
		 * still clear - a bad descriptor must not ghost on the next
		 * wrap (same discipline as the arm path). */
		nic->stats.rx_errors++;
		nic->rx[idx].sta = 0u;
		frame = NULL;
	} else {
		/* Good frame: a fresh pbuf must replace it in this descriptor
		 * before the doorbell, or the device would DMA over a buffer
		 * the stack now owns. Pool exhaustion leaves the frame armed
		 * for a retry (the DD bit stays set, the next call revisits
		 * this slot). */
		if (rx_arm(nic, idx) != 0) {
			nic->stats.rx_pool_fail++;
			return NULL;
		}
		frame->len = len;
		frame->tot_len = len;
		nic->stats.rx_frames++;
	}

	nic->rx_next = (nic->rx_next + 1u) % E1000E_RX_DESCS;

	return frame;
}

void e1000e_rx_kick(struct e1000e *nic)
{
	/* The batched hand-back: descriptors re-armed in the burst are given
	 * to the device with ONE tail write (the last consumed index). */
	ARCH_DSB_ST();
	e1k_wr(nic, E1000_RDT,
	       (nic->rx_next + E1000E_RX_DESCS - 1u) % E1000E_RX_DESCS);
}

void e1000e_get_stats(const struct e1000e *nic, struct e1000e_stats *stats)
{
	*stats = nic->stats;
}
