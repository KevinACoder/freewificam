/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_net.c
 * @brief  Case iwl7260_net: the IP presentation layer over the net80211
 *         bridge - lwIP attached to the bridged interface, ICMP to the AP, and
 *         one TCP session in each direction against the bench's iperf peer.
 *
 * Why this file is an lwIP translation unit and not a driver one: lwIP's
 * headers and the driver world's headers own the same names (lwIP's htons
 * against the contract header's), so no file may see both.  The bridge's face
 * is deliberately scalar-only (wlan_bridge.h -> seam.h carries stdint/stddef
 * and nothing else), so this file includes it and never a linuxkpi header:
 *   TX  linkoutput -> wlan_bridge_tx_raw() (an ethernet frame enters net80211's
 *       own encap/crypto path, exactly as the raw-L2 dataplane case does);
 *   RX  wlan_bridge_set_data_rx() hands back a complete ethernet frame, which
 *       this file copies into a pbuf and gives to the stack's input.
 * The shape follows the 8189FTV image's netif (tests/board/rtos/app/cases/
 * wlan_netif.c) and the net80211 line's template (its wlannetif.c), with the
 * low edge pointed at the bridge instead of a FullMAC driver's net device: this
 * port has no net_device per interface (linuxkpi_80211.c), so lkpinetif cannot
 * bind here.
 *
 * Locking: the RTOS lwIP build is core-locked with deferred input
 * (ports/include/lwipopts.h: LWIP_TCPIP_CORE_LOCKING=1,
 * LWIP_TCPIP_CORE_LOCKING_INPUT=0), so the receive callback only posts to the
 * TCP/IP mailbox and every stack call this thread makes takes the core lock for
 * the duration of that call.  No transmit happens on the receive context.
 *
 * The two phases are driven from outside: the image prints
 * `net: phase <n> begin ... name=down|tcpup` before it waits, and the board
 * round tool answers with the peer command that phase names (tools/rtos/
 * board-round.py, PEER_ACTIONS).  Downlink: this image is the iperf server and
 * the peer pushes with iperf2.  Uplink: this image starts the iperf client and
 * the peer's iperf2 server is the independent witness.
 *
 * Anchors:
 *   iwl7260: net up ip=192.168.99.2/24 mac=<mac> mtu=1500
 *   iwl7260: net ping tx=10 rx=<n> min_ms=<n> max_ms=<n>
 *   iwl7260: net iperf down done=<0|1> bytes=<n> ms=<n> kbit=<n>
 *   iwl7260: net iperf up done=<0|1> bytes=<n> ms=<n> kbit=<n>
 *   iwl7260: net counters tx=<n> txb=<n> txfail=<n> rx=<n> rxb=<n> rxfail=<n>
 *   iwl7260: net PASS
 *
 * A failed gate is `iwl7260: net FAIL <reason>` with the same counters, so
 * "not associated", "the netif never came up", "the AP did not answer ICMP" and
 * "a TCP direction carried nothing" are told apart from the log alone.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "wlan_bridge.h"

/* The port's serializer (declared by the component's port.h; repeated here so
 * this lwIP translation unit stays free of the net80211 world's headers).  The
 * transmit path is the second caller of the bridge that is not the receive
 * path, and the bridge does not take the serializer itself for it: the raw-L2
 * dataplane case got away with one low-rate transmit thread, a TCP session does
 * not. */
void wlan_port_serializer_lock(void);
void wlan_port_serializer_unlock(void);

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip4.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet_chksum.h"
#include "lwip/etharp.h"
#include "lwip/apps/lwiperf.h"

#define IWL_NET_IP_A		192u
#define IWL_NET_IP_B		168u
#define IWL_NET_IP_C		99u
#define IWL_NET_IP_US		2u
#define IWL_NET_IP_AP		1u

#define IWL_NET_MTU		1500u
#define IWL_NET_FRAME_MAX	1600u

#define IWL_NET_PING_N		10u
#define IWL_NET_PING_GAP_MS	500u
#define IWL_NET_PING_WAIT_MS	800u
#define IWL_NET_PING_ID		0x7260u
#define IWL_NET_PING_MIN_RX	9u

/* The downlink window is the peer's `iperf -c ... -t 10` plus connect slack;
 * the uplink window bounds the board-initiated client (the peer's server ends
 * the session, and the report is what this side waits for). */
#define IWL_NET_DOWN_WAIT_MS	45000u
#define IWL_NET_UP_WAIT_MS	40000u
#define IWL_NET_WAIT_STEP_MS	100u

#define IWL_NET_RUN_STATE	4

static struct netif iwl_netif;
static osSemaphoreId_t iwl_stack_ready;

