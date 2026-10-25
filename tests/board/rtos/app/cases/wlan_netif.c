/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   wlan_netif.c
 * @brief  The lwIP half of the 8189FTV data path: assemble the netif over the
 *         driver's net device, set the static address, and send ICMP echoes.
 *
 * This file must not include a linuxkpi or driver-world header; the calls that
 * cross into that world are the plain-C seam in lkpinetif_seam.h.  The receive
 * path is the other direction: lkpinetif.c's lkpi_netdev_rx() calls the lwIP
 * half's lkpinetif_deliver(), which hands the frame to tcpip_input().
 */

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet_chksum.h"
#include "lwip/etharp.h"
#include "lwip/tcp.h"

#include "lkpinetif.h"

#include <string.h>

#define WLAN_NETIF_IP_A		192u
#define WLAN_NETIF_IP_B		168u
#define WLAN_NETIF_IP_C		99u
#define WLAN_NETIF_IP_D		2u
#define WLAN_NETIF_GW_D		1u

#define WLAN_PING_ID		0x8189u
#define WLAN_PING_WAIT_MS	500u
#define WLAN_TCP_PORT		5001u
#define WLAN_TCP_CONNECT_WAIT_MS 60000u

static struct netif wlan_netif;
static osSemaphoreId_t wlan_stack_ready;

static void wlan_stack_done(void *arg)
{
	(void)arg;
	if (wlan_stack_ready != NULL) {
		(void)osSemaphoreRelease(wlan_stack_ready);
	}
}

int wlan_netif_start(void)
{
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gw;

	wlan_stack_ready = osSemaphoreNew(1u, 0u, NULL);
	if (wlan_stack_ready == NULL) {
		CASE_ANCHOR("wlan: FAIL stack semaphore\n");
		return -1;
	}

	tcpip_init(wlan_stack_done, NULL);
	if (osSemaphoreAcquire(wlan_stack_ready, 5000u) != osOK) {
		CASE_ANCHOR("wlan: FAIL tcpip init timeout\n");
		return -1;
	}

	IP4_ADDR(&ip, WLAN_NETIF_IP_A, WLAN_NETIF_IP_B, WLAN_NETIF_IP_C,
		 WLAN_NETIF_IP_D);
	IP4_ADDR(&mask, 255u, 255u, 255u, 0u);
	IP4_ADDR(&gw, WLAN_NETIF_IP_A, WLAN_NETIF_IP_B, WLAN_NETIF_IP_C,
		 WLAN_NETIF_GW_D);

	if (lkpinetif_add(&wlan_netif, &ip, &mask, &gw) != ERR_OK) {
		CASE_ANCHOR("wlan: FAIL netif add\n");
		return -1;
	}
	netif_set_up(&wlan_netif);
	CASE_ANCHOR("wlan: netif up ip=192.168.99.2/24\n");
	return 0;
}

void wlan_netif_link(int up)
{
	lkpinetif_note_link(&wlan_netif, up);
}


/* --- TCP downlink sink ----------------------------------------------------- */
/*
 * The throughput round needs a receive-side witness that does not share a
 * clock with the AP sender: one lwIP PCB, byte/segment counters, and a
 * one-second line.  A zero-byte second while the connection is up is the
 * receive-stall signature; the caller keeps the connection open long enough
 * for the AP-side sender to finish.
 */
struct wlan_tcp_sink_state {
	struct tcp_pcb *listen_pcb;
	struct tcp_pcb *conn;
	volatile uint32_t rx_bytes;
	volatile uint32_t segments;
	volatile int connected;
	volatile int closed;
	volatile int error;
};

static struct wlan_tcp_sink_state wlan_tcp_sink;

static err_t
wlan_tcp_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	(void)arg;

	if (p == NULL) {
		/* remote close: the PCB is ours to close */
		wlan_tcp_sink.conn = NULL;
		wlan_tcp_sink.connected = 0;
		wlan_tcp_sink.closed = 1;
		(void)tcp_close(pcb);
		return ERR_OK;
	}
	if (err != ERR_OK) {
		pbuf_free(p);
		return err;
	}
	wlan_tcp_sink.rx_bytes += (uint32_t)p->tot_len;
	wlan_tcp_sink.segments++;
	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

static void
wlan_tcp_err(void *arg, err_t err)
{
	(void)arg;
	wlan_tcp_sink.conn = NULL;
	wlan_tcp_sink.connected = 0;
	wlan_tcp_sink.closed = 1;
	wlan_tcp_sink.error = (int)err;
}

static err_t
wlan_tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	(void)arg;

	if ((err != ERR_OK) || (newpcb == NULL))
		return ERR_VAL;
	if (wlan_tcp_sink.conn != NULL)
		return ERR_ABRT;
	wlan_tcp_sink.conn = newpcb;
	wlan_tcp_sink.connected = 1;
	wlan_tcp_sink.closed = 0;
	tcp_arg(newpcb, &wlan_tcp_sink);
	tcp_recv(newpcb, wlan_tcp_recv);
	tcp_err(newpcb, wlan_tcp_err);
	return ERR_OK;
}

