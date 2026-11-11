/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpinetif_case.c
 * @brief   The lwIP world's half of the C6 case face: bring the wireless netif
 *          up with a static address and send ICMP echoes through it.
 *
 * The board case lives in the stack's world and cannot include lwIP's headers
 * (the two worlds disagree about htons/ntohs and the buffer types), so the
 * contract is the plain-C trio in include/linux80211/net.h.  This file is the
 * only place that sees both the case's request and lwIP.
 *
 * The stack bring-up is the one-shot shape cases/net_stack.c uses: tcpip_init
 * is once per boot by contract, and a second call re-creates the stack's own
 * thread and mailboxes.  The ping is the raw-ICMP shape cases/net.c uses, with
 * the core lock held around every stack call because the TCP/IP thread is
 * running concurrently.
 */

#include <stddef.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip.h"
#include "lwip/ip4_addr.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/sys.h"
#include "arch/sys_arch.h"

#include "os/cmsis2/cmsis2.h"

#include "lkpinetif.h"
#include "linux80211/net.h"

#define LKPINETIF_PING_ID	0x5a5au
#define LKPINETIF_PING_TRACK	64u

static struct netif g_netif;

/* --- one-shot tcpip_init -------------------------------------------------- */

static osSemaphoreId_t g_stack_ready;
static int g_stack_state;		/* 0 cold, 1 up, -1 failed */

static void
net_stack_done(void *arg)
{
	(void)arg;
	g_stack_state = 1;
	(void)osSemaphoreRelease(g_stack_ready);
}

static int
net_stack_up(void)
{
	if (g_stack_state != 0)
		return g_stack_state == 1 ? 0 : -1;

	g_stack_ready = osSemaphoreNew(1u, 0u, NULL);
	if (g_stack_ready == NULL) {
		g_stack_state = -1;
		return -1;
	}

	tcpip_init(net_stack_done, NULL);

	if (osSemaphoreAcquire(g_stack_ready, 5000u) != osOK) {
		g_stack_state = -1;
		return -1;
	}
	return 0;
}

/* --- the netif ------------------------------------------------------------ */

int
lkpi_net_wlan_up(const char *ip, const char *mask, const char *gw, char mac[6])
{
	ip4_addr_t ipaddr, maskaddr, gwaddr;

	if (ip == NULL || mask == NULL || gw == NULL)
		return -1;
	if (ip4addr_aton(ip, &ipaddr) == 0 ||
	    ip4addr_aton(mask, &maskaddr) == 0 ||
	    ip4addr_aton(gw, &gwaddr) == 0)
		return -1;
	if (net_stack_up() != 0)
		return -2;
	if (lkpinetif_bind("wlan0") != 0)
		return -3;
	if (lkpinetif_add(&g_netif, &ipaddr, &maskaddr, &gwaddr) != ERR_OK)
		return -4;

	LOCK_TCPIP_CORE();
	netif_set_default(&g_netif);
	UNLOCK_TCPIP_CORE();

	lkpinetif_start_static(&g_netif);

	if (mac != NULL) {
		if (lkpinetif_hwaddr((uint8_t *)mac) != 0)
			memset(mac, 0, 6);
	}
	return 0;
}

/* --- raw ICMP echo -------------------------------------------------------- */

static struct raw_pcb *ping_pcb;
static ip_addr_t ping_dst;
static volatile unsigned ping_rx;
static unsigned ping_tx;
static unsigned ping_seen;
static unsigned ping_sent_ms[LKPINETIF_PING_TRACK];
static unsigned ping_rtt_n;
static unsigned ping_rtt_sum_ms;
static unsigned ping_rtt_min_ms;
static unsigned ping_rtt_max_ms;

/* A raw pcb is handed the packet from its IP header on, so the ICMP header
 * starts after the IP header the sender wrote. */
static u8_t
ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
	struct ip_hdr *iphdr;
	struct icmp_echo_hdr *iecho;
	u16_t hlen;

	(void)arg;
	(void)pcb;
	(void)addr;

	if (p->len < sizeof(struct ip_hdr))
		return 0;
	iphdr = (struct ip_hdr *)p->payload;
	if (IPH_V(iphdr) != 4u)
		return 0;
	hlen = IPH_HL_BYTES(iphdr);
	if (p->len < (u16_t)(hlen + sizeof(struct icmp_echo_hdr)))
		return 0;

	ping_seen++;
	iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);

	if ((ICMPH_TYPE(iecho) == ICMP_ER) &&
	    (iecho->id == PP_HTONS(LKPINETIF_PING_ID))) {
		u16_t seq = lwip_ntohs(iecho->seqno);
		unsigned sent = ping_sent_ms[seq % LKPINETIF_PING_TRACK];
		unsigned now = (unsigned)sys_now();

		ping_rx++;
		if (sent != 0u) {
			unsigned rtt = now - sent;

			/* A stale stamp (a duplicate reply from an earlier
			 * round) would price a wrapped interval; the one-second
			 * ceiling keeps that out of the statistics. */
			if (rtt < 1000u) {
				ping_rtt_n++;
				ping_rtt_sum_ms += rtt;
				if (ping_rtt_min_ms == 0u || rtt < ping_rtt_min_ms)
					ping_rtt_min_ms = rtt;
				if (rtt > ping_rtt_max_ms)
					ping_rtt_max_ms = rtt;
			}
		}
		pbuf_free(p);
		return 1;
	}

	return 0;
}

