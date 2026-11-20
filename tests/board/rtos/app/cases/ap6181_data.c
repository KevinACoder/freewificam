/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ap6181_data.c - G4: the data plane, 20 ICMP echo requests and 20 replies.
 *
 * Anchor (one line, greppable):
 *   ap6181: PASS DATA
 * Above it: the join result, the netif address, and "ap6181: icmp sent=<n>
 * recv=<n>".  The gate needs all twenty back; a link that associates but loses
 * frames is not this rung.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/ip.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4_addr.h"
#include "lwip/timeouts.h"
#include "lwip/tcpip.h"

#include "platform/wwd_platform_interface.h"
#include "RTOS/wwd_rtos_interface.h"
#include "network/wwd_buffer_interface.h"
#include "wwd_constants.h"
#include "wwd_management.h"
#include "wwd_wifi.h"

#include "wiced_port.h"

#define JOIN_SSID	"labwifi-ar-ht20s"
#define PING_COUNT	20U
#define PING_LEN	32U
#define PING_GAP_MS	200U

static struct netif s_netif;
static host_semaphore_type_t s_join_sem;
static volatile uint32_t s_replies;
static uint16_t s_seq;

static u8_t
ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
	struct ip_hdr *iph;
	struct icmp_echo_hdr *iecho;

	(void)arg;
	(void)pcb;
	(void)addr;
	if (p == NULL)
		return 0;

	iph = (struct ip_hdr *)p->payload;
	if (IPH_PROTO(iph) == IP_PROTO_ICMP && p->len >= IPH_HL_BYTES(iph) + 8U) {
		iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload +
						 IPH_HL_BYTES(iph));
		if (ICMPH_TYPE(iecho) == ICMP_ER &&
		    lwip_ntohs(iecho->id) == 0x4336U)
			s_replies++;
	}
	pbuf_free(p);
	return 1;	/* the echo reply is ours; do not pass it on */
}

static void
ping_once(struct raw_pcb *pcb, const ip_addr_t *dst)
{
	struct pbuf *p;
	struct icmp_echo_hdr *iecho;
	uint16_t i;

	p = pbuf_alloc(PBUF_IP, (u16_t)(sizeof(*iecho) + PING_LEN), PBUF_RAM);
	if (p == NULL)
		return;

	iecho = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = lwip_htons(0x4336U);
	iecho->seqno = lwip_htons(++s_seq);
	for (i = 0U; i < PING_LEN; i++)
		((u8_t *)iecho)[sizeof(*iecho) + i] = (u8_t)('A' + (i % 26U));
	iecho->chksum = inet_chksum(iecho, p->len);

	LOCK_TCPIP_CORE();
	(void)raw_sendto(pcb, p, dst);
	UNLOCK_TCPIP_CORE();
	pbuf_free(p);
}

int
case_f429_ap6181_data_run(void)
{
	wiced_ssid_t ssid;
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gw;
	ip_addr_t dst;
	struct raw_pcb *pcb;
	wwd_result_t rc;
	unsigned i;

	CASE_INFO("ap6181: stage=data\n");

	/* The data stage is the one shape that needs the TCP/IP thread: its
	 * netif input is tcpip_input, so the stack starts here (and not through
	 * ap6181_lwip_core_init, which would initialise the core twice). */
	if (ap6181_lwip_stack_start() != 0) {
		CASE_ANCHOR("ap6181: FAIL lwip stack\n");
		return -1;
	}
	(void)wwd_buffer_init(NULL);
	rc = wwd_management_wifi_on(WICED_COUNTRY_AUSTRALIA);
	CASE_INFO("ap6181: wifi_on rc=%d\n", (int)rc);
	if (rc != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL wifi_on rc=%d\n", (int)rc);
		return -1;
	}

	if (host_rtos_init_semaphore(&s_join_sem) != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL join semaphore\n");
		return -1;
	}
	ssid.length = (uint8_t)(sizeof(JOIN_SSID) - 1U);
	memcpy(ssid.value, JOIN_SSID, sizeof(JOIN_SSID) - 1U);
	rc = wwd_wifi_join(&ssid, WICED_SECURITY_OPEN, NULL, 0U, &s_join_sem,
			   WWD_STA_INTERFACE);
	CASE_INFO("ap6181: join rc=%d\n", (int)rc);
	if (rc != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL join rc=%d\n", (int)rc);
		return -1;
	}
	for (i = 0U; i < 50U; i++) {
		if (wwd_wifi_is_ready_to_transceive(WWD_STA_INTERFACE) ==
		    WWD_SUCCESS)
			break;
		host_rtos_delay_milliseconds(100U);
	}
	CASE_INFO("ap6181: link wait=%ums\n", i * 100U);
	if (i == 50U) {
		CASE_ANCHOR("ap6181: FAIL link down\n");
		return -1;
	}

	IP_ADDR4(&ip, 192, 168, 99, 2);
	IP_ADDR4(&mask, 255, 255, 255, 0);
	IP_ADDR4(&gw, 192, 168, 99, 1);
	if (netif_add(&s_netif, &ip, &mask, &gw, (void *)WWD_STA_INTERFACE,
		      ap6181_netif_init, tcpip_input) == NULL) {
		CASE_ANCHOR("ap6181: FAIL netif_add\n");
		return -1;
	}
	netif_set_default(&s_netif);
	netif_set_up(&s_netif);
	CASE_INFO("ap6181: netif ip=%s\n", ip4addr_ntoa(netif_ip4_addr(&s_netif)));

	pcb = raw_new(IP_PROTO_ICMP);
	if (pcb == NULL) {
		CASE_ANCHOR("ap6181: FAIL raw pcb\n");
		return -1;
	}
	raw_recv(pcb, ping_recv, NULL);

	IP_ADDR4(&dst, 192, 168, 99, 1);
	for (i = 0U; i < PING_COUNT; i++) {
		ping_once(pcb, &dst);
		sys_msleep(PING_GAP_MS);
	}
	/* Give the last replies a moment to arrive. */
	sys_msleep(500U);

	CASE_INFO("ap6181: icmp sent=%u recv=%lu tx=%lu buffail=%lu\n",
		  PING_COUNT, (unsigned long)s_replies,
		  (unsigned long)ap6181_stats.tx_frames,
		  (unsigned long)ap6181_stats.buffer_fail);

	raw_remove(pcb);

	if (s_replies < PING_COUNT) {
		CASE_ANCHOR("ap6181: FAIL icmp recv=%lu want=%u\n",
			    (unsigned long)s_replies, PING_COUNT);
		return -1;
	}

	CASE_ANCHOR("ap6181: PASS DATA\n");
	return 0;
}