static void
wlan_tcp_sink_close(void)
{
	LOCK_TCPIP_CORE();
	if (wlan_tcp_sink.conn != NULL) {
		tcp_arg(wlan_tcp_sink.conn, NULL);
		tcp_recv(wlan_tcp_sink.conn, NULL);
		tcp_err(wlan_tcp_sink.conn, NULL);
		if (tcp_close(wlan_tcp_sink.conn) != ERR_OK)
			tcp_abort(wlan_tcp_sink.conn);
		wlan_tcp_sink.conn = NULL;
	}
	if (wlan_tcp_sink.listen_pcb != NULL) {
		tcp_arg(wlan_tcp_sink.listen_pcb, NULL);
		tcp_accept(wlan_tcp_sink.listen_pcb, NULL);
		if (tcp_close(wlan_tcp_sink.listen_pcb) != ERR_OK)
			tcp_abort(wlan_tcp_sink.listen_pcb);
		wlan_tcp_sink.listen_pcb = NULL;
	}
	UNLOCK_TCPIP_CORE();
}

int
wlan_netif_tcp_sink(unsigned int seconds)
{
	struct tcp_pcb *pcb;
	struct tcp_pcb *listen_pcb;
	uint32_t last_bytes = 0u;
	uint32_t last_segments = 0u;
	unsigned int waited;
	unsigned int i;
	err_t err;

	if (seconds == 0u)
		return -1;
	memset(&wlan_tcp_sink, 0, sizeof(wlan_tcp_sink));

	LOCK_TCPIP_CORE();
	pcb = tcp_new();
	if (pcb == NULL) {
		UNLOCK_TCPIP_CORE();
		CASE_ANCHOR("wlan: FAIL tcp pcb\n");
		return -1;
	}
	err = tcp_bind(pcb, IP_ADDR_ANY, WLAN_TCP_PORT);
	if (err != ERR_OK) {
		(void)tcp_close(pcb);
		UNLOCK_TCPIP_CORE();
		CASE_ANCHOR("wlan: FAIL tcp bind err=%d\n", (int)err);
		return -1;
	}
	listen_pcb = tcp_listen(pcb);
	if (listen_pcb == NULL) {
		UNLOCK_TCPIP_CORE();
		CASE_ANCHOR("wlan: FAIL tcp listen\n");
		return -1;
	}
	wlan_tcp_sink.listen_pcb = listen_pcb;
	tcp_arg(listen_pcb, &wlan_tcp_sink);
	tcp_accept(listen_pcb, wlan_tcp_accept);
	UNLOCK_TCPIP_CORE();

	CASE_ANCHOR("wlan: tcp sink ready port=%u seconds=%u\n",
		    (unsigned)WLAN_TCP_PORT, seconds);

	waited = 0u;
	while ((wlan_tcp_sink.connected == 0) &&
	       (waited < WLAN_TCP_CONNECT_WAIT_MS)) {
		(void)osDelay(100u);
		waited += 100u;
	}
	if (wlan_tcp_sink.connected == 0) {
		CASE_ANCHOR("wlan: FAIL tcp connect timeout\n");
		wlan_tcp_sink_close();
		return -1;
	}
	CASE_ANCHOR("wlan: tcp connected\n");

	for (i = 1u; i <= seconds; i++) {
		uint32_t total;
		uint32_t segs;
		uint32_t delta;
		uint32_t seg_delta;

		(void)osDelay(1000u);
		total = wlan_tcp_sink.rx_bytes;
		segs = wlan_tcp_sink.segments;
		delta = total - last_bytes;
		seg_delta = segs - last_segments;
		last_bytes = total;
		last_segments = segs;
		CASE_ANCHOR("wlan: tcp t=%u rx=%u delta=%u seg=%u "
			    "seg_delta=%u state=%s\n",
			    i, (unsigned)total, (unsigned)delta,
			    (unsigned)segs, (unsigned)seg_delta,
			    wlan_tcp_sink.connected ? "up" : "closed");
		if ((delta == 0u) && (wlan_tcp_sink.connected != 0))
			CASE_ANCHOR("wlan: tcp stall t=%u idle=1s\n", i);
	}

	CASE_ANCHOR("wlan: tcp summary seconds=%u rx=%u seg=%u error=%d\n",
		    seconds, (unsigned)wlan_tcp_sink.rx_bytes,
		    (unsigned)wlan_tcp_sink.segments, wlan_tcp_sink.error);
	wlan_tcp_sink_close();
	return (wlan_tcp_sink.rx_bytes != 0u) ? 0 : -1;
}

/* --- ICMP echo ------------------------------------------------------------- */

static volatile unsigned int wlan_ping_rx;

