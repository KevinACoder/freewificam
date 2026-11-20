/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   stm32eth_netif.c
 * @brief  lwIP netif bridge over the F429 board's wired Ethernet seam.
 *
 * The shape is the wired line's existing skeleton (ethnetif.c on the RK3568
 * GMAC, xmac_netif.c on the firefly board): netif_add(..., tcpip_input) with
 * etharp_output, the driver's frame entry points in the low-level slots, and
 * the receive side drained by a poll thread.  What differs is only the
 * driver face: the board's `eth.h` (plain C, no HAL types) instead of a
 * controller-specific header.
 *
 * Threading: the transmit side runs on the TCP/IP thread (linkoutput), the
 * receive side on the case's drain thread (poll).  The board driver
 * serialises its own HAL handle; this file adds no lock of its own.
 */

#include <stdint.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#include "eth.h"
#include "stm32eth_netif.h"

/* One linear frame, used by the transmit path only.  The transmit side is the
 * TCP/IP thread alone, so one buffer is enough; the driver copies it into
 * the DMA descriptor before returning. */
static uint8_t stm32eth_tx_linear[F429_ETH_FRAME_MAX];
static uint8_t stm32eth_rx_linear[F429_ETH_FRAME_MAX];

/* Receive-path counters: the case prints them beside the driver's own, so a
 * frame that reached the driver but not the stack is distinguishable from a
 * frame the driver never saw. */
static struct stm32eth_netif_stats stm32eth_stats;

static err_t stm32eth_linkoutput(struct netif *netif, struct pbuf *p);

static err_t stm32eth_netif_init(struct netif *netif)
{
	uint8_t mac[6];

	netif->name[0] = 'e';
	netif->name[1] = 'n';
	netif->hwaddr_len = 6U;
	f429_eth_mac(mac);
	memcpy(netif->hwaddr, mac, sizeof(mac));
	netif->mtu = 1500U;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
	netif->output = etharp_output;
	netif->linkoutput = stm32eth_linkoutput;
	return ERR_OK;
}

static err_t stm32eth_linkoutput(struct netif *netif, struct pbuf *p)
{
	uint16_t total;
	err_t err;

	(void)netif;
	total = p->tot_len;
	if (total == 0U || total > sizeof(stm32eth_tx_linear)) {
		stm32eth_stats.tx_oversize++;
		return ERR_MEM;
	}
	/* Copy the chain into one frame: the driver's transmit slot is one
	 * descriptor holding one contiguous buffer, and the copy is the
	 * contract every wired bridge on this line has. */
	if (pbuf_copy_partial(p, stm32eth_tx_linear, total, 0U) != total) {
		stm32eth_stats.tx_copy_fail++;
		return ERR_BUF;
	}
	err = (f429_eth_send_frame(stm32eth_tx_linear, total) == 0) ?
	      ERR_OK : ERR_MEM;
	if (err != ERR_OK) {
		stm32eth_stats.tx_drop++;
	}
	return err;
}

void stm32eth_netif_add(struct netif *netif, const ip4_addr_t *ip,
			const ip4_addr_t *mask, const ip4_addr_t *gw)
{
	memset(&stm32eth_stats, 0, sizeof(stm32eth_stats));
	netif_add(netif, ip, mask, gw, netif, stm32eth_netif_init, tcpip_input);
}

unsigned int stm32eth_netif_poll(struct netif *netif, unsigned int budget)
{
	unsigned int fed = 0U;
	unsigned int i;

	for (i = 0U; i < budget; i++) {
		struct pbuf *p;
		unsigned int len = 0U;
		int got = f429_eth_read_frame(stm32eth_rx_linear,
					      sizeof(stm32eth_rx_linear),
					      &len);

		if (got <= 0) {
			break;
		}
		p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
		if (p == NULL) {
			stm32eth_stats.rx_pbuf_fail++;
			continue;
		}
		if (pbuf_take(p, stm32eth_rx_linear, (u16_t)len) != ERR_OK) {
			stm32eth_stats.rx_copy_fail++;
			pbuf_free(p);
			continue;
		}
		if (netif->input(p, netif) != ERR_OK) {
			stm32eth_stats.rx_input_fail++;
			pbuf_free(p);
			continue;
		}
		stm32eth_stats.rx_fed++;
		fed++;
	}
	return fed;
}

void stm32eth_netif_stats_snapshot(struct stm32eth_netif_stats *out)
{
	if (out != NULL) {
		*out = stm32eth_stats;
	}
}
