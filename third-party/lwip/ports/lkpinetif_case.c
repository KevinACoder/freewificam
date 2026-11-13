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
#include <stdint.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip.h"
#include "lwip/ip4_addr.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/udp.h"
#include "lwip/inet_chksum.h"
#include "lwip/sys.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/dhcp.h"
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

	if (ip == NULL || mask == NULL)
		return -1;
	/* The AP role has no gateway: the peer is on-link.  NULL is the stated
	 * "no default route" request, not an empty string. */
	IP4_ADDR(&gwaddr, 0, 0, 0, 0);
	if (ip4addr_aton(ip, &ipaddr) == 0 ||
	    ip4addr_aton(mask, &maskaddr) == 0 ||
	    (gw != NULL && ip4addr_aton(gw, &gwaddr) == 0))
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

/* --- DHCP / late static address (G2) -------------------------------------- */

int
lkpi_net_dhcp_start(void)
{
	err_t err;

	LOCK_TCPIP_CORE();
	err = dhcp_start(&g_netif);
	if (err == ERR_OK)
		netif_set_default(&g_netif);
	UNLOCK_TCPIP_CORE();
	return err == ERR_OK ? 0 : -1;
}

int
lkpi_net_dhcp_wait(struct lkpi_net_lease *lease, unsigned int timeout_ms)
{
	unsigned int waited = 0u;

	if (lease == NULL)
		return -1;
	lease->ip[0] = '\0';
	lease->mask[0] = '\0';
	lease->gw[0] = '\0';
	while (waited <= timeout_ms) {
		int ready;

		LOCK_TCPIP_CORE();
		ready = dhcp_supplied_address(&g_netif) ? 1 : 0;
		if (ready) {
			ip4addr_ntoa_r(&g_netif.ip_addr, lease->ip,
				       (int)sizeof(lease->ip));
			ip4addr_ntoa_r(&g_netif.netmask, lease->mask,
				       (int)sizeof(lease->mask));
			ip4addr_ntoa_r(&g_netif.gw, lease->gw,
				       (int)sizeof(lease->gw));
		}
		UNLOCK_TCPIP_CORE();
		if (ready)
			return 0;
		osDelay(100u);
		waited += 100u;
	}
	return -1;
}