/* The transmit staging buffer: lwIP serialises linkoutput on the TCP/IP thread,
 * so one buffer is this port's shape (the 8189FTV netif keeps one too). */
static uint8_t iwl_tx_linear[IWL_NET_FRAME_MAX] __attribute__((aligned(8)));

static uint8_t iwl_net_mac[6];

/* Accounting: the judge and the run report read these to place a failure on the
 * bridge, the netif or the stack. */
static volatile unsigned iwl_tx_frames;
static volatile unsigned iwl_tx_bytes;
static volatile unsigned iwl_tx_fail;
static volatile unsigned iwl_rx_frames;
static volatile unsigned iwl_rx_bytes;
static volatile unsigned iwl_rx_fail;

/* --- ping ---------------------------------------------------------------- */

static struct raw_pcb *iwl_ping_pcb;
static ip_addr_t iwl_ap_ip;
static volatile unsigned iwl_ping_tx;
static volatile unsigned iwl_ping_rx;
static volatile unsigned iwl_ping_seen;
static uint64_t iwl_ping_sent_us[IWL_NET_PING_N + 1u];
static unsigned iwl_ping_rtt_min_us;
static unsigned iwl_ping_rtt_max_us;

static void
iwl_net_stack_done(void *arg)
{
	(void)arg;
	if (iwl_stack_ready != NULL)
		(void)osSemaphoreRelease(iwl_stack_ready);
}

static int
iwl_net_stack_start(void)
{

	iwl_stack_ready = osSemaphoreNew(1u, 0u, NULL);
	if (iwl_stack_ready == NULL)
		return -1;
	tcpip_init(iwl_net_stack_done, NULL);
	if (osSemaphoreAcquire(iwl_stack_ready, 5000u) != osOK)
		return -1;
	return 0;
}

/* --- the netif ----------------------------------------------------------- */

static err_t
iwl_net_link_output(struct netif *netif, struct pbuf *p)
{
	int rc;

	(void)netif;

	if (p->tot_len > (u16_t)sizeof(iwl_tx_linear)) {
		iwl_tx_fail++;
		return ERR_BUF;
	}
	if (pbuf_copy_partial(p, iwl_tx_linear, p->tot_len, 0) != p->tot_len) {
		iwl_tx_fail++;
		return ERR_BUF;
	}
#if defined(IWL_NET_TRACE)
	CASE_ANCHOR("iwl7260: net tx pbuf=%p len=%u type=%02x%02x\n", (void *)p,
	    (unsigned)p->tot_len, iwl_tx_linear[12], iwl_tx_linear[13]);
#endif
	wlan_port_serializer_lock();
	rc = wlan_bridge_tx_raw(iwl_tx_linear, p->tot_len);
	wlan_port_serializer_unlock();
	if (rc < 0) {
		iwl_tx_fail++;
		return ERR_IF;
	}
	iwl_tx_frames++;
	iwl_tx_bytes += p->tot_len;
	return ERR_OK;
}

static err_t
iwl_net_if_init(struct netif *netif)
{

	netif->name[0] = 'w';
	netif->name[1] = 'l';
	netif->mtu = IWL_NET_MTU;
	netif->hwaddr_len = 6u;
	(void)memcpy(netif->hwaddr, iwl_net_mac, 6u);
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
	    NETIF_FLAG_LINK_UP;
	netif->output = etharp_output;
	netif->linkoutput = iwl_net_link_output;
	return ERR_OK;
}

/*
 * The bridge's receive seam.  It runs on the driver's receive context, so it
 * only copies and posts: the stack's input call enqueues the frame on the
 * TCP/IP thread (core-locked input is deferred in this build), and nothing here
 * takes the bridge's serializer or transmits.
 */
static void
iwl_net_rx(const uint8_t *frame, size_t len, void *arg)
{
	struct netif *netif = arg;
	struct pbuf *p;

	if (netif == NULL || frame == NULL || len == 0u) {
		iwl_rx_fail++;
		return;
	}
	p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
	if (p == NULL) {
		iwl_rx_fail++;
		return;
	}
#if defined(IWL_NET_TRACE)
	CASE_ANCHOR("iwl7260: net rx pbuf=%p ref=%u len=%u type=%02x%02x\n",
	    (void *)p, (unsigned)p->ref, (unsigned)len,
	    len > 13u ? frame[12] : 0u, len > 13u ? frame[13] : 0u);
#endif
	if (pbuf_take(p, frame, (u16_t)len) != ERR_OK) {
#if defined(IWL_NET_TRACE)
		CASE_ANCHOR("iwl7260: net rx take failed pbuf=%p\n", (void *)p);
#endif
		pbuf_free(p);
		iwl_rx_fail++;
		return;
	}
	{
		err_t rc = netif->input(p, netif);

#if defined(IWL_NET_TRACE)
		CASE_ANCHOR("iwl7260: net rx input rc=%d pbuf=%p ref=%u\n", (int)rc,
		    (void *)p, (unsigned)p->ref);
#endif
		if (rc != ERR_OK) {
			pbuf_free(p);
			iwl_rx_fail++;
			return;
		}
	}
	iwl_rx_frames++;
	iwl_rx_bytes += (unsigned)len;
}

