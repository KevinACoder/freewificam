/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net.c
 * @brief  Case net: lwIP over the board's GMAC, quiet-link checks and a TCP
 *         baseline in both directions.
 *
 * Topology (the lab's reliable port, per the board's MAC contract): gmac1 at
 * fe010000, MAC 02:e4:a5:35:68:01, static 192.168.0.200/24, peer the Raspberry
 * Pi at 192.168.0.17 on the same switch.  Static addressing keeps the case
 * independent of a DHCP lease that the lab's other lines also hold.
 *
 * What is claimed as evidence, in order:
 *   net: link ...             PHY autoneg result and the MAC's configured mode
 *   net: addr ...             the address and the contract MAC actually read back
 *   net: ping tx/rx           ten echo requests, counted at both ends (the peer
 *                             side is counted by the board runner)
 *   net: arp ...              the peer's resolved entry, read from the stack
 *   net: iperf down ...       a peer-to-board TCP session (the board serves)
 *   net: iperf up ...         a board-to-peer TCP session (the board initiates)
 *
 * Every wait is bounded: a missing peer fails the case, it does not hang the
 * image.  Throughput is recorded as a baseline only, with no target.
 */

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/etharp.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/apps/lwiperf.h"

#include "dwc_eqos.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"

#include "ethnetif.h"

#define NET_PORT		1u
#define NET_IP_A		192u
#define NET_IP_B		168u
#define NET_IP_C		0u
#define NET_IP_D		200u
#define NET_MASK_A		255u
#define NET_MASK_B		255u
#define NET_MASK_C		255u
#define NET_MASK_D		0u
#define NET_GW_D		1u
#define PEER_IP_D		17u

#define PING_COUNT		10u
#define PING_INTERVAL_MS	200u
#define IPERF_DOWN_WINDOW_MS	40000u
#define IPERF_UP_WAIT_MS	15000u
#define LINK_WAIT_MS		10000u
#define PING_ID			0x5a5au

static struct netif netif;
static osSemaphoreId_t rx_sem;
static osSemaphoreId_t stack_ready;
static osThreadId_t rx_thread;
static ip_addr_t peer;
static struct raw_pcb *ping_pcb;
static volatile unsigned ping_tx;
static volatile unsigned ping_rx;
static volatile unsigned ping_seen;	/* every ICMP packet the pcb was handed */
static int net_up;

/* iperf sessions: the board serves the downlink and initiates the uplink. */
static volatile unsigned down_bytes;
static volatile unsigned down_ms;
static volatile unsigned down_kbit;
static volatile int down_done;
static volatile unsigned up_bytes;
static volatile unsigned up_ms;
static volatile unsigned up_kbit;
static volatile int up_done;

static void iperf_report(void *arg, enum lwiperf_report_type type,
			 const ip_addr_t *local_addr, u16_t local_port,
			 const ip_addr_t *remote_addr, u16_t remote_port,
			 u32_t bytes, u32_t ms, u32_t kbitpsec)
{
	(void)arg;
	(void)local_addr;
	(void)local_port;
	(void)remote_addr;
	(void)remote_port;

	CASE_ANCHOR("net: iperf report type=%d bytes=%lu ms=%lu kbit=%lu\n",
		    (int)type, (unsigned long)bytes, (unsigned long)ms,
		    (unsigned long)kbitpsec);

	if (type == LWIPERF_TCP_DONE_SERVER) {
		down_bytes = (unsigned)bytes;
		down_ms = (unsigned)ms;
		down_kbit = (unsigned)kbitpsec;
		down_done = 1;
	} else if (type == LWIPERF_TCP_DONE_CLIENT) {
		up_bytes = (unsigned)bytes;
		up_ms = (unsigned)ms;
		up_kbit = (unsigned)kbitpsec;
		up_done = 1;
	}
}

/* GMAC ISR context: frames landed in the driver's staging ring. */
static void net_rx_notify(unsigned int index)
{
	(void)index;
	(void)osSemaphoreRelease(rx_sem);
}

