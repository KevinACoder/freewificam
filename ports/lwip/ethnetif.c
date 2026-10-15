/*
 * @file   ethnetif.c
 * @brief  lwIP netif bridge over the GMAC driver.
 *
 * The shape is the upstream ethernetif skeleton: netif_add(..., eth_init,
 * tcpip_input) with etharp_output/linkoutput, where the low_level_* slots
 * are dwc_eqos's own entry points. The driver stages completed frames; this
 * bridge drains them (ethnetif_poll), wraps each in a pbuf and hands it to
 * the stack's input, and linearizes outgoing pbuf chains into the driver's
 * send_frame call.
 *
 * Threading (NO_SYS=0, core locking on): the RX service thread runs
 * ethnetif_poll and netif->input is tcpip_input, so frames reach the TCP/IP
 * thread through its mailbox; the transmit side runs on the TCP/IP thread.
 * The driver's own port-line mask covers driver-ring mutual exclusion; this
 * file adds no lock of its own.
 *
 * @design template instantiation of the upstream netif skeleton: the
 * low_level_init/output/input bodies are filled in against this project's
 * driver - third-party/lwip/contrib/examples/ethernetif/ethernetif.c:85-332
 * (netif_add assembly at :327-332, the linkoutput pbuf walk at :137-183, the
 * input pbuf_alloc + pbuf_take shape at :185-256). No framework code is
 * rewritten.
 */

#include <stddef.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#include "dwc_eqos.h"

#include "ethnetif.h"

/* One frame, linearized. Matches the driver's own frame ceiling. */
#define ETHNETIF_MTU		1500u
#define ETHNETIF_LINEAR_BUF	DWC_EQOS_RX_BUF_SIZE

static uint8_t out_linear[ETHNETIF_LINEAR_BUF] __attribute__((aligned(8)));
static uint8_t in_linear[ETHNETIF_LINEAR_BUF] __attribute__((aligned(8)));

/* Loss counters for the heartbeat: they localize a receive loss to the
 * pool, the input call or the driver without a debugger. */
static unsigned long g_poll_calls;
static unsigned long g_pool_fail;
static unsigned long g_input_fail;
static unsigned long g_fed_total;

void ethnetif_get_diag(unsigned long *poll_calls, unsigned long *pool_fail,
		       unsigned long *input_fail, unsigned long *fed_total)
{
	*poll_calls = g_poll_calls;
	*pool_fail = g_pool_fail;
	*input_fail = g_input_fail;
	*fed_total = g_fed_total;
}

/* netif->state carries the driver port index (the only per-netif fact the
 * bridge needs - the MAC address comes from the driver). */
static unsigned int port_of(struct netif *netif)
{
	return (unsigned int)(uintptr_t)netif->state;
}

static err_t eth_linkoutput(struct netif *netif, struct pbuf *p)
{
	unsigned int port = port_of(netif);
	uint32_t len = 0;
	struct pbuf *q;

	if (p->tot_len > ETHNETIF_LINEAR_BUF) {
		return ERR_BUF;
	}
	/* LWIP_NETIF_TX_SINGLE_PBUF makes the common case a single contiguous
	 * pbuf, and the driver copies into its own DMA buffer anyway: handing it
	 * the payload directly saves the whole-frame copy this bridge used to
	 * pay for nothing. Chains (the rare case) still linearize. */
	if (p->next == NULL) {
		return (dwc_eqos_send_frame(port, (const uint8_t *)p->payload,
					    p->tot_len) == EQOS_OK)
		       ? ERR_OK : ERR_IF;
	}
	for (q = p; q != NULL; q = q->next) {
		memcpy(&out_linear[len], q->payload, q->len);
		len += q->len;
	}

	return (dwc_eqos_send_frame(port, out_linear, len) == EQOS_OK)
	       ? ERR_OK : ERR_IF;
}

static err_t eth_init(struct netif *netif)
{
	unsigned int port = port_of(netif);
	uint8_t mac[DWC_EQOS_MAC_ADDR_LEN];

	netif->mtu = ETHNETIF_MTU;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
		       NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;

	if (dwc_eqos_get_mac_address(port, mac) != EQOS_OK) {
		return ERR_IF;
	}
	netif->hwaddr_len = DWC_EQOS_MAC_ADDR_LEN;
	memcpy(netif->hwaddr, mac, DWC_EQOS_MAC_ADDR_LEN);

	netif->output = etharp_output;
	netif->linkoutput = eth_linkoutput;

	return ERR_OK;
}

void ethnetif_add(struct netif *netif, unsigned int port,
		  const ip4_addr_t *ip, const ip4_addr_t *mask,
		  const ip4_addr_t *gw)
{
	(void)netif_add(netif, ip, mask, gw, (void *)(uintptr_t)port,
			eth_init, tcpip_input);
	netif_set_default(netif);
	netif_set_up(netif);
}

/* The driver-side seam: drain the staged frames into the stack. Returns the
 * number of frames handed to netif->input (the heartbeat's "fed" counter). */
unsigned int ethnetif_poll(struct netif *netif)
{
	unsigned int port = port_of(netif);
	unsigned int fed = 0;

	g_poll_calls++;
	while (dwc_eqos_get_rx_frame_size(port) > 0U) {
		int len = dwc_eqos_read_frame(port, in_linear, sizeof(in_linear));
		struct pbuf *p;

		if (len <= 0) {
			break;
		}
		p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
		if (p == NULL) {
			/* Pool exhaustion: drop the frame (the staging slot is
			 * already consumed), the TCP peer retransmits. */
			g_pool_fail++;
			continue;
		}
		if (pbuf_take(p, in_linear, (u16_t)len) != ERR_OK) {
			pbuf_free(p);
			g_pool_fail++;
			continue;
		}
		if (netif->input(p, netif) != ERR_OK) {
			pbuf_free(p);
			g_input_fail++;
			continue;
		}
		fed++;
		g_fed_total++;
	}

	return fed;
}
