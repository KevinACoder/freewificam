/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   e1000e.h
 * @brief  First-party driver for the PCIe Ethernet NIC on the firefly
 *         boards (Intel 82574L, PCI 8086:10d3): descriptor-ring DMA,
 *         polled data plane, lwIP pbuf-backed buffers.
 *
 * WHAT THIS DRIVER IS. A single-queue legacy-descriptor driver for the
 * 82574L behind the firefly ECAM root complex. The firmware (U-Boot) has
 * already trained the link, assigned the BARs and enabled bus mastering;
 * this driver resets the MAC core, negotiates the PHY, arms the RX ring
 * with lwIP pool pbufs and moves frames in both directions - no interrupt
 * is requested or enabled, the data plane is drained by polling (the
 * reference standalone SDK's iperf rounds measured this same shape above
 * 900 Mbit/s rx and tx on this exact NIC).
 *
 * DMA COHERENCY. The SoC's PCIe path is I/O coherent: device accesses
 * snoop the CPU caches, so descriptors and frame buffers live in plain
 * cacheable RAM and the driver performs NO cache maintenance anywhere in
 * the data path. (Board-proven fact: the reference standalone SDK runs
 * this NIC with caches ON and without any cache operations on the
 * descriptor/buffer path at line rate.) The only ordering rule kept is a
 * store barrier before each tail-pointer doorbell write, so the device
 * never fetches a half-written descriptor.
 *
 * THREADING. One RX drainer (e1000e_rx_take) and one TX poster
 * (e1000e_tx_frame) may run concurrently - the rings are disjoint and the
 * registers each side touches are disjoint (RDT vs TDT). The lwIP glue
 * satisfies this naturally: linkoutput runs on the TCP/IP thread, the RX
 * drain on the poll thread.
 *
 * @design exemplars:
 *   - Linux drivers/net/ethernet/intel/e1000e/netdev.c - the legacy ring
 *     discipline: arm descriptors with buffer addresses, write the tail
 *     doorbell, clean on descriptor-done (e1000_clean_rx_irq /
 *     e1000_xmit_frame);
 *   - Zephyr drivers/ethernet/eth_e1000.c - the pbuf-shaped minimal e1000:
 *     TX posts the frame buffer the stack handed over, RX wraps the armed
 *     buffer into a packet (eth_tx / eth_rx);
 *   - this repository's dwc_eqos (board/rk3568-itx/drivers) - the polled
 *     RX bottom-half and the stats-for-the-console discipline.
 */

#ifndef FREEWIFICAM_E1000E_H
#define FREEWIFICAM_E1000E_H

#include <stdbool.h>
#include <stdint.h>

#include "lwip/pbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define E1000E_RX_DESCS		128u
#define E1000E_TX_DESCS		128u

/* Ethernet header + 1500-byte MTU; the MAC strips the FCS (RCTL_SECRC). */
#define E1000E_FRAME_MAX	1514u
#define E1000E_MTU		1500u

/* Legacy descriptors, 16 bytes each, their natural layout. */
struct e1000e_rx_desc {
	volatile uint64_t	addr;
	volatile uint16_t	len;
	volatile uint16_t	csum;
	volatile uint8_t	sta;
	volatile uint8_t	err;
	volatile uint16_t	special;
};

struct e1000e_tx_desc {
	volatile uint64_t	addr;
	volatile uint16_t	len;
	volatile uint8_t	cso;
	volatile uint8_t	cmd;
	volatile uint8_t	sta;
	volatile uint8_t	css;
	volatile uint16_t	special;
};

struct e1000e_stats {
	uint32_t	rx_frames;	/* frames handed up */
	uint32_t	rx_errors;	/* error bits or bad length */
	uint32_t	rx_pool_fail;	/* re-arm alloc failed (frame retried) */
	uint32_t	rx_polls;	/* drain calls (idle-rate evidence) */
	uint32_t	tx_frames;
	uint32_t	tx_errors;
	uint32_t	tx_waits;	/* ring full, waited for a writeback */
	uint32_t	tx_wait_timeouts;
};