int
lkpi_net_set_static(const char *ip, const char *mask, const char *gw)
{
	ip4_addr_t ipaddr, maskaddr, gwaddr;

	if (ip == NULL || mask == NULL || gw == NULL)
		return -1;
	if (ip4addr_aton(ip, &ipaddr) == 0 ||
	    ip4addr_aton(mask, &maskaddr) == 0 ||
	    ip4addr_aton(gw, &gwaddr) == 0)
		return -1;
	LOCK_TCPIP_CORE();
#if LWIP_DHCP
	dhcp_stop(&g_netif);
#endif
	netif_set_addr(&g_netif, &ipaddr, &maskaddr, &gwaddr);
	netif_set_up(&g_netif);
	netif_set_link_up(&g_netif);
	netif_set_default(&g_netif);
	UNLOCK_TCPIP_CORE();
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

/* --- the throughput phases (C6T) -----------------------------------------
 *
 * One phase = one bounded window.  The case prints the anchors and this file
 * owns the session: lwiperf speaks iperf2 (the bench peer runs iperf 2.2.1),
 * and the UDP leg is the counting sink / fixed-length blast the wired `net`
 * case already proved (tests/board/rtos/app/cases/net.c).
 *
 * Slot discipline: a phase sets `tp_slot` before it starts a session and
 * clears it after the window.  The report callback is asynchronous (it runs
 * in the TCP/IP thread), so a report is credited to the slot that was active
 * when it arrived - a late loopback server report can never be counted as a
 * downlink report.  A report with no slot is dropped.
 */

#define LKPINETIF_UDP_BLAST_LEN		1470u
#define LKPINETIF_UDP_BLAST_BURST	64u

enum lkpinetif_tp_slot {
	TP_SLOT_NONE = 0,
	TP_SLOT_DOWN,
	TP_SLOT_UP,
	TP_SLOT_LOOP,
};

static volatile int tp_slot = TP_SLOT_NONE;
static volatile unsigned tp_down_done, tp_down_bytes, tp_down_ms, tp_down_kbit;
static volatile unsigned tp_up_done, tp_up_bytes, tp_up_ms, tp_up_kbit;
static volatile unsigned tp_lo_done, tp_lo_bytes, tp_lo_ms, tp_lo_kbit;

static void *g_iperf_server;
static unsigned g_iperf_server_port;

static struct udp_pcb *g_udp_sink;
static unsigned g_udp_sink_port;
static volatile unsigned g_udp_pkts;
static volatile unsigned g_udp_bytes;

static void
tp_report(void *arg, enum lwiperf_report_type type,
	  const ip_addr_t *local_addr, u16_t local_port,
	  const ip_addr_t *remote_addr, u16_t remote_port,
	  u32_t bytes, u32_t ms, u32_t kbitpsec)
{
	int slot = tp_slot;

	(void)arg;
	(void)local_addr;
	(void)local_port;
	(void)remote_addr;
	(void)remote_port;

	if (slot == TP_SLOT_NONE)
		return;

	if (type == LWIPERF_TCP_DONE_SERVER) {
		if (slot == TP_SLOT_DOWN) {
			tp_down_bytes = (unsigned)bytes;
			tp_down_ms = (unsigned)ms;
			tp_down_kbit = (unsigned)kbitpsec;
			tp_down_done = 1;
		} else if (slot == TP_SLOT_LOOP) {
			/* the loopback session's server half: either half's
			 * completion closes the attribution phase */
			tp_lo_bytes = (unsigned)bytes;
			tp_lo_ms = (unsigned)ms;
			tp_lo_kbit = (unsigned)kbitpsec;
			tp_lo_done = 1;
		}
	} else if (type == LWIPERF_TCP_DONE_CLIENT) {
		if (slot == TP_SLOT_UP) {
			tp_up_bytes = (unsigned)bytes;
			tp_up_ms = (unsigned)ms;
			tp_up_kbit = (unsigned)kbitpsec;
			tp_up_done = 1;
		} else if (slot == TP_SLOT_LOOP) {
			tp_lo_bytes = (unsigned)bytes;
			tp_lo_ms = (unsigned)ms;
			tp_lo_kbit = (unsigned)kbitpsec;
			tp_lo_done = 1;
		}
	}
}

static void
tp_phase_fill(struct lkpi_net_phase *out, int done, unsigned bytes,
	      unsigned ms, unsigned kbit, unsigned pkts)
{
	if (out == NULL)
		return;
	out->done = done ? 1 : 0;
	out->bytes = bytes;
	out->ms = ms;
	out->kbit = kbit;
	out->pkts = pkts;
}

static unsigned
tp_kbit(unsigned bytes, unsigned ms)
{
	return (ms != 0u) ? (unsigned)(((uint64_t)bytes * 8u) / ms) : 0u;
}

int
lkpi_net_iperf_server_start(unsigned port)
{
	if (g_iperf_server != NULL)
		return (g_iperf_server_port == port) ? 0 : -1;
	if (port == 0u || port > 65535u)
		return -1;

	LOCK_TCPIP_CORE();
	g_iperf_server = lwiperf_start_tcp_server(IP_ANY_TYPE, (u16_t)port,
	    tp_report, NULL);
	g_iperf_server_port = port;
	UNLOCK_TCPIP_CORE();

	return g_iperf_server != NULL ? 0 : -1;
}

int
lkpi_net_ready(void)
{
	return (g_stack_state == 1 && netif_is_up(&g_netif)) ? 1 : 0;
}

int
lkpi_net_iperf_down_begin(void)
{
	tp_down_done = 0u;
	tp_down_bytes = 0u;
	tp_down_ms = 0u;
	tp_down_kbit = 0u;
	tp_slot = TP_SLOT_DOWN;
	return 0;
}

int
lkpi_net_iperf_down_poll(struct lkpi_net_phase *out)
{
	if (out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));
	tp_phase_fill(out, tp_down_done, tp_down_bytes, tp_down_ms,
	    tp_down_kbit, 0u);
	return out->done ? 0 : 1;
}

void
lkpi_net_iperf_down_end(void)
{
	tp_slot = TP_SLOT_NONE;
}

int
lkpi_net_iperf_down(unsigned port, unsigned window_ms,
		    struct lkpi_net_phase *out)
{
	unsigned waited;

	if (out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));
	if (lkpi_net_iperf_server_start(port) != 0)
		return -1;

	(void)lkpi_net_iperf_down_begin();
	for (waited = 0u; (waited < window_ms) && (tp_down_done == 0u);
	     waited += 50u)
		(void)osDelay(50u);
	lkpi_net_iperf_down_end();

	tp_phase_fill(out, tp_down_done, tp_down_bytes, tp_down_ms,
	    tp_down_kbit, 0u);

	return (out->done != 0 && out->bytes != 0u) ? 0 : -1;
}

int
lkpi_net_iperf_up(const char *peer, unsigned port, unsigned wait_ms,
		  struct lkpi_net_phase *out)
{
	ip_addr_t addr;
	void *client;
	unsigned waited;

	if ((out == NULL) || (peer == NULL) || (port == 0u) || (port > 65535u))
		return -1;
	if (ip4addr_aton(peer, &addr) == 0)
		return -1;
	memset(out, 0, sizeof(*out));

