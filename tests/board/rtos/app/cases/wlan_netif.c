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
#include "lwip/udp.h"

#include "lkpinetif.h"
#include "wlan_netif.h"

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
#ifndef WLAN_UDP_PORT
#define WLAN_UDP_PORT		5002u
#endif
#ifndef WLAN_UDP_SINK_SECONDS
#define WLAN_UDP_SINK_SECONDS	30u
#endif
#ifndef WLAN_PING_INTERVAL_MS
#define WLAN_PING_INTERVAL_MS	500u
#endif
#ifndef WLAN_PING_TRACE
#define WLAN_PING_TRACE		0
#endif

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

static void (*wlan_obs_hook)(unsigned int sec,
			     const struct wlan_netif_obs *obs);

void wlan_netif_set_obs_hook(void (*fn)(unsigned int sec,
			     const struct wlan_netif_obs *obs))
{
	wlan_obs_hook = fn;
}

/*
 * The stack half's snapshot. It is deliberately taken after the per-second
 * counters have been read and outside the console: the PCB fields are copied
 * under a bounded core-lock take, then the hook prints. A lock that cannot be
 * taken inside 50 ms is itself evidence (the stack is wedged or a holder is
 * slow), so the snapshot reports the owner and leaves the PCB fields zero.
 */
static void wlan_netif_emit_obs(unsigned int sec)
{
	struct lkpinetif_diag d;
	struct lkpinetif_stack_diag s;
	struct wlan_netif_obs o;
	struct tcp_pcb *pcb;

	if (wlan_obs_hook == NULL)
		return;
	memset(&o, 0, sizeof(o));
	memset(&d, 0, sizeof(d));
	memset(&s, 0, sizeof(s));
	lkpinetif_get_diag(&d);
	lkpinetif_get_stack_diag(&s);

	o.rx_frames = d.rx_frames;
	o.rx_bytes = d.rx_bytes;
	o.rx_alloc_fail = d.rx_alloc_fail;
	o.rx_post_fail = d.rx_post_fail;
	o.rx_input_fail = d.rx_input_fail;
	o.tx_frames = d.tx_frames;
	o.tx_bytes = d.tx_bytes;
	o.tx_fail = d.tx_fail;
	o.tcp_recv = s.tcp_recv;
	o.tcp_xmit = s.tcp_xmit;
	o.tcp_drop = s.tcp_drop;
	o.tcp_memerr = s.tcp_memerr;
	o.tcp_err = s.tcp_err;
	o.pcb_avail = s.pcb_avail;
	o.pcb_err = s.pcb_err;
	o.pbuf_avail = s.pbuf_avail;
	o.pbuf_err = s.pbuf_err;
	o.tcpseg_avail = s.tcpseg_avail;
	o.tcpseg_err = s.tcpseg_err;
	o.inpkt_avail = s.inpkt_avail;
	o.inpkt_err = s.inpkt_err;
	o.mbox_post_full = s.mbox_post_full;
	o.mbox_fetch_ok = s.mbox_fetch_ok;
	o.mbox_fetch_empty = s.mbox_fetch_empty;
	o.heartbeat = lkpinetif_tcpip_heartbeat();
	o.lock_owner = lkpinetif_core_lock_owner();
	o.lock_held_ms = lkpinetif_core_lock_held_ms();

	pcb = wlan_tcp_sink.conn;
	if (pcb != NULL && lkpinetif_core_lock_timeout(50u) == 0) {
		o.pcb_valid = 1u;
		o.tcp_state = (unsigned int)pcb->state;
		o.rcv_wnd = (unsigned int)pcb->rcv_wnd;
		o.snd_wnd = (unsigned int)pcb->snd_wnd;
		o.snd_queuelen = (unsigned int)pcb->snd_queuelen;
		o.unacked = (pcb->unacked != NULL) ? 1u : 0u;
		o.mss = (unsigned int)pcb->mss;
		o.nrtx = (unsigned int)pcb->nrtx;
		lkpinetif_core_unlock();
	}
	wlan_obs_hook(sec, &o);
}


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
	unsigned int stalls = 0u;
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
		if ((delta == 0u) && (wlan_tcp_sink.connected != 0)) {
			stalls++;
			CASE_ANCHOR("wlan: tcp stall t=%u idle=1s\n", i);
		}
#if WLAN_OBS
		wlan_netif_emit_obs(i);
#endif
	}

	CASE_ANCHOR("wlan: tcp summary seconds=%u rx=%u seg=%u stalls=%u error=%d\n",
		    seconds, (unsigned)wlan_tcp_sink.rx_bytes,
		    (unsigned)wlan_tcp_sink.segments, stalls, wlan_tcp_sink.error);
	wlan_tcp_sink_close();
	return (wlan_tcp_sink.rx_bytes != 0u && stalls == 0u) ? 0 : -1;
}

/* --- UDP downlink sink ----------------------------------------------------- */
/*
 * The fixed-rate receive witness: UDP has no TCP window or ACK feedback, so a
 * zero-byte second here means the receive path itself stopped, not that the
 * peer is waiting for an ACK. The caller's sender offers a known rate; the
 * board counts delivered datagrams and bytes.
 */
struct wlan_udp_sink_state {
	struct udp_pcb *pcb;
	volatile uint32_t rx_bytes;
	volatile uint32_t datagrams;
};

static struct wlan_udp_sink_state wlan_udp_sink;

static void
wlan_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
	      const ip_addr_t *addr, u16_t port)
{
	(void)arg;
	(void)pcb;
	(void)addr;
	(void)port;
	if (p == NULL)
		return;
	wlan_udp_sink.rx_bytes += (uint32_t)p->tot_len;
	wlan_udp_sink.datagrams++;
	pbuf_free(p);
}

int
wlan_netif_udp_sink(unsigned int seconds)
{
	struct udp_pcb *pcb;
	uint32_t last_bytes = 0u;
	uint32_t last_dgrams = 0u;
	unsigned int stalls = 0u;
	unsigned int i;

	if (seconds == 0u)
		seconds = WLAN_UDP_SINK_SECONDS;
	memset(&wlan_udp_sink, 0, sizeof(wlan_udp_sink));

	LOCK_TCPIP_CORE();
	pcb = udp_new();
	if (pcb == NULL) {
		UNLOCK_TCPIP_CORE();
		CASE_ANCHOR("wlan: FAIL udp pcb\n");
		return -1;
	}
	if (udp_bind(pcb, IP_ADDR_ANY, WLAN_UDP_PORT) != ERR_OK) {
		udp_remove(pcb);
		UNLOCK_TCPIP_CORE();
		CASE_ANCHOR("wlan: FAIL udp bind\n");
		return -1;
	}
	udp_recv(pcb, wlan_udp_recv, NULL);
	wlan_udp_sink.pcb = pcb;
	UNLOCK_TCPIP_CORE();

	CASE_ANCHOR("wlan: udp sink ready port=%u seconds=%u\n",
		    (unsigned)WLAN_UDP_PORT, seconds);
	for (i = 1u; i <= seconds; i++) {
		uint32_t total;
		uint32_t dgrams;
		uint32_t delta;
		uint32_t ddelta;

		(void)osDelay(1000u);
		total = wlan_udp_sink.rx_bytes;
		dgrams = wlan_udp_sink.datagrams;
		delta = total - last_bytes;
		ddelta = dgrams - last_dgrams;
		last_bytes = total;
		last_dgrams = dgrams;
		CASE_ANCHOR("wlan: udp t=%u rx=%u delta=%u dgram=%u ddelta=%u\n",
			    i, (unsigned)total, (unsigned)delta,
			    (unsigned)dgrams, (unsigned)ddelta);
		if (delta == 0u) {
			stalls++;
			CASE_ANCHOR("wlan: udp stall t=%u idle=1s\n", i);
		}
#if WLAN_OBS
		wlan_netif_emit_obs(i);
#endif
	}
	CASE_ANCHOR("wlan: udp summary seconds=%u rx=%u dgram=%u stalls=%u\n",
		    seconds, (unsigned)wlan_udp_sink.rx_bytes,
		    (unsigned)wlan_udp_sink.datagrams, stalls);
	LOCK_TCPIP_CORE();
	if (wlan_udp_sink.pcb != NULL) {
		udp_remove(wlan_udp_sink.pcb);
		wlan_udp_sink.pcb = NULL;
	}
	UNLOCK_TCPIP_CORE();
	return (wlan_udp_sink.rx_bytes != 0u && stalls == 0u) ? 0 : -1;
}

/* --- ICMP echo ------------------------------------------------------------- */

static volatile unsigned int wlan_ping_rx;
static volatile unsigned int wlan_ping_last_seq;

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
		wlan_ping_last_seq = (unsigned int)lwip_ntohs(iecho->seqno);
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

int wlan_netif_ping_ex(const char *peer_ip, unsigned int count,
		       unsigned int interval_ms, unsigned int *rx)
{
	ip_addr_t peer;
	struct raw_pcb *pcb;
	unsigned int i;
	unsigned int wait_ms;

	if ((peer_ip == NULL) || (count == 0u)) {
		return -1;
	}
	if (interval_ms == 0u)
		interval_ms = WLAN_PING_INTERVAL_MS;
	wait_ms = interval_ms + 100u;
	if (wait_ms < 200u)
		wait_ms = 200u;
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
	wlan_ping_last_seq = 0u;
	for (i = 1u; i <= count; i++) {
		unsigned int before = wlan_ping_rx;
		unsigned int waited = 0u;

		wlan_ping_send(pcb, &peer, (u16_t)i);
#if WLAN_PING_TRACE
		CASE_ANCHOR("wlan: ping tx seq=%u\n", i);
#endif
		while ((waited < wait_ms) && (wlan_ping_rx == before)) {
			(void)osDelay(10u);
			waited += 10u;
		}
		if (wlan_ping_rx == before) {
			CASE_ANCHOR("wlan: ping lost seq=%u\n", i);
		} else {
#if WLAN_PING_TRACE
			CASE_ANCHOR("wlan: ping rx seq=%u\n",
				    wlan_ping_last_seq);
#endif
		}
		(void)osDelay(interval_ms);
	}

	raw_remove(pcb);
	if (rx != NULL) {
		*rx = wlan_ping_rx;
	}
	CASE_ANCHOR("wlan: ping tx=%u rx=%u\n", count, wlan_ping_rx);
	return (wlan_ping_rx != 0u) ? 0 : -1;
}

int wlan_netif_ping(const char *peer_ip, unsigned int count, unsigned int *rx)
{
	return wlan_netif_ping_ex(peer_ip, count, WLAN_PING_INTERVAL_MS, rx);
}