/* --- ICMP ---------------------------------------------------------------- */

/*
 * The raw receive contract has two parts, and both matter:
 *  - the payload a raw pcb is handed still starts at the IP header (the
 *    reference ping parses it the same way), so the ICMP header is at
 *    payload + IP header length;
 *  - a non-zero return says this callback consumed the pbuf (and freed it),
 *    zero says lwIP still owns it and frees it after the raw walk.  A branch
 *    that frees and returns zero frees the same pbuf twice - lwIP's own
 *    `pbuf_free: p->ref > 0` assert found that during this round, one frame
 *    after the first echo reply arrived.
 */
static u8_t
iwl_net_ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
    const ip_addr_t *addr)
{
	struct ip_hdr *iphdr;
	struct icmp_echo_hdr *iecho;
	u16_t hlen;
	u16_t seq;
	uint64_t now;

	(void)arg;
	(void)pcb;
	(void)addr;

	if (p == NULL || p->len < (u16_t)sizeof(struct ip_hdr))
		return 0u;
	iphdr = (struct ip_hdr *)p->payload;
	if (IPH_V(iphdr) != 4u)
		return 0u;
	hlen = IPH_HL_BYTES(iphdr);
	if (p->len < (u16_t)(hlen + sizeof(struct icmp_echo_hdr)))
		return 0u;
	iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);
#if defined(IWL_NET_TRACE)
	CASE_ANCHOR("iwl7260: net icmp type=%u id=%04x want=%04x seq=%u\n",
	    (unsigned)ICMPH_TYPE(iecho), (unsigned)iecho->id,
	    (unsigned)PP_HTONS(IWL_NET_PING_ID),
	    (unsigned)lwip_ntohs(iecho->seqno));
#endif
	if ((ICMPH_TYPE(iecho) != ICMP_ER) ||
	    (iecho->id != PP_HTONS(IWL_NET_PING_ID)))
		return 0u;

	iwl_ping_seen++;
	seq = lwip_ntohs(iecho->seqno);
	if ((seq != 0u) && (seq <= IWL_NET_PING_N) &&
	    (iwl_ping_sent_us[seq] != 0u)) {
		unsigned rtt_us;

		now = rtos_clock_us();
		rtt_us = (unsigned)(now - iwl_ping_sent_us[seq]);
		if ((iwl_ping_rtt_min_us == 0u) || (rtt_us < iwl_ping_rtt_min_us))
			iwl_ping_rtt_min_us = rtt_us;
		if (rtt_us > iwl_ping_rtt_max_us)
			iwl_ping_rtt_max_us = rtt_us;
		iwl_ping_rx++;
	}
	pbuf_free(p);
	return 1u;	/* consumed: we freed it above */
}

static void
iwl_net_ping_send(u16_t seq)
{
	struct pbuf *p;
	struct icmp_echo_hdr *iecho;
	u16_t len = (u16_t)(sizeof(struct icmp_echo_hdr) + 8u);

	p = pbuf_alloc(PBUF_IP, len, PBUF_RAM);
	if (p == NULL)
		return;
	iecho = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = PP_HTONS(IWL_NET_PING_ID);
	iecho->seqno = lwip_htons(seq);
	(void)memset((uint8_t *)p->payload + sizeof(struct icmp_echo_hdr),
	    0xa5, 8u);
	iecho->chksum = inet_chksum(iecho, p->len);

	iwl_ping_sent_us[seq] = rtos_clock_us();
	LOCK_TCPIP_CORE();
	if (raw_sendto(iwl_ping_pcb, p, &iwl_ap_ip) == ERR_OK)
		iwl_ping_tx++;
	UNLOCK_TCPIP_CORE();
	pbuf_free(p);
}