	tp_up_done = 0u;
	tp_up_bytes = 0u;
	tp_up_ms = 0u;
	tp_up_kbit = 0u;
	tp_slot = TP_SLOT_UP;

	LOCK_TCPIP_CORE();
	client = lwiperf_start_tcp_client(&addr, (u16_t)port, LWIPERF_CLIENT,
	    tp_report, NULL);
	UNLOCK_TCPIP_CORE();
	if (client == NULL) {
		tp_slot = TP_SLOT_NONE;
		return -1;
	}

	/* The client half is time-limited by lwiperf's own settings
	 * (amount = -1000 -> 10 s), so this wait is a ceiling, not a pace. */
	for (waited = 0u; (waited < wait_ms) && (tp_up_done == 0u);
	     waited += 50u)
		(void)osDelay(50u);

	tp_slot = TP_SLOT_NONE;
	tp_phase_fill(out, tp_up_done, tp_up_bytes, tp_up_ms, tp_up_kbit, 0u);

	return (out->done != 0 && out->bytes != 0u) ? 0 : -1;
}

static void
udp_sink_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
	      const ip_addr_t *addr, u16_t port)
{
	(void)arg;
	(void)pcb;
	(void)addr;
	(void)port;

	if (p == NULL)
		return;
	g_udp_pkts++;
	g_udp_bytes += p->tot_len;
	pbuf_free(p);
}

int
lkpi_net_udp_sink_start(unsigned port)
{
	struct udp_pcb *pcb;

	if (g_udp_sink != NULL)
		return (g_udp_sink_port == port) ? 0 : -1;
	if (port == 0u || port > 65535u)
		return -1;

	LOCK_TCPIP_CORE();
	pcb = udp_new();
	if (pcb != NULL) {
		udp_recv(pcb, udp_sink_recv, NULL);
		if (udp_bind(pcb, IP_ANY_TYPE, (u16_t)port) != ERR_OK) {
			udp_remove(pcb);
			pcb = NULL;
		}
	}
	UNLOCK_TCPIP_CORE();

	if (pcb == NULL)
		return -1;
	g_udp_sink = pcb;
	g_udp_sink_port = port;
	return 0;
}

int
lkpi_net_udp_sink_window(unsigned window_ms, struct lkpi_net_phase *out)
{
	unsigned t0;
	unsigned ms;
	unsigned waited;

	if (out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));

	LOCK_TCPIP_CORE();
	g_udp_pkts = 0u;
	g_udp_bytes = 0u;
	UNLOCK_TCPIP_CORE();

	t0 = (unsigned)sys_now();
	for (waited = 0u; waited < window_ms; waited += 50u)
		(void)osDelay(50u);
	ms = (unsigned)sys_now() - t0;

	tp_phase_fill(out, (g_udp_bytes != 0u), g_udp_bytes, ms,
	    tp_kbit(g_udp_bytes, ms), g_udp_pkts);

	return (g_udp_bytes != 0u) ? 0 : -1;
}

int
lkpi_net_udp_blast(const char *peer, unsigned port, unsigned window_ms,
		   unsigned rate_kbps, struct lkpi_net_phase *out)
{
	struct udp_pcb *pcb;
	struct pbuf *p;
	ip_addr_t addr;
	uint64_t t0, deadline;
	unsigned sent = 0u;
	unsigned pkts = 0u;
	unsigned per_quantum;
	unsigned ms;

	if ((out == NULL) || (peer == NULL) || (port == 0u) || (port > 65535u))
		return -1;
	if (ip4addr_aton(peer, &addr) == 0)
		return -1;
	memset(out, 0, sizeof(*out));

	pcb = udp_new();
	if (pcb == NULL)
		return -1;
	p = pbuf_alloc(PBUF_TRANSPORT, LKPINETIF_UDP_BLAST_LEN, PBUF_RAM);
	if (p == NULL) {
		udp_remove(pcb);
		return -1;
	}
	(void)memset(p->payload, 0x5au, LKPINETIF_UDP_BLAST_LEN);
	if (udp_connect(pcb, &addr, (u16_t)port) != ERR_OK) {
		pbuf_free(p);
		udp_remove(pcb);
		return -1;
	}

	/* sys_now() is the stack's own millisecond clock; a 1 ms quantum on a
	 * 20 s window is 0.005%, far below the phase's own noise.
	 *
	 * Pacing: `rate_kbps` is split into 10 ms quanta (a 10 ms burst is short
	 * enough for the TCP/IP thread and the RX path to run between bursts).
	 * An unpaced blast is kept for the wired capacity shape, but on Wi-Fi it
	 * overruns the skb pool and the transmit entry refuses most frames - the
	 * delivered byte count then measures backpressure, not capacity. */
	/* bytes per 10 ms = rate/8/100; split into full datagrams */
	per_quantum = (rate_kbps != 0u)
		? (unsigned)(((uint64_t)rate_kbps * 1000u) / 8u / 100u /
			     LKPINETIF_UDP_BLAST_LEN)
		: LKPINETIF_UDP_BLAST_BURST;
	if (per_quantum == 0u)
		per_quantum = 1u;
	t0 = (uint64_t)sys_now();
	deadline = t0 + (uint64_t)window_ms;
	for (;;) {
		unsigned before = pkts;
		unsigned i;

		LOCK_TCPIP_CORE();
		for (i = 0u; i < per_quantum; i++) {
			if (udp_send(pcb, p) != ERR_OK)
				break;
			pkts++;
			sent += p->tot_len;
		}
		UNLOCK_TCPIP_CORE();
		if (pkts == before) {
			/* The transmit ring is full: one tick for the
			 * completion path is enough to make progress. */
			(void)osDelay(1u);
		}
		if (rate_kbps != 0u)
			(void)osDelay(10u);
		if ((uint64_t)sys_now() >= deadline)
			break;
	}
	ms = (unsigned)((uint64_t)sys_now() - t0);

	tp_phase_fill(out, (sent != 0u), sent, ms, tp_kbit(sent, ms), pkts);

	pbuf_free(p);
	udp_remove(pcb);
	return (sent != 0u) ? 0 : -1;
}

