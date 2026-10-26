/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   gmac.h
 * @brief  First-party driver for the on-SoC Ethernet MAC (Synopsys DWC GMAC,
 *         dwmac1000-class register layout) and its MDIO-attached PHY.
 *
 * Derived from the reference standalone SDK's Ethernet MAC driver
 * (BSD-3-Clause, Copyright (C) 2022 Phytium Technology Co., Ltd.): its
 * register sequences and DMA ring contract are the silicon-proven base.
 * The naming, structure, the standard-clause-22 PHY handling and this
 * project's glue are new here.
 *
 * Model: copy-based, fully polled. The driver owns the descriptor rings and
 * their buffers; the caller hands frames over and takes them back one
 * complete frame at a time (the lwIP bridge in third-party/lwip/ports/
 * gmac_netif.c adapts that to pbufs). No interrupt is requested - the poll
 * thread paces reception, exactly like the wired PCIe NIC line on the same
 * board. The SoC DMA path is I/O-coherent: no cache maintenance on the ring
 * or the frame buffers, only a store barrier before a descriptor is handed
 * to the hardware.
 */

#ifndef FREEWIFICAM_BOARD_FIREFLY_GMAC_H
#define FREEWIFICAM_BOARD_FIREFLY_GMAC_H

#include <stddef.h>
#include <stdint.h>

#include "board_conf.h"

#define GMAC_MAC_ADDR_LEN	6U

struct gmac_link {
	unsigned int speed_mbps;	/* 10 / 100 / 1000 */
	unsigned int full_duplex : 1;
	unsigned int up : 1;
};

struct gmac_stats {
	uint32_t rx_frames;
	uint32_t rx_bytes;
	uint32_t rx_crc_errors;		/* RDES0 error-summary frames dropped */
	uint32_t rx_dropped;		/* complete frames the caller did not take */
	uint32_t tx_frames;
	uint32_t tx_busy;		/* sends that found the ring still owned */
};

struct gmac {
	uintptr_t base;
	uint8_t mac[GMAC_MAC_ADDR_LEN];
	uint8_t phy_addr;		/* 0xff until detected */
	uint16_t phy_id1;
	struct gmac_link link;
	struct gmac_stats stats;
	int an_restarted;		/* autoneg restart issued this bring-up */
};

/*
 * Reset and configure the controller, detect the PHY, build the descriptor
 * rings. Leaves the MAC and DMA stopped; gmac_start() brings the line up.
 * Returns 0 on success.
 */
int gmac_init(struct gmac *m, uintptr_t base);

/*
 * One non-blocking step of the PHY bring-up, paced by the caller: returns 1
 * once the link is up (and the MAC speed/duplex match it), 0 while the PHY
 * is still negotiating, negative on a hard MDIO failure. The first
 * configuration inherits whatever the boot loader's PHY already negotiated
 * - no PHY reset and no autoneg restart unless the link reads down.
 */
int gmac_phy_step(struct gmac *m);

/* Enable MAC and DMA transmission/reception over the built rings. */
void gmac_start(struct gmac *m);

/* Stop MAC and DMA (the pre-reset quiesce). */
void gmac_stop(struct gmac *m);

/*
 * Send one frame: copies it into the next free descriptor's buffer and
 * releases the descriptor to the DMA. Frames larger than the ring buffer
 * size are rejected. Returns 0 on success, -1 if the DMA still owns every
 * candidate descriptor (caller paces) or the length is invalid.
 */
int gmac_tx_frame(struct gmac *m, const void *frame, size_t len);

/*
 * Take one complete received frame: copies it into the caller's buffer and
 * returns its length; returns 0 when no complete frame is pending. Frames
 * longer than buf_size are taken and dropped (counted in rx_dropped).
 */
int gmac_rx_frame(struct gmac *m, uint8_t *buf, size_t buf_size);

/* Re-read the PHY state and align the MAC's speed/duplex with it. */
void gmac_link_poll(struct gmac *m);

/* One line of counters (deltas between calls give a phase's numbers). */
void gmac_get_stats(const struct gmac *m, struct gmac_stats *out);

#endif /* FREEWIFICAM_BOARD_FIREFLY_GMAC_H */
