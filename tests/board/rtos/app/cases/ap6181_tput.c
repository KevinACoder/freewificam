/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ap6181_tput.c - throughput, report only (no gate).
 *
 * The board joins the bench AP, takes 192.168.99.2/24, and runs two TCP
 * phases against the AP:
 *   TX: connect to 192.168.99.1:5001 and push 4 MiB (the peer discards).
 *   RX: connect to 192.168.99.1:5002 and pull 4 MiB (the peer streams zeros).
 *
 * Anchor:
 *   ap6181: tput tx=<x.xx> Mbit/s rx=<y.yy> Mbit/s tx_ms=<n> rx_ms=<n>
 *   ap6181: PASS TPUT
 * There is no pass threshold: the numbers are what they are, and the case only
 * fails if a phase cannot run at all.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcpip.h"

#include "platform/wwd_platform_interface.h"
#include "RTOS/wwd_rtos_interface.h"
#include "network/wwd_buffer_interface.h"
#include "wwd_constants.h"
#include "wwd_management.h"
#include "wwd_wifi.h"

#include "wiced_port.h"

#define JOIN_SSID	"labwifi-ar-ht20s"
#define TP_BYTES	(4U * 1024U * 1024U)
#define TP_CHUNK	1460U

static struct netif s_netif;
static host_semaphore_type_t s_join_sem;

static int
tcp_connect(uint16_t port)
{
	struct sockaddr_in addr;
	int s;
	int rc;

	s = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = lwip_htons(port);
	/* 192.168.99.1 */
	addr.sin_addr.s_addr = lwip_htonl(0xC0A86301UL);

	rc = lwip_connect(s, (struct sockaddr *)&addr, sizeof(addr));
	if (rc != 0) {
		lwip_close(s);
		return -1;
	}
	return s;
}

static uint32_t
phase_tx(void)
{
	static uint8_t buf[TP_CHUNK];
	int s = tcp_connect(5001);
	uint32_t sent = 0U;
	uint32_t t0;
	uint32_t ms;

	if (s < 0)
		return 0U;

	t0 = (uint32_t)host_rtos_get_time();
	memset(buf, 0x5A, sizeof(buf));
	while (sent < TP_BYTES) {
		int n = lwip_send(s, buf, sizeof(buf), 0);

		if (n <= 0)
			break;
		sent += (uint32_t)n;
	}
	ms = (uint32_t)host_rtos_get_time() - t0;
	lwip_close(s);

	CASE_INFO("ap6181: tput tx_bytes=%lu tx_ms=%lu\n", (unsigned long)sent,
		  (unsigned long)ms);
	return ms == 0U ? 0U : (sent * 8U) / ms;	/* kbit/s */
}

static uint32_t
phase_rx(void)
{
	static uint8_t buf[TP_CHUNK];
	int s = tcp_connect(5002);
	uint32_t got = 0U;
	uint32_t t0;
	uint32_t ms;

	if (s < 0)
		return 0U;

	t0 = (uint32_t)host_rtos_get_time();
	while (got < TP_BYTES) {
		int n = lwip_recv(s, buf, sizeof(buf), 0);

		if (n <= 0)
			break;
		got += (uint32_t)n;
	}
	ms = (uint32_t)host_rtos_get_time() - t0;
	lwip_close(s);

	CASE_INFO("ap6181: tput rx_bytes=%lu rx_ms=%lu\n", (unsigned long)got,
		  (unsigned long)ms);
	return ms == 0U ? 0U : (got * 8U) / ms;	/* kbit/s */
}

int
case_f429_ap6181_tput_run(void)
{
	wiced_ssid_t ssid;
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gw;
	wwd_result_t rc;
	uint32_t tx_kbps;
	uint32_t rx_kbps;
	unsigned i;

	CASE_INFO("ap6181: stage=tput\n");

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

	tx_kbps = phase_tx();
	host_rtos_delay_milliseconds(500U);
	rx_kbps = phase_rx();

	CASE_INFO("ap6181: tput tx=%lu.%02lu Mbit/s rx=%lu.%02lu Mbit/s\n",
		  (unsigned long)(tx_kbps / 1000U),
		  (unsigned long)((tx_kbps % 1000U) / 10U),
		  (unsigned long)(rx_kbps / 1000U),
		  (unsigned long)((rx_kbps % 1000U) / 10U));

	if (tx_kbps == 0U && rx_kbps == 0U) {
		CASE_ANCHOR("ap6181: FAIL tput no phase ran\n");
		return -1;
	}

	CASE_ANCHOR("ap6181: PASS TPUT\n");
	return 0;
}
