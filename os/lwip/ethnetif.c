/*
 * @file   ethnetif.c
 * @brief  lwIP netif bridge over the GMAC driver (the FR-8 seam).
 *
 * The shape is fixed and independent of the NO_SYS switch (specs/lwip-adapt
 * FR-8, so S5a/S7a/S8 face a stable surface): netif_add(..., eth_init,
 * ethernet_input) with etharp_output/linkoutput, and the driver-side
 * receive seam ethnetif_poll() - driver staging ring -> pbuf_alloc(PBUF_RAW)
 * + pbuf_take -> netif->input. Under NO_SYS=1 that input function is
 * ethernet_input and the poll runs in the app main loop; after P7-2a it is
 * tcpip_input and the bridge code does not change (input is a pointer on
 * the netif).
 *
 * The bridge owns nothing the driver owns: it is the only consumer of
 * dwc_eqos_read_frame() and the only producer of dwc_eqos_send_frame().
 * TX linearizes the pbuf chain into one static buffer first - the DMA
 * buffers live in the driver (2 KiB slots), so the copy here is unavoidable
 * and the buffer the driver ultimately DMAs from is always its own aligned
 * slot (the xHCI bring-up lesson: every byte handed to a DMA engine comes
 * from aligned static storage).
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

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
#define ETHNETIF_LINEAR_BUF	(DWC_EQOS_RX_BUF_SIZE)

static uint8_t tx_linear[ETHNETIF_LINEAR_BUF] __attribute__((aligned(8)));
static uint8_t rx_linear[ETHNETIF_LINEAR_BUF] __attribute__((aligned(8)));

/* N7 diagnostics: the R2 board round showed the driver completing frames the
 * stack never saw; these counters localize the loss without a debugger. */
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
	for (q = p; q != NULL; q = q->next) {
		memcpy(&tx_linear[len], q->payload, q->len);
		len += q->len;
	}

	return (dwc_eqos_send_frame(port, tx_linear, len) == EQOS_OK)
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
#if NO_SYS
	/* NO_SYS=1: input is ethernet_input, called from the main loop's
	 * ethnetif_poll. */
	(void)netif_add(netif, ip, mask, gw, (void *)(uintptr_t)port,
			eth_init, ethernet_input);
#else
	/* NO_SYS=0: the RX thread's ethnetif_poll hands frames to the tcpip
	 * thread's mailbox - same bridge, one input pointer (spec FR-8). */
	(void)netif_add(netif, ip, mask, gw, (void *)(uintptr_t)port,
			eth_init, tcpip_input);
#endif
	netif_set_default(netif);
	netif_set_up(netif);
}

/* The driver-side seam: drain the staging ring into the stack. Returns the
 * number of frames handed to netif->input (the heartbeat's "fed" counter). */
unsigned int ethnetif_poll(struct netif *netif)
{
	unsigned int port = port_of(netif);
	unsigned int fed = 0;

	g_poll_calls++;
	while (dwc_eqos_get_rx_frame_size(port) > 0U) {
		int len = dwc_eqos_read_frame(port, rx_linear, sizeof(rx_linear));
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
		if (pbuf_take(p, rx_linear, (u16_t)len) != ERR_OK) {
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