static void net_rx_thread(void *arg)
{
	(void)arg;

	for (;;) {
		(void)osSemaphoreAcquire(rx_sem, osWaitForever);
		/* The ISR hands the sweep to this thread: the watchdog re-arms
		 * the ring and re-pokes the line before the drain.  Skipping it
		 * leaves the first interrupt's work done but the ring disarmed. */
		dwc_eqos_watchdog(NET_PORT);
		(void)ethnetif_poll(&netif);
	}
}

static void net_service_thread(void *arg)
{
	(void)arg;

	for (;;) {
		(void)osDelay(100u);
		dwc_eqos_watchdog(NET_PORT);
	}
}

static void stack_done(void *arg)
{
	(void)arg;
	CASE_ANCHOR("net: stack ready lwip=%s\n", LWIP_VERSION_STRING);
	(void)osSemaphoreRelease(stack_ready);
}

static int stack_up(void)
{
	stack_ready = osSemaphoreNew(1u, 0u, NULL);
	if (stack_ready == NULL) {
		return -1;
	}

	tcpip_init(stack_done, NULL);

	if (osSemaphoreAcquire(stack_ready, 5000u) != osOK) {
		return -1;
	}

	return 0;
}

/* --- ping ------------------------------------------------------------------- */

/* A raw pcb is handed the packet from its IP header on (lwIP's raw_input
 * reads the protocol out of that header and passes the same pbuf through), so
 * the ICMP header starts after the IP header the sender wrote. */
static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
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

	ping_seen++;
	iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);

	if ((ICMPH_TYPE(iecho) == ICMP_ER) &&
	    (iecho->id == PP_HTONS(PING_ID))) {
		ping_rx++;
		pbuf_free(p);
		return 1;
	}

	return 0;
}

static void ping_send(u16_t seq)
{
	struct pbuf *p;
	struct icmp_echo_hdr *iecho;
	uint8_t *payload;
	u16_t len = (u16_t)(sizeof(struct icmp_echo_hdr) + 8u);
	unsigned i;

	p = pbuf_alloc(PBUF_IP, len, PBUF_RAM);
	if (p == NULL) {
		return;
	}

	iecho = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = PP_HTONS(PING_ID);
	iecho->seqno = lwip_htons(seq);

	payload = (uint8_t *)p->payload + sizeof(struct icmp_echo_hdr);
	for (i = 0u; i < 8u; i++) {
		payload[i] = (uint8_t)(0xa5u ^ (uint8_t)(seq + i));
	}

	iecho->chksum = inet_chksum(iecho, p->len);

	if (raw_sendto(ping_pcb, p, &peer) == ERR_OK) {
		ping_tx++;
	}

	pbuf_free(p);
}

/* --- bring-up --------------------------------------------------------------- */

static int arp_lookup(uint8_t mac[6]);

