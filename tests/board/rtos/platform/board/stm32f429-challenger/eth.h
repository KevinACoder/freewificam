/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   eth.h
 * @brief  The F429 board's wired Ethernet, as a frame-level interface.
 *
 * This is the plain-C face of the imported ST HAL ETH driver plus the board
 * seam around it (pins, clocks, PHY, MAC address, the HAL time base).  The
 * case and the lwIP netif bridge include THIS header, never a HAL header:
 * the HAL's types and macros stay inside eth.c, the same discipline the
 * wireless egress uses to keep its two worlds apart.
 *
 * The driver owns the MAC/DMA descriptor rings and copies one frame per
 * call.  That is the whole contract: read one completed frame into the
 * caller's buffer, or copy one frame into the next transmit descriptor.
 * Nothing here refers to lwIP, and nothing here blocks longer than a HAL
 * register sequence.
 */

#ifndef FREEWIFICAM_BOARD_STM32F429_ETH_H
#define FREEWIFICAM_BOARD_STM32F429_ETH_H

#include <stdint.h>

/* Ring geometry: mirrors stm32f4xx_hal_conf.h (8 + 8 buffers of 1524 B). */
#define F429_ETH_RX_RING	8U
#define F429_ETH_TX_RING	8U
#define F429_ETH_FRAME_MAX	1524U

/* The LAN8720A's strap address on this board (datasheet 3.7.1 default, and
 * the address the vendor reference ladder runs at).  Published so the case
 * can print the address it actually used instead of a copied literal. */
#define F429_ETH_PHY_ADDR	0U

struct f429_eth_link {
	unsigned int speed_mbit;	/* 10 or 100 when up, 0 when down */
	unsigned int full_duplex;	/* 1 full, 0 half */
};

struct f429_eth_stats {
	unsigned long rx_frames;	/* frames copied up to the caller */
	unsigned long rx_len_err;	/* frame outside 1..1524 B, or segmented */
	unsigned long rx_copy_err;	/* caller buffer too small */
	unsigned long tx_frames;
	unsigned long tx_busy;		/* ring full, HAL busy, or lock timeout */
	unsigned long tx_err;
	unsigned long dma_missed;	/* DMAMFBOCR missed-by-application */
	unsigned long dma_fifo_ovf;	/* DMAMFBOCR FIFO overflow counter */
};

/* The one-line snapshot a failed bring-up needs: the MAC/DMA registers and
 * the PHY's clause-22 registers, read back rather than assumed.  Values are
 * filled even when the driver did not reach a running state (PHY reads use
 * the HAL's MDIO path and count their own failures). */
struct f429_eth_diag {
	uint32_t dmabmr;
	uint32_t dmasr;
	uint32_t maccr;
	uint32_t macffr;
	uint32_t macmiiar;
	uint32_t macmiidr;
	uint16_t phy_bcr;
	uint16_t phy_bsr;
	uint16_t phy_id1;
	uint16_t phy_id2;
	uint16_t phy_sr;
	unsigned int phy_errors;
};

/*
 * Bring the MAC and the PHY up.  Returns 0 on success; a positive value is
 * the HAL status of the failing step (1 error, 2 timeout, 3 busy) and a
 * negative value is a board-level precondition failure (-1 no descriptor
 * memory, -2 mutex creation).  Idempotent: a second call returns 0 without
 * re-initialising (the HAL handle and the rings are process-wide).
 */
int f429_eth_init(void);

/* Locally administered address derived from the device UID; stable across
 * boots, unique per board. */
void f429_eth_mac(uint8_t mac[6]);

/* Read the PHY's live link state.  Returns 1 up, 0 down, -1 when MDIO did
 * not answer.  Fills `out` (speed 0 when down). */
int f429_eth_link(struct f429_eth_link *out);

int f429_eth_phy_read(uint16_t reg, uint16_t *val);
int f429_eth_phy_write(uint16_t reg, uint16_t val);

/*
 * One completed receive frame, copied into `dst` (capacity `cap`).  Returns
 * 1 when a frame was returned, 0 when the ring is empty, -1 on a driver
 * error (the error is counted; a frame that cannot fit is dropped and
 * counted as rx_len_err/rx_copy_err and the call returns 0).
 */
int f429_eth_read_frame(uint8_t *dst, unsigned int cap, unsigned int *len);

/* Copy one frame into the next transmit descriptor and hand it to the DMA.
 * Returns 0 on success, -1 when the ring is full or the HAL refused. */
int f429_eth_send_frame(const uint8_t *frame, unsigned int len);

void f429_eth_stats_snapshot(struct f429_eth_stats *out);
void f429_eth_diag(struct f429_eth_diag *out);

#endif /* FREEWIFICAM_BOARD_STM32F429_ETH_H */