static u8_t wlan_ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
			   const ip_addr_t *addr)
{
	struct ip_hdr *iphdr;
	struct icmp_echo_hdr *iecho;
	u16_t hlen;

	(void)arg;
	(void)pcb;
	(void)addr;

	if (p->len < sizeof(struct ip_hdr)) {
		return 0;
	}
	iphdr = (struct ip_hdr *)p->payload;
	if (IPH_V(iphdr) != 4u) {
		return 0;
	}
	hlen = IPH_HL_BYTES(iphdr);
	if (p->len < (u16_t)(hlen + sizeof(struct icmp_echo_hdr))) {
		return 0;
	}
	iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);
	if ((ICMPH_TYPE(iecho) == ICMP_ER) &&
	    (iecho->id == PP_HTONS(WLAN_PING_ID))) {
		wlan_ping_rx++;
		pbuf_free(p);
		return 1;
	}
	return 0;
}

/* Wait until lwIP has the peer's neighbour entry.  A request sent into an
 * unresolved entry is dropped without queueing, so the first counted echo
 * would otherwise price the ARP resolution, not the link.  (The platform's
 * wired case waits the same way.) */
static int wlan_arp_wait(const ip4_addr_t *peer, unsigned int ms)
{
	unsigned int waited = 0u;

	while (waited < ms) {
		const ip4_addr_t *addr = NULL;
		struct eth_addr *eth = NULL;
		int rc;

		LOCK_TCPIP_CORE();
		rc = etharp_find_addr(&wlan_netif, peer, &eth, &addr);
		UNLOCK_TCPIP_CORE();
		if ((rc >= 0) && (eth != NULL)) {
			return 0;
		}
		(void)osDelay(20u);
		waited += 20u;
	}
	return -1;
}

static void wlan_ping_send(struct raw_pcb *pcb, const ip_addr_t *peer,
			   u16_t seq)
{
	struct pbuf *p;
	struct icmp_echo_hdr *iecho;
	u16_t len = (u16_t)(sizeof(struct icmp_echo_hdr) + 8u);
	uint8_t *payload;
	unsigned int i;

	p = pbuf_alloc(PBUF_IP, len, PBUF_RAM);
	if (p == NULL) {
		return;
	}
	iecho = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = PP_HTONS(WLAN_PING_ID);
	iecho->seqno = lwip_htons(seq);
	payload = (uint8_t *)p->payload + sizeof(struct icmp_echo_hdr);
	for (i = 0u; i < 8u; i++) {
		payload[i] = (uint8_t)(0xa5u ^ (uint8_t)(seq + i));
	}
	iecho->chksum = inet_chksum(iecho, p->len);
	(void)raw_sendto(pcb, p, peer);
	pbuf_free(p);
}

int wlan_netif_ping(const char *peer_ip, unsigned int count, unsigned int *rx)
{
	ip_addr_t peer;
	struct raw_pcb *pcb;
	unsigned int i;

	if ((peer_ip == NULL) || (count == 0u)) {
		return -1;
	}
	if (!ip4addr_aton(peer_ip, (ip4_addr_t *)&peer)) {
		CASE_ANCHOR("wlan: FAIL bad peer address %s\n", peer_ip);
		return -1;
	}

	pcb = raw_new(IP_PROTO_ICMP);
	if (pcb == NULL) {
		CASE_ANCHOR("wlan: FAIL raw pcb\n");
		return -1;
	}
	raw_recv(pcb, wlan_ping_recv, NULL);

	/* One throwaway echo forces the neighbour resolution and proves the
	 * echo path before counting; lwIP drops a request sent into an
	 * unresolved entry without queueing it.  Wait for its reply as well,
	 * so the first counted echo does not price the join/settling window. */
	wlan_ping_rx = 0u;
	wlan_ping_send(pcb, &peer, 0u);
	{
		unsigned int waited = 0u;

		while ((wlan_ping_rx == 0u) && (waited < 2000u)) {
			(void)osDelay(10u);
			waited += 10u;
		}
	}
	(void)wlan_arp_wait((const ip4_addr_t *)&peer, 2000u);
	/* Give the join's TX/EDCA and the AP's station entry a settle window
	 * before the first counted echo; the short 500 ms shape still lost the
	 * first packet on alternating boots. */
	(void)osDelay(2000u);

	wlan_ping_rx = 0u;
	for (i = 1u; i <= count; i++) {
		unsigned int before = wlan_ping_rx;
		unsigned int waited = 0u;

		wlan_ping_send(pcb, &peer, (u16_t)i);
		while ((waited < WLAN_PING_WAIT_MS) &&
		       (wlan_ping_rx == before)) {
			(void)osDelay(10u);
			waited += 10u;
		}
		if (wlan_ping_rx == before)
			CASE_ANCHOR("wlan: ping lost seq=%u\n", i);
	}

	raw_remove(pcb);
	if (rx != NULL) {
		*rx = wlan_ping_rx;
	}
	CASE_ANCHOR("wlan: ping tx=%u rx=%u\n", count, wlan_ping_rx);
	return (wlan_ping_rx != 0u) ? 0 : -1;
}