static int
iwl_net_ping(unsigned int count)
{
	unsigned i;

	for (i = 1u; i <= count; i++) {
		unsigned before = iwl_ping_rx;
		unsigned waited;

		iwl_net_ping_send((u16_t)i);
		for (waited = 0u;
		     (waited < IWL_NET_PING_WAIT_MS) && (iwl_ping_rx == before);
		     waited += 20u)
			(void)osDelay(20u);
		if (i < count)
			(void)osDelay(IWL_NET_PING_GAP_MS);
	}
	return (iwl_ping_rx >= IWL_NET_PING_MIN_RX) ? 0 : -1;
}

/* --- iperf --------------------------------------------------------------- */

static volatile int iwl_down_done;
static volatile int iwl_up_done;
static volatile unsigned iwl_down_bytes, iwl_down_ms, iwl_down_kbit;
static volatile unsigned iwl_up_bytes, iwl_up_ms, iwl_up_kbit;

static void
iwl_net_iperf_report(void *arg, enum lwiperf_report_type type,
    const ip_addr_t *local_addr, u16_t local_port,
    const ip_addr_t *remote_addr, u16_t remote_port,
    u32_t bytes, u32_t ms, u32_t kbitpsec)
{
	(void)arg;
	(void)local_addr;
	(void)local_port;
	(void)remote_addr;
	(void)remote_port;

	CASE_ANCHOR("iwl7260: net iperf report type=%d bytes=%lu ms=%lu kbit=%lu\n",
	    (int)type, (unsigned long)bytes, (unsigned long)ms,
	    (unsigned long)kbitpsec);
	if (type == LWIPERF_TCP_DONE_SERVER) {
		iwl_down_bytes = (unsigned)bytes;
		iwl_down_ms = (unsigned)ms;
		iwl_down_kbit = (unsigned)kbitpsec;
		iwl_down_done = 1;
	} else if (type == LWIPERF_TCP_DONE_CLIENT) {
		iwl_up_bytes = (unsigned)bytes;
		iwl_up_ms = (unsigned)ms;
		iwl_up_kbit = (unsigned)kbitpsec;
		iwl_up_done = 1;
	}
}

static int
iwl_net_iperf_down(void)
{
	void *server;
	unsigned waited;

	iwl_down_done = 0;
	LOCK_TCPIP_CORE();
	server = lwiperf_start_tcp_server_default(iwl_net_iperf_report, NULL);
	UNLOCK_TCPIP_CORE();
	if (server == NULL) {
		CASE_ANCHOR("iwl7260: net FAIL iperf server\n");
		return -1;
	}
	/*
	 * The phase anchor tells the peer driver which command this phase wants;
	 * it is printed only after the listener exists, so the peer's connect
	 * cannot be refused by a server that is not up yet.
	 */
	CASE_ANCHOR("net: phase 1 begin window_ms=%u name=down\n",
	    IWL_NET_DOWN_WAIT_MS);
	for (waited = 0u; (waited < IWL_NET_DOWN_WAIT_MS) && (iwl_down_done == 0);
	     waited += IWL_NET_WAIT_STEP_MS)
		(void)osDelay(IWL_NET_WAIT_STEP_MS);

	CASE_ANCHOR("iwl7260: net iperf down done=%d bytes=%u ms=%u kbit=%u\n",
	    iwl_down_done, iwl_down_bytes, iwl_down_ms, iwl_down_kbit);
	return ((iwl_down_done != 0) && (iwl_down_bytes != 0u)) ? 0 : -1;
}

static int
iwl_net_iperf_up(void)
{
	void *client;
	unsigned waited;

	iwl_up_done = 0;
	/* The peer's iperf2 server listens on the AP host from round start, so
	 * this phase only has to start the board's client. */
	CASE_ANCHOR("net: phase 2 begin window_ms=%u name=tcpup\n",
	    IWL_NET_UP_WAIT_MS);
	LOCK_TCPIP_CORE();
	client = lwiperf_start_tcp_client_default(&iwl_ap_ip,
	    iwl_net_iperf_report, NULL);
	UNLOCK_TCPIP_CORE();
	if (client == NULL) {
		CASE_ANCHOR("iwl7260: net FAIL iperf client\n");
		return -1;
	}
	for (waited = 0u; (waited < IWL_NET_UP_WAIT_MS) && (iwl_up_done == 0);
	     waited += IWL_NET_WAIT_STEP_MS)
		(void)osDelay(IWL_NET_WAIT_STEP_MS);

	CASE_ANCHOR("iwl7260: net iperf up done=%d bytes=%u ms=%u kbit=%u\n",
	    iwl_up_done, iwl_up_bytes, iwl_up_ms, iwl_up_kbit);
	return ((iwl_up_done != 0) && (iwl_up_bytes != 0u)) ? 0 : -1;
}

/* --- the case ------------------------------------------------------------ */

