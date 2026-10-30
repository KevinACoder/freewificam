/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   xmac.h
 * @brief  First-party driver for the firefly v2 board's on-SoC Ethernet MAC
 *         (Cadence GEM-class controller with an SGMII SerDes) and its
 *         MDIO-attached PHY.
 *
 * Register facts come from the SoC parameter map and from the board's own
 * working mainline driver; the structure, the ring ownership discipline, the
 * standard-clause-22 PHY handling and the copy-based polled data path are
 * this project's (see LICENSES.md next to this file).
 *
 * Model: copy-based, fully polled - the same shape as the board family's other
 * NIC driver (gmac.{h,c}). The driver owns the descriptor rings and their
 * buffers; the caller hands frames over and takes them back one complete
 * frame at a time (the lwIP bridge in third-party/lwip/ports/xmac_netif.c
 * adapts that to pbufs). No interrupt is requested: a poll thread paces
 * reception.
 *
 * DMA memory: descriptors live in the `.noncacheable` window (they are 8 bytes
 * inside a 64-byte line - arming one descriptor by hand-flushing would push a
 * stale copy of its completed neighbours back over the DMA's writeback, and
 * the completion sweep would wait forever on a bit nothing will clear). Frame
 * buffers stay in ordinary cacheable .bss and are maintained explicitly around
 * each transfer, so the driver is correct whether the port snoops or not.
 */

#ifndef FREEWIFICAM_BOARD_FIREFLY_XMAC_H
#define FREEWIFICAM_BOARD_FIREFLY_XMAC_H

#include <stddef.h>
#include <stdint.h>

#include "board_conf.h"

#define XMAC_MAC_ADDR_LEN	6U

struct xmac_link {
	unsigned int speed_mbps;	/* 10 / 100 / 1000 */
	unsigned int full_duplex : 1;
	unsigned int up : 1;
};

struct xmac_stats {
	uint32_t rx_frames;
	uint32_t rx_bytes;
	uint32_t rx_crc_errors;		/* error-summary frames dropped */
	uint32_t rx_dropped;		/* complete frames the caller did not take */
	uint32_t tx_frames;
	uint32_t tx_bytes;
	uint32_t tx_busy;		/* sends that found the ring still owned */
	uint32_t tx_tsr;		/* last acknowledged transmit status */
	uint32_t tx_restarts;		/* how many times the leg had to be re-armed */
};

struct xmac {
	uintptr_t base;
	uint8_t mac[XMAC_MAC_ADDR_LEN];
	uint8_t phy_addr;		/* 0xff until detected */
	uint16_t phy_id1;
	struct xmac_link link;
	struct xmac_stats stats;
	int an_restarted;		/* autoneg restart issued this bring-up */
};

/*
 * Reset and configure the controller, detect the PHY, build the descriptor
 * rings. Leaves the MAC and DMA stopped; xmac_start() brings the line up.
 * Returns 0 on success.
 */
int xmac_init(struct xmac *m, uintptr_t base);

/*
 * One non-blocking step of the PHY bring-up, paced by the caller: returns 1
 * once the link is up (and the MAC's speed/duplex match it), 0 while the PHY
 * is still negotiating, negative on a hard MDIO failure. The first
 * configuration inherits whatever the boot loader's tftp session negotiated -
 * no PHY reset and no autoneg restart unless the link reads down.
 */
int xmac_phy_step(struct xmac *m);

/* Enable MAC and DMA transmission/reception over the built rings. */
void xmac_start(struct xmac *m);

/* Stop MAC and DMA (the pre-reset quiesce). */
void xmac_stop(struct xmac *m);

/*
 * Send one frame: copies it into the next free descriptor's buffer and
 * releases the descriptor to the DMA. Frames larger than the ring buffer size
 * are rejected. Returns 0 on success, -1 if the DMA still owns every
 * candidate descriptor (caller paces) or the length is invalid.
 */
int xmac_tx_frame(struct xmac *m, const void *frame, size_t len);

/*
 * Take one complete received frame: copies it into the caller's buffer and
 * returns its length; returns 0 when no complete frame is pending. Frames
 * longer than buf_size are taken and dropped (counted in rx_dropped).
 */
int xmac_rx_frame(struct xmac *m, uint8_t *buf, size_t buf_size);

/* Re-read the PHY state and align the MAC's speed/duplex with it. */
/* Interrupt entry: acknowledge the controller and report whether the receive
 * line was among the sources (1 = there is work for the drain). Safe from
 * exception context; touches no RTOS object. */
int xmac_irq_ack(struct xmac *m);

/* Re-open the receive line after a drain. The handler masks it (a level
 * receive-complete line re-asserts until the ring is empty), so every drain has
 * to call this, including one that took the semaphore without a fresh
 * interrupt having fired. */
void xmac_rx_rearm(struct xmac *m);

void xmac_link_poll(struct xmac *m);

/* One line of counters (deltas between calls give a phase's numbers). */
void xmac_get_stats(const struct xmac *m, struct xmac_stats *out);

/* Receive-all (promiscuous) mode - a bench diagnostic for frame watching. */
void xmac_set_promisc(struct xmac *m, int on);

/* Register offsets the case layer may snapshot for a verdict line. Keeping
 * them here rather than re-deriving the numbers in a case file is the point:
 * one place knows the map. */
#define XMAC_REG_NCR	0x0000u
#define XMAC_REG_TSR	0x0014u
#define XMAC_REG_TBQP	0x001cu
#define XMAC_REG_NSR	0x0008u
#define XMAC_REG_ISR	0x0024u
#define XMAC_REG_IMR	0x0030u

/* Raw register read for diagnostics. Read-only: the driver keeps ownership of
 * every write. */
uint32_t xmac_reg_read(const struct xmac *m, uint32_t off);

#endif /* FREEWIFICAM_BOARD_FIREFLY_XMAC_H */
