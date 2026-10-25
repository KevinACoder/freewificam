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

#include "lkpinetif.h"

#include <string.h>

#define WLAN_NETIF_IP_A		192u
#define WLAN_NETIF_IP_B		168u
#define WLAN_NETIF_IP_C		99u
#define WLAN_NETIF_IP_D		2u
#define WLAN_NETIF_GW_D		1u

#define WLAN_PING_ID		0x8189u
#define WLAN_PING_WAIT_MS	500u

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
	/* Give the join's TX/EDCA state one settle window before the first
	 * counted echo; the P3 device round measured the first packet lost
	 * when the counted run started immediately after the ARP entry. */
	(void)osDelay(WLAN_PING_WAIT_MS);

	/* The settle window may let the driver enter its idle power state; one
	 * more throwaway immediately before counting proves the path is awake. */
	wlan_ping_rx = 0u;
	wlan_ping_send(pcb, &peer, 0u);
	{
		unsigned int waited = 0u;

		while ((wlan_ping_rx == 0u) && (waited < WLAN_PING_WAIT_MS)) {
			(void)osDelay(10u);
			waited += 10u;
		}
	}

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
	}

	raw_remove(pcb);
	if (rx != NULL) {
		*rx = wlan_ping_rx;
	}
	CASE_ANCHOR("wlan: ping tx=%u rx=%u\n", count, wlan_ping_rx);
	return (wlan_ping_rx != 0u) ? 0 : -1;
}