static void
iwl_net_counters_line(void)
{
	struct wlan_bridge_counters c;

	wlan_bridge_stats(&c);
	CASE_ANCHOR("iwl7260: net counters tx=%u txb=%u txfail=%u rx=%u rxr=%u "
	    "rxfail=%u bridge_tx=%u bridge_done=%u bridge_rx=%u\n",
	    iwl_tx_frames, iwl_tx_bytes, iwl_tx_fail, iwl_rx_frames,
	    iwl_rx_bytes, iwl_rx_fail, c.data_tx, c.tx_completes, c.rx_frames);
}

int
case_iwl7260_net_run(void)
{
	ip4_addr_t ip, mask, gw;
	struct wlan_bridge_counters c;
	int ping_ok;
	int down_ok;
	int up_ok;

	wlan_bridge_stats(&c);
	if (c.attached == 0u) {
		CASE_ANCHOR("iwl7260: net FAIL not attached\n");
		return 1;
	}
	if (wlan_bridge_state() != IWL_NET_RUN_STATE) {
		CASE_ANCHOR("iwl7260: net FAIL not associated (state=%d)\n",
		    wlan_bridge_state());
		return 1;
	}
	if (wlan_bridge_hwaddr(iwl_net_mac) != 0) {
		CASE_ANCHOR("iwl7260: net FAIL no hardware address\n");
		return 1;
	}
	if (iwl_net_stack_start() != 0) {
		CASE_ANCHOR("iwl7260: net FAIL tcpip init\n");
		return 1;
	}

	IP4_ADDR(&ip, IWL_NET_IP_A, IWL_NET_IP_B, IWL_NET_IP_C, IWL_NET_IP_US);
	IP4_ADDR(&mask, 255u, 255u, 255u, 0u);
	IP4_ADDR(&gw, IWL_NET_IP_A, IWL_NET_IP_B, IWL_NET_IP_C, IWL_NET_IP_AP);
	ip_addr_copy(iwl_ap_ip, gw);

	LOCK_TCPIP_CORE();
	if (netif_add(&iwl_netif, &ip, &mask, &gw, NULL, iwl_net_if_init,
	    tcpip_input) == NULL) {
		UNLOCK_TCPIP_CORE();
		CASE_ANCHOR("iwl7260: net FAIL netif add\n");
		return 1;
	}
	netif_set_default(&iwl_netif);
	netif_set_up(&iwl_netif);
	UNLOCK_TCPIP_CORE();
	/* The receive seam is registered only after the netif exists: a frame
	 * that arrived between the two would be dropped by the seam's own
	 * netif-NULL guard. */
	wlan_bridge_set_data_rx(iwl_net_rx, &iwl_netif);

	CASE_ANCHOR("iwl7260: net up ip=%u.%u.%u.%u/24 mac=%02x:%02x:%02x:%02x:"
	    "%02x:%02x mtu=%u\n", IWL_NET_IP_A, IWL_NET_IP_B, IWL_NET_IP_C,
	    IWL_NET_IP_US, iwl_net_mac[0], iwl_net_mac[1], iwl_net_mac[2],
	    iwl_net_mac[3], iwl_net_mac[4], iwl_net_mac[5], IWL_NET_MTU);

	/* The AP's ARP entry must exist before the first counted echo: lwIP
	 * drops an unresolved-ARP packet without queueing it. */
	iwl_ping_pcb = raw_new(IP_PROTO_ICMP);
	if (iwl_ping_pcb == NULL) {
		CASE_ANCHOR("iwl7260: net FAIL raw pcb\n");
		return 1;
	}
	raw_recv(iwl_ping_pcb, iwl_net_ping_recv, NULL);
	iwl_net_ping_send(0u);
	{
		unsigned waited;

		for (waited = 0u; waited < 2000u; waited += 20u)
			(void)osDelay(20u);
	}

	ping_ok = iwl_net_ping(IWL_NET_PING_N);
	CASE_ANCHOR("iwl7260: net ping tx=%u rx=%u min_ms=%u max_ms=%u\n",
	    iwl_ping_tx, iwl_ping_rx, iwl_ping_rtt_min_us / 1000u,
	    iwl_ping_rtt_max_us / 1000u);

	down_ok = iwl_net_iperf_down();
	up_ok = iwl_net_iperf_up();

	iwl_net_counters_line();
	if ((ping_ok != 0) || (down_ok != 0) || (up_ok != 0)) {
		CASE_ANCHOR("iwl7260: net FAIL ping=%d down=%d up=%d\n",
		    ping_ok, down_ok, up_ok);
		return 1;
	}
	CASE_ANCHOR("iwl7260: net PASS\n");
	return 0;
}