int
lkpi_net_loopback(unsigned port, unsigned window_ms,
		  struct lkpi_net_phase *out)
{
	ip_addr_t lo;
	void *client;
	unsigned waited;

	if (out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));

	tp_lo_done = 0u;
	tp_lo_bytes = 0u;
	tp_lo_ms = 0u;
	tp_lo_kbit = 0u;
	tp_slot = TP_SLOT_LOOP;

	IP_ADDR4(&lo, 127, 0, 0, 1);
	LOCK_TCPIP_CORE();
	client = lwiperf_start_tcp_client(&lo, (u16_t)port, LWIPERF_CLIENT,
	    tp_report, NULL);
	UNLOCK_TCPIP_CORE();
	if (client == NULL) {
		tp_slot = TP_SLOT_NONE;
		return -1;
	}

	for (waited = 0u; (waited < window_ms) && (tp_lo_done == 0u);
	     waited += 50u)
		(void)osDelay(50u);

	tp_slot = TP_SLOT_NONE;
	tp_phase_fill(out, tp_lo_done, tp_lo_bytes, tp_lo_ms, tp_lo_kbit, 0u);

	return (out->done != 0 && out->bytes != 0u) ? 0 : -1;
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
	out->rx_arp_req = d.rx_arp_req;
	out->rx_arp_rep = d.rx_arp_rep;
	out->rx_ip = d.rx_ip;
	out->rx_other = d.rx_other;
	out->rx_other_n = d.rx_other_n;
	{
		unsigned int i;

		for (i = 0u; i < 4u; i++) {
			out->rx_other_len[i] = d.rx_other_len[i];
			out->rx_other_proto[i] = d.rx_other_proto[i];
			memcpy(out->rx_other_head[i], d.rx_other_head[i], 16u);
		}
	}
	out->rx_no_netif = d.rx_no_netif;
	out->rx_input_fail = d.rx_input_fail;
	out->tx_frames = d.tx_frames;
	out->tx_fail = d.tx_fail;
	out->rx_bytes = d.rx_bytes;
	out->tx_bytes = d.tx_bytes;
	out->rx_deliver_ns = (unsigned long)d.rx_deliver_ns;
	out->rx_deliver_max_ns = (unsigned long)d.rx_deliver_max_ns;
	out->tx_ndo_ns = (unsigned long)d.tx_ndo_ns;
	out->tx_ndo_max_ns = (unsigned long)d.tx_ndo_max_ns;
}

void
lkpi_net_stack_diag(struct lkpi_net_stack_diag *out)
{
	struct lkpinetif_stack_diag d;

	if (out == NULL)
		return;
	memset(out, 0, sizeof(*out));
	lkpinetif_get_stack_diag(&d);
	out->tcp_recv = d.tcp_recv;
	out->tcp_xmit = d.tcp_xmit;
	out->tcp_drop = d.tcp_drop;
	out->tcp_memerr = d.tcp_memerr;
	out->tcp_err = d.tcp_err;
	out->pbuf_avail = d.pbuf_avail;
	out->pbuf_err = d.pbuf_err;
	out->tcpseg_avail = d.tcpseg_avail;
	out->tcpseg_err = d.tcpseg_err;
	out->mbox_post_ok = d.mbox_post_ok;
	out->mbox_post_full = d.mbox_post_full;
	out->mbox_fetch_ok = d.mbox_fetch_ok;
	out->mbox_fetch_empty = d.mbox_fetch_empty;
	out->mbox_fetch_wait_ms = (unsigned long)d.mbox_fetch_wait_ms;
}