struct e1000e_link {
	bool		up;
	bool		full_duplex;
	unsigned int	speed_mbps;
};

struct e1000e {
	uintptr_t		base;		/* BAR0, identity-mapped MMIO */
	uint8_t			bus;
	uint8_t			dev;
	uint8_t			fn;
	uint8_t			mac[6];

	struct e1000e_rx_desc	rx[E1000E_RX_DESCS] __attribute__((aligned(16)));
	struct pbuf		*rx_buf[E1000E_RX_DESCS];
	uint32_t		rx_next;

	struct e1000e_tx_desc	tx[E1000E_TX_DESCS] __attribute__((aligned(16)));
	uint32_t		tx_next;	/* next free slot to fill */
	uint32_t		tx_clean;	/* oldest slot not yet reclaimed */
	uint32_t		tx_inflight;

	struct e1000e_stats	stats;
	struct e1000e_link	link;
	/* PHY bring-up state (the autoneg step's own): the last control/status
	 * words - the caller prints them when its budget expires - and the
	 * poll counter / one-restart latch. */
	uint16_t		phy_bmcr;
	uint16_t		phy_bmsr;
	uint16_t		phy_polls;
	uint8_t			phy_restarts;
	int			started;
};

/* Reset the MAC core, restore the station MAC, arm both descriptor rings
 * with lwIP pool pbufs and leave RX/TX stopped. The lwIP memory pools must
 * already exist (tcpip_init has run), because the RX ring is armed from
 * the pool. Returns 0, or -1 on reset/MDI timeout. */
int e1000e_init(struct e1000e *nic, uintptr_t bar0,
		uint8_t bus, uint8_t dev, uint8_t fn);

/* Restart PHY auto-negotiation. Non-blocking step function: returns 1 when
 * negotiation completed (link fields of `nic` refreshed), 0 while still in
 * progress, -1 on MDI timeout. The caller drives the pacing. */
int e1000e_phy_autoneg_step(struct e1000e *nic);

/* Enable the receiver and the transmitter (after init/autoneg). */
void e1000e_start(struct e1000e *nic);

/* Refresh the link view (MAC STATUS register; speed/duplex from the PHY's
 * negotiated result recorded by the autoneg step). */
void e1000e_link_poll(struct e1000e *nic);

/* Post one frame. The pbuf must be a single-pbuf frame (the stack contract
 * LWIP_NETIF_TX_SINGLE_PBUF); a chain is linearized into a driver buffer.
 * Reclaims completed writebacks first; blocks bounded (polling the oldest
 * descriptor) only when the ring is full. Returns 0 on posted, -1 on
 * error/timeout. */
int e1000e_tx_frame(struct e1000e *nic, struct pbuf *p);

/* Take one received frame: returns the armed pbuf with len/tot_len set to
 * the frame length (the stack owns it now - input or free it), the
 * descriptor re-armed with a fresh pool pbuf. NULL when nothing is done.
 * On pool exhaustion the frame stays armed in the ring and is retried by a
 * later call. The tail doorbell is NOT written per frame - the drain loop
 * posts the batches' last consumed index once via e1000e_rx_kick(). */
struct pbuf *e1000e_rx_take(struct e1000e *nic);

/* Ring the RX tail doorbell for every descriptor consumed so far (the
 * batched hand-back: one MMIO write per drain burst instead of one per
 * frame - the write is a posted transaction across the PCIe link). */
void e1000e_rx_kick(struct e1000e *nic);

void e1000e_get_stats(const struct e1000e *nic, struct e1000e_stats *stats);

/* Raw 32-bit CSR read at a byte offset (diagnostics; the register facts
 * live in e1000e_regs.h). */
uint32_t e1000e_reg_rd(const struct e1000e *nic, uint32_t off);

#ifdef __cplusplus
}
#endif

#endif /* FREEWIFICAM_E1000E_H */