static void
ping_send(u16_t seq, unsigned payload_len)
{
	struct pbuf *p;
	struct icmp_echo_hdr *iecho;
	uint8_t *payload;
	u16_t len = (u16_t)(sizeof(struct icmp_echo_hdr) + payload_len);
	unsigned i;

	p = pbuf_alloc(PBUF_IP, len, PBUF_RAM);
	if (p == NULL)
		return;

	iecho = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = PP_HTONS(LKPINETIF_PING_ID);
	iecho->seqno = lwip_htons(seq);

	payload = (uint8_t *)p->payload + sizeof(struct icmp_echo_hdr);
	for (i = 0u; i < payload_len; i++)
		payload[i] = (uint8_t)(0xa5u ^ (uint8_t)(seq + i));

	iecho->chksum = inet_chksum(iecho, p->len);

	ping_sent_ms[seq % LKPINETIF_PING_TRACK] = (unsigned)sys_now();
	if (raw_sendto(ping_pcb, p, &ping_dst) == ERR_OK)
		ping_tx++;

	pbuf_free(p);
}

int
lkpi_net_ping(const char *dst, unsigned count, unsigned payload_len,
	      unsigned timeout_ms, unsigned *rx, unsigned *rtt_min_ms,
	      unsigned *rtt_avg_ms, unsigned *rtt_max_ms)
{
	unsigned i;

	if (dst == NULL || count == 0u || count > 1000u)
		return -1;
	if (ip4addr_aton(dst, &ping_dst) == 0)
		return -1;

	LOCK_TCPIP_CORE();
	ping_pcb = raw_new(IP_PROTO_ICMP);
	if (ping_pcb != NULL)
		raw_recv(ping_pcb, ping_recv, NULL);
	UNLOCK_TCPIP_CORE();
	if (ping_pcb == NULL)
		return -1;

	/* Warm-up: one throwaway echo forces the ARP resolution; the counted
	 * packets are not sent into an unresolved entry (lwIP drops them). */
	ping_send(0u, 8u);
	for (i = 0u; i < 50u; i++)
		(void)osDelay(10u);

	ping_tx = 0u;
	ping_rx = 0u;
	ping_seen = 0u;
	ping_rtt_n = 0u;
	ping_rtt_sum_ms = 0u;
	ping_rtt_min_ms = 0u;
	ping_rtt_max_ms = 0u;

	for (i = 1u; i <= count; i++) {
		unsigned before = ping_rx;
		unsigned waited;

		LOCK_TCPIP_CORE();
		ping_send((u16_t)i, payload_len);
		UNLOCK_TCPIP_CORE();

		for (waited = 0u;
		     waited < timeout_ms && ping_rx == before;
		     waited += 10u)
			(void)osDelay(10u);
	}

	LOCK_TCPIP_CORE();
	raw_remove(ping_pcb);
	ping_pcb = NULL;
	UNLOCK_TCPIP_CORE();

	if (rx != NULL)
		*rx = ping_rx;
	if (rtt_min_ms != NULL)
		*rtt_min_ms = ping_rtt_min_ms;
	if (rtt_avg_ms != NULL)
		*rtt_avg_ms = ping_rtt_n != 0u ? ping_rtt_sum_ms / ping_rtt_n : 0u;
	if (rtt_max_ms != NULL)
		*rtt_max_ms = ping_rtt_max_ms;

	return ping_rx == count ? 0 : -1;
}

/* --- counters ------------------------------------------------------------- */

void
lkpi_net_diag(struct lkpi_net_diag *out)
{
	struct lkpinetif_diag d;

	if (out == NULL)
		return;
	memset(out, 0, sizeof(*out));
	lkpinetif_get_diag(&d);
	out->rx_frames = d.rx_frames;
	out->rx_foreign = d.rx_foreign;
	out->rx_eapol = d.rx_eapol;
	out->rx_no_netif = d.rx_no_netif;
	out->rx_input_fail = d.rx_input_fail;
	out->tx_frames = d.tx_frames;
	out->tx_fail = d.tx_fail;
	out->rx_bytes = d.rx_bytes;
	out->tx_bytes = d.tx_bytes;
}