static int gmac_up(void)
{
	eqos_link_info_t link;
	uint32_t configure;

	if (dwc_eqos_initialize(NET_PORT, eqos_board_plat(NET_PORT)) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL gmac initialize\n");
		return -1;
	}
	if (dwc_eqos_power_control(NET_PORT, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL gmac power\n");
		return -1;
	}
	if (rtl8211f_initialize(NET_PORT, (void *)(uintptr_t)NET_PORT,
				eqos_board_mdio_read,
				eqos_board_mdio_write) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL phy initialize\n");
		return -1;
	}
	if (rtl8211f_power_control(NET_PORT, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL phy power\n");
		return -1;
	}
	(void)rtl8211f_set_interface(NET_PORT, EQOS_INTERFACE_RGMII);
	if (rtl8211f_set_mode(NET_PORT,
			      EQOS_PHY_AUTO_NEGOTIATE |
			      EQOS_PHY_SPEED_1G) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL phy autoneg\n");
		return -1;
	}

	link = rtl8211f_get_link_info(NET_PORT);
	configure = link.speed | link.duplex;
	(void)dwc_eqos_control(NET_PORT, DWC_EQOS_CONFIGURE, configure);
	(void)dwc_eqos_control(NET_PORT, DWC_EQOS_CONTROL_TX, 1);
	(void)dwc_eqos_control(NET_PORT, DWC_EQOS_CONTROL_RX, 1);

	CASE_ANCHOR("net: link speed=%lu duplex=%lu\n",
		    (unsigned long)link.speed, (unsigned long)link.duplex);

	return 0;
}

static int line_up(void)
{
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gw;
	uint8_t mac[6];

	if (net_up) {
		return 0;
	}

	if (gmac_up() != 0) {
		return -1;
	}

	rx_sem = osSemaphoreNew(0xFFFFu, 0u, NULL);
	if (rx_sem == NULL) {
		CASE_ANCHOR("net: FAIL rx semaphore\n");
		return -1;
	}
	dwc_eqos_set_rx_notify(net_rx_notify);

	rx_thread = osThreadNew(net_rx_thread, NULL,
				&(const osThreadAttr_t){ .name = "gmac-rx",
							 .priority = osPriorityNormal,
							 .stack_size = 4096U });
	if (rx_thread == NULL) {
		CASE_ANCHOR("net: FAIL rx thread\n");
		return -1;
	}

	if (osThreadNew(net_service_thread, NULL,
			&(const osThreadAttr_t){ .name = "gmac-svc",
						 .priority = osPriorityLow,
						 .stack_size = 2048U }) == NULL) {
		CASE_ANCHOR("net: FAIL service thread\n");
		return -1;
	}

	if (stack_up() != 0) {
		CASE_ANCHOR("net: FAIL stack bring-up\n");
		return -1;
	}

	IP4_ADDR(&ip, NET_IP_A, NET_IP_B, NET_IP_C, NET_IP_D);
	IP4_ADDR(&mask, NET_MASK_A, NET_MASK_B, NET_MASK_C, NET_MASK_D);
	IP4_ADDR(&gw, NET_IP_A, NET_IP_B, NET_IP_C, NET_GW_D);
	IP4_ADDR(&peer, NET_IP_A, NET_IP_B, NET_IP_C, PEER_IP_D);

	LOCK_TCPIP_CORE();
	ethnetif_add(&netif, NET_PORT, &ip, &mask, &gw);
	if (lwiperf_start_tcp_server_default(iperf_report, NULL) == NULL) {
		CASE_ANCHOR("net: FAIL lwiperf server\n");
	}
	UNLOCK_TCPIP_CORE();

	(void)dwc_eqos_get_mac_address(NET_PORT, mac);
	CASE_ANCHOR("net: addr %lu.%lu.%lu.%lu/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    (unsigned long)NET_IP_A, (unsigned long)NET_IP_B,
		    (unsigned long)NET_IP_C, (unsigned long)NET_IP_D,
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	net_up = 1;
	return 0;
}

/* Wait for the PHY's autoneg result, bounded. */
static int wait_link(void)
{
	unsigned waited = 0u;

	while (waited < LINK_WAIT_MS) {
		eqos_link_info_t link = rtl8211f_get_link_info(NET_PORT);

		if (link.speed != 0u) {
			CASE_ANCHOR("net: link up speed=%lu duplex=%lu\n",
				    (unsigned long)link.speed,
				    (unsigned long)link.duplex);
			return 0;
		}
		(void)osDelay(100u);
		waited += 100u;
	}

	CASE_ANCHOR("net: FAIL link down after %ums\n", LINK_WAIT_MS);
	return -1;
}

static int arp_lookup(uint8_t mac[6])
{
	const ip4_addr_t *addr = NULL;
	struct eth_addr *eth = NULL;
	int rc;

	LOCK_TCPIP_CORE();
	rc = etharp_find_addr(&netif, &peer, &eth, &addr);
	UNLOCK_TCPIP_CORE();

	if ((rc < 0) || (eth == NULL)) {
		return -1;
	}
	mac[0] = eth->addr[0];
	mac[1] = eth->addr[1];
	mac[2] = eth->addr[2];
	mac[3] = eth->addr[3];
	mac[4] = eth->addr[4];
	mac[5] = eth->addr[5];
	return 0;
}

/* --- the case --------------------------------------------------------------- */

int case_net_run(void)
{
	uint8_t mac[6];
	unsigned i;
	unsigned waited;
	unsigned arp_waited;
	int ok = 1;
	void *up_client;

	if (line_up() != 0) {
		CASE_ANCHOR("net: FAIL bring-up\n");
		return -1;
	}

	if (wait_link() != 0) {
		return -1;
	}

	ping_pcb = raw_new(IP_PROTO_ICMP);
	if (ping_pcb == NULL) {
		CASE_ANCHOR("net: FAIL raw pcb\n");
		return -1;
	}
	raw_recv(ping_pcb, ping_recv, NULL);

	/* Warm-up: one throwaway echo forces the ARP resolution, and the first
	 * counted packets are not sent into an unresolved entry (lwIP drops
	 * them without a queue). */
	ping_tx = 0u;
	ping_rx = 0u;
	ping_seen = 0u;
	ping_send(0u);
	for (waited = 0u; (waited < 2000u) && (arp_lookup(mac) != 0); waited += 20u) {
		(void)osDelay(20u);
	}

	ping_tx = 0u;
	ping_rx = 0u;
	ping_seen = 0u;
	for (i = 1u; i <= PING_COUNT; i++) {
		unsigned before = ping_rx;

		ping_send((u16_t)i);
		for (waited = 0u; (waited < 300u) && (ping_rx == before);
		     waited += 10u) {
			(void)osDelay(10u);
		}
	}

	CASE_ANCHOR("net: ping tx=%u rx=%u of=%u seen=%u\n",
		    ping_tx, ping_rx, PING_COUNT, ping_seen);

	if (ping_rx != PING_COUNT) {
		ok = 0;
	}

	/* The ping above resolved the peer; read the stack's own entry. */
	for (arp_waited = 0u; arp_waited < 500u; arp_waited += 20u) {
		if (arp_lookup(mac) == 0) {
			CASE_ANCHOR("net: arp %lu.%lu.%lu.%lu -> %02x:%02x:%02x:%02x:%02x:%02x\n",
				    (unsigned long)NET_IP_A,
				    (unsigned long)NET_IP_B,
				    (unsigned long)NET_IP_C, (unsigned long)PEER_IP_D,
				    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			break;
		}
		(void)osDelay(20u);
	}
	if (arp_waited >= 500u) {
		CASE_ANCHOR("net: FAIL arp entry missing\n");
		ok = 0;
	}

	/* Downlink window: the peer pings and then drives a TCP session into the
	 * server started above. */
	down_done = 0;
	CASE_ANCHOR("net: iperf ready port=5001 window_ms=%u\n",
		    (unsigned)IPERF_DOWN_WINDOW_MS);

	for (waited = 0u; waited < IPERF_DOWN_WINDOW_MS; waited += 50u) {
		(void)osDelay(50u);
		if (down_done != 0) {
			break;
		}
	}

	CASE_ANCHOR("net: iperf down done=%d bytes=%u ms=%u kbit=%u\n",
		    down_done, down_bytes, down_ms, down_kbit);
	if ((down_done == 0) || (down_bytes == 0u)) {
		ok = 0;
	}

	/* Uplink: the board initiates the session, so the peer only needs its
	 * server listening. */
	up_done = 0;
	up_client = NULL;
	LOCK_TCPIP_CORE();
	up_client = lwiperf_start_tcp_client_default(&peer, iperf_report, NULL);
	UNLOCK_TCPIP_CORE();

	CASE_ANCHOR("net: iperf up start=%s\n", (up_client != NULL) ? "ok" : "fail");
	if (up_client == NULL) {
		ok = 0;
	}

	for (waited = 0u; (waited < IPERF_UP_WAIT_MS) && (up_done == 0);
	     waited += 100u) {
		(void)osDelay(100u);
	}

	CASE_ANCHOR("net: iperf up done=%d bytes=%u ms=%u kbit=%u\n",
		    up_done, up_bytes, up_ms, up_kbit);
	if ((up_done == 0) || (up_bytes == 0u)) {
		ok = 0;
	}

	raw_remove(ping_pcb);
	ping_pcb = NULL;

	CASE_ANCHOR("net: %s\n", ok ? "PASS" : "FAIL");

	return ok ? 0 : -1;
}