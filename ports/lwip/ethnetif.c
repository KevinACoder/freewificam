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
#include "generic_timer.h"

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

/* Service-time split (2026-10-18): the UDP-vs-TCP cut proved the receive
 * chain sustains wire rate for UDP while TCP caps far below, so the question
 * is no longer "is the bridge expensive" but "which half of it is the TCP
 * half". These two buckets answer exactly that: the pbuf preparation
 * (alloc + take, identical work for both protocols) and the input call
 * (trivial for UDP, the whole TCP state machine for TCP). Ticks are the
 * 24 MHz system counter, so /24 gives microseconds; two reads per frame. */
static uint64_t g_prep_ticks;
static uint64_t g_input_ticks;
static unsigned long g_input_calls;

/* Traffic-shape classification (2026-10-19 instrument): every IPv4/TCP frame
 * that crosses the netif is one of a small set of shapes, and the mix says
 * what the TCP flow is actually made of - which matters because "0.54 TX/RX"
 * cannot be interpreted without knowing whether the small frames are pure
 * ACKs, dup ACKs or data. Cheap by construction: ethertype and protocol are
 * at fixed offsets, and only TCP frames pay the flag read. */
static struct ethnetif_shape g_rx_shape;
static struct ethnetif_shape g_tx_shape;
/* ACK-history per direction, for the duplicate-ACK split: the (seq, ack,
 * window) triple of the last pure ACK. A repeat with the same window is the
 * true duplicate-ACK signature (a hole report); a repeat with a different
 * window is a window update (tcp_recved's inflation path) and says nothing
 * about loss. */
static uint32_t g_rx_last_ack_seq;
static uint32_t g_rx_last_ack_ack;
static uint16_t g_rx_last_ack_win;
static uint32_t g_tx_last_ack_seq;
static uint32_t g_tx_last_ack_ack;
static uint16_t g_tx_last_ack_win;

static void ethnetif_classify(const uint8_t *frame, unsigned int len,
			      struct ethnetif_shape *shape, int ingress)
{
	const uint8_t *ip;
	unsigned int ip_hlen;

	shape->frames++;
	if (len < 34u) {
		shape->other++;
		return;
	}
	if (((frame[12] << 8) | frame[13]) != 0x0800u) {	/* IPv4 only */
		shape->other++;
		return;
	}
	ip = &frame[14];
	if ((ip[0] >> 4) != 4u) {
		shape->other++;
		return;
	}
	ip_hlen = (unsigned int)(ip[0] & 0x0fu) * 4u;
	if ((ip_hlen < 20u) || (ip[9] != 6u)) {		/* TCP only */
		shape->other++;
		return;
	}
	{
		unsigned int l4_off = 14u + ip_hlen;
		uint8_t flags;
		uint32_t seq, ack;
		uint16_t win;

		if (len < l4_off + 20u) {
			shape->other++;
			return;
		}
		flags = frame[l4_off + 13];
		seq = ((uint32_t)frame[l4_off + 4] << 24)
		    | ((uint32_t)frame[l4_off + 5] << 16)
		    | ((uint32_t)frame[l4_off + 6] << 8)
		    | (uint32_t)frame[l4_off + 7];
		ack = ((uint32_t)frame[l4_off + 8] << 24)
		    | ((uint32_t)frame[l4_off + 9] << 16)
		    | ((uint32_t)frame[l4_off + 10] << 8)
		    | (uint32_t)frame[l4_off + 11];
		win = (uint16_t)(((uint16_t)frame[l4_off + 14] << 8)
				 | frame[l4_off + 15]);
		if ((flags & 0x02u) != 0u) {		/* SYN */
			shape->syn++;
		} else if ((flags & 0x01u) != 0u) {	/* FIN */
			shape->fin++;
		} else if ((flags & 0x04u) != 0u) {	/* RST */
			shape->rst++;
		} else if ((ip[2] << 8 | ip[3]) > (ip_hlen + 20u)) {
			shape->data++;
		} else if ((flags & 0x10u) != 0u) {	/* pure ACK */
			uint32_t *last_seq = ingress ? &g_rx_last_ack_seq
						     : &g_tx_last_ack_seq;
			uint32_t *last_ack = ingress ? &g_rx_last_ack_ack
						     : &g_tx_last_ack_ack;
			uint16_t *last_win = ingress ? &g_rx_last_ack_win
						     : &g_tx_last_ack_win;

			shape->pure_ack++;
			if ((*last_seq != 0u || *last_ack != 0u)
			    && (*last_seq == seq) && (*last_ack == ack)) {
				if (*last_win == win) {
					shape->ack_dup++;
				} else {
					shape->ack_wupd++;
				}
			}
			*last_seq = seq;
			*last_ack = ack;
			*last_win = win;
		} else {
			shape->other++;
		}
	}
}

void ethnetif_get_shape(struct ethnetif_shape *rx, struct ethnetif_shape *tx)
{
	*rx = g_rx_shape;
	*tx = g_tx_shape;
}

void ethnetif_reset_shape(void)
{
	memset(&g_rx_shape, 0, sizeof(g_rx_shape));
	memset(&g_tx_shape, 0, sizeof(g_tx_shape));
	g_rx_last_ack_seq = 0u;
	g_rx_last_ack_ack = 0u;
	g_rx_last_ack_win = 0u;
	g_tx_last_ack_seq = 0u;
	g_tx_last_ack_ack = 0u;
	g_tx_last_ack_win = 0u;
}

void ethnetif_get_stack_prof(uint64_t *prep_ticks, uint64_t *input_ticks,
			     unsigned long *input_calls)
{
	*prep_ticks = g_prep_ticks;
	*input_ticks = g_input_ticks;
	*input_calls = g_input_calls;
}

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
		ethnetif_classify((const uint8_t *)p->payload, p->tot_len,
				  &g_tx_shape, 0);
		return (dwc_eqos_send_frame(port, (const uint8_t *)p->payload,
					    p->tot_len) == EQOS_OK)
		       ? ERR_OK : ERR_IF;
	}
	for (q = p; q != NULL; q = q->next) {
		memcpy(&out_linear[len], q->payload, q->len);
		len += q->len;
	}
	ethnetif_classify(out_linear, len, &g_tx_shape, 0);

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
#if defined(CONFIG_GMAC_TX_CSUM) && CONFIG_GMAC_TX_CSUM && LWIP_CHECKSUM_CTRL_PER_NETIF
	/* Hand the IPv4 header + TCP/UDP checksum generation to the MAC
	 * (dwc_eqos.c eqos_tx_csum_prepare writes the pseudo-header sum and
	 * sets CIC=3). The netif's switches must move together with the driver
	 * build flag or frames leave with either no checksum or a double one:
	 * same lockstep discipline as the RX IPC pair (lwipopts.h). ICMP is NOT
	 * offloaded - no pseudo-header in the protocol - so its GEN bit stays
	 * on, and its CHECK bit too (the RX IPC block does not cover ICMP). */
	NETIF_SET_CHECKSUM_CTRL(netif,
				NETIF_CHECKSUM_GEN_ICMP | NETIF_CHECKSUM_CHECK_ICMP
				| NETIF_CHECKSUM_CHECK_IP
				| NETIF_CHECKSUM_CHECK_TCP
				| NETIF_CHECKSUM_CHECK_UDP
				);
#else
	(void)netif;
#endif
	netif_set_default(netif);
	netif_set_up(netif);
}

/* The driver-side seam: drain the staged frames into the stack. Returns the
 * number of frames handed to netif->input (the heartbeat's "fed" counter).
 *
 * CONFIG_GMAC_RX_BATCH: take the core lock ONCE for the whole drain instead
 * of letting each tcpip_input take and release it. Both shapes end in the
 * same input path; the difference is the lock traffic - the per-frame shape
 * pays two CMSIS mutex kernel calls per frame (tcpip.c:259-261 in
 * LWIP_TCPIP_CORE_LOCKING_INPUT mode), and the bridge's own bucket for the
 * input call is where that cost shows. The lock is recursive in neither
 * backend, so this only works because nothing inside the drain re-locks;
 * that is exactly what tcpip_input does, so the batch wraps the loop and the
 * unbatched path is kept as the reverse lever. */
unsigned int ethnetif_poll(struct netif *netif)
{
	unsigned int port = port_of(netif);
	unsigned int fed = 0;

	g_poll_calls++;
#if defined(CONFIG_GMAC_RX_BATCH) && CONFIG_GMAC_RX_BATCH \
	&& LWIP_TCPIP_CORE_LOCKING && LWIP_TCPIP_CORE_LOCKING_INPUT
	LOCK_TCPIP_CORE();
#endif
	while (1) {
		int len;
		struct pbuf *p;
		uint64_t t0;

		len = (int)dwc_eqos_get_rx_frame_size(port);
		if (len <= 0) {
			break;
		}
#if defined(CONFIG_GMAC_RX_DIRECT) && CONFIG_GMAC_RX_DIRECT
		/* One copy less per frame (2026-10-19, the cost-bound verdict):
		 * the driver reads the staged frame STRAIGHT into the pool pbuf
		 * instead of into the linear scratch, and pbuf_take disappears.
		 * The stage slot is consumed by read_frame itself, so a pool
		 * failure has to drain it into the scratch (the slot must not
		 * stay blocked). */
		t0 = gtimer_count_get();
		p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
		if (p == NULL) {
			g_pool_fail++;
			(void)dwc_eqos_read_frame(port, in_linear, sizeof(in_linear));
			continue;
		}
		if (dwc_eqos_read_frame(port, (uint8_t *)p->payload, p->len) != len) {
			pbuf_free(p);
			g_pool_fail++;
			continue;
		}
		ethnetif_classify((const uint8_t *)p->payload, (unsigned int)len,
				  &g_rx_shape, 1);
		{
			uint64_t t1 = gtimer_count_get();

			g_prep_ticks += t1 - t0;
#else
		len = dwc_eqos_read_frame(port, in_linear, sizeof(in_linear));
		if (len <= 0) {
			break;
		}
		t0 = gtimer_count_get();
		ethnetif_classify(in_linear, (unsigned int)len, &g_rx_shape, 1);
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
		{
			uint64_t t1 = gtimer_count_get();

			g_prep_ticks += t1 - t0;
#endif
#if defined(CONFIG_GMAC_RX_BATCH) && CONFIG_GMAC_RX_BATCH \
	&& LWIP_TCPIP_CORE_LOCKING && LWIP_TCPIP_CORE_LOCKING_INPUT
			/* Already under the core lock: hand the frame straight
			 * to the stack's ethernet entry, the same function the
			 * locking tcpip_input would call after re-locking. */
			if (ethernet_input(p, netif) != ERR_OK) {
#else
			if (netif->input(p, netif) != ERR_OK) {
#endif
				pbuf_free(p);
				g_input_fail++;
				g_input_ticks += gtimer_count_get() - t1;
				continue;
			}
			g_input_ticks += gtimer_count_get() - t1;
			g_input_calls++;
		}
		fed++;
		g_fed_total++;
	}
#if defined(CONFIG_GMAC_RX_BATCH) && CONFIG_GMAC_RX_BATCH \
	&& LWIP_TCPIP_CORE_LOCKING && LWIP_TCPIP_CORE_LOCKING_INPUT
	UNLOCK_TCPIP_CORE();
#endif

	return fed;
}
