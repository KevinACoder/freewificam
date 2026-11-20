/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * pi_net.c - the AP6181/lwIP half of the F429 -> Pi stream.
 *
 * This file is compiled in the WICED world (build/wiced.mk) because it drives
 * the module's own driver and lwIP; the camera case never includes a WICED
 * header.  The WICED driver is the same first-party port the station gates
 * use; this file adds the address acquisition and the two TCP senders.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "cases.h"

#include "pi_net.h"
#include "fwc_proto.h"

#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/ip.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet_chksum.h"
#include "lwip/timeouts.h"
#include "lwip/tcpip.h"

#include "platform/wwd_platform_interface.h"
#include "RTOS/wwd_rtos_interface.h"
#include "network/wwd_buffer_interface.h"
#include "wwd_constants.h"
#include "wwd_management.h"
#include "wwd_wifi.h"

#include "wiced_port.h"

#ifndef CONFIG_STREAM_SSID
#define CONFIG_STREAM_SSID "labwifi-ar-ht20s"
#endif
#ifndef CONFIG_STREAM_DHCP
#define CONFIG_STREAM_DHCP 0
#endif
#ifndef CONFIG_STREAM_STATIC_IP
#define CONFIG_STREAM_STATIC_IP "192.168.99.2"
#endif
#ifndef CONFIG_STREAM_PI_IP
#define CONFIG_STREAM_PI_IP "192.168.99.1"
#endif

#define PI_PING_LEN	32U
#define PI_PING_GAP_MS	200U
#define PI_PING_ID	0x4336U

static struct netif s_netif;
static host_semaphore_type_t s_join_sem;
static struct pi_link_info s_info;
static int s_up;
static volatile uint32_t s_ping_replies;
static uint16_t s_ping_seq;

static void
copy_ip(char *dst, size_t dst_len, const char *src)
{
	size_t n = strlen(src);

	if (n >= dst_len)
		n = dst_len - 1U;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static int
parse_pi_ip(ip4_addr_t *ip)
{
	return ip4addr_aton(CONFIG_STREAM_PI_IP, ip) ? 0 : -1;
}

static int
join_once(wiced_ssid_t *ssid, unsigned *ready_ms)
{
	wwd_result_t rc;
	unsigned i;
	uint32_t t0 = sys_now();

	rc = wwd_wifi_join(ssid, WICED_SECURITY_OPEN, NULL, 0U, &s_join_sem,
			   WWD_STA_INTERFACE);
	s_info.join_rc = (int)rc;
	if (rc != WWD_SUCCESS)
		return -1;

	for (i = 0U; i < 100U; i++) {
		if (wwd_wifi_is_ready_to_transceive(WWD_STA_INTERFACE) ==
		    WWD_SUCCESS)
			break;
		host_rtos_delay_milliseconds(100U);
	}
	*ready_ms = (unsigned)(sys_now() - t0);
	if (i == 100U)
		return -1;
	return 0;
}

static int
join_ap(unsigned *join_ms)
{
	wiced_ssid_t ssid;
	wwd_result_t rc;
	unsigned attempt;
	unsigned ready_ms = 0U;

	(void)wwd_buffer_init(NULL);
	rc = wwd_management_wifi_on(WICED_COUNTRY_AUSTRALIA);
	CASE_INFO("pi_net: wifi_on rc=%d t=%lu\n", (int)rc,
		  (unsigned long)sys_now());
	if (rc != WWD_SUCCESS) {
		s_info.join_rc = (int)rc;
		return -1;
	}

	if (host_rtos_init_semaphore(&s_join_sem) != WWD_SUCCESS) {
		s_info.join_rc = -1000;
		return -1;
	}

	memset(&ssid, 0, sizeof(ssid));
	ssid.length = (uint8_t)strlen(CONFIG_STREAM_SSID);
	if (ssid.length > (uint8_t)sizeof(ssid.value))
		ssid.length = (uint8_t)sizeof(ssid.value);
	memcpy(ssid.value, CONFIG_STREAM_SSID, ssid.length);

	/* The AP has been seen to accept the association and then drop it before
	 * the link event; a leave and a second join recovers without a board
	 * reset.  Three attempts is the same budget the gate uses for a bad
	 * boot. */
	for (attempt = 0U; attempt < 3U; attempt++) {
		if (join_once(&ssid, &ready_ms) == 0) {
			*join_ms = ready_ms;
			return 0;
		}
		CASE_INFO("pi_net: join attempt=%u failed rc=%d ready_ms=%u\n",
			  attempt, s_info.join_rc, ready_ms);
		(void)wwd_wifi_leave(WWD_STA_INTERFACE);
		host_rtos_delay_milliseconds(500U);
	}
	*join_ms = ready_ms;
	return -1;
}

static int
netif_start(void)
{
	ip4_addr_t any;
	ip4_addr_t pi;
	ip4_addr_t static_ip;
	ip4_addr_t mask;
	ip4_addr_t gw;
	err_t err;
	unsigned i;

	if (parse_pi_ip(&pi) != 0) {
		CASE_INFO("pi_net: bad pi ip\n");
		return -1;
	}
	ip4_addr_set_zero(&any);

	if (netif_add(&s_netif, &any, &any, &any,
		      (void *)WWD_STA_INTERFACE, ap6181_netif_init,
		      tcpip_input) == NULL) {
		CASE_INFO("pi_net: netif_add failed\n");
		return -1;
	}
	netif_set_default(&s_netif);
	netif_set_up(&s_netif);

#if CONFIG_STREAM_DHCP
	LOCK_TCPIP_CORE();
	err = dhcp_start(&s_netif);
	UNLOCK_TCPIP_CORE();
	CASE_INFO("pi_net: dhcp_start=%d\n", (int)err);
	if (err == ERR_OK) {
		for (i = 0U; i < 200U; i++) {
			if (dhcp_supplied_address(&s_netif) != 0U)
				break;
			sys_msleep(100U);
		}
	}
	if (err == ERR_OK && dhcp_supplied_address(&s_netif) != 0U) {
		s_info.dhcp = 1;
		copy_ip(s_info.ip,
			sizeof(s_info.ip),
			ip4addr_ntoa(netif_ip4_addr(&s_netif)));
		copy_ip(s_info.gw, sizeof(s_info.gw), CONFIG_STREAM_PI_IP);
		return 0;
	}
	CASE_INFO("pi_net: dhcp timeout\n");
	/* The bench AP runs DHCP, but a timeout must not hide the camera.  Fall
	 * back to the address the station gates already proved, and say so. */
	LOCK_TCPIP_CORE();
	dhcp_stop(&s_netif);
	UNLOCK_TCPIP_CORE();
#endif

	if (ip4addr_aton(CONFIG_STREAM_STATIC_IP, &static_ip) == 0) {
		CASE_INFO("pi_net: bad static ip\n");
		return -1;
	}
	IP4_ADDR(&mask, 255, 255, 255, 0);
	ip4_addr_copy(gw, pi);
	LOCK_TCPIP_CORE();
	netif_set_addr(&s_netif, &static_ip, &mask, &gw);
	UNLOCK_TCPIP_CORE();
	s_info.dhcp = 0;
	copy_ip(s_info.ip, sizeof(s_info.ip), CONFIG_STREAM_STATIC_IP);
	copy_ip(s_info.gw, sizeof(s_info.gw), CONFIG_STREAM_PI_IP);
	return 0;
}

int
pi_net_up(struct pi_link_info *out)
{
	unsigned join_ms = 0U;

	if (out == NULL)
		return -1;
	if (s_up) {
		*out = s_info;
		return 0;
	}

	memset(&s_info, 0, sizeof(s_info));
	s_info.join_rc = -1;
	copy_ip(s_info.gw, sizeof(s_info.gw), CONFIG_STREAM_PI_IP);

	if (ap6181_lwip_stack_start() != 0) {
		CASE_INFO("pi_net: lwip stack failed\n");
		goto fail;
	}
	if (join_ap(&join_ms) != 0) {
		CASE_INFO("pi_net: join failed rc=%d\n", s_info.join_rc);
		goto fail;
	}
	s_info.join_ms = join_ms;

	if (netif_start() != 0) {
		CASE_INFO("pi_net: netif start failed\n");
		goto fail;
	}

	s_up = 1;
	{
		unsigned recv = 0U;
		int hrc = pi_net_ping(3U, &recv);

		CASE_INFO("pi_net: health ping recv=%u rc=%d\n", recv, hrc);
	}
	*out = s_info;
	return 0;

fail:
	*out = s_info;
	return -1;
}

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
		    lwip_ntohs(iecho->id) == PI_PING_ID)
			s_ping_replies++;
	}
	pbuf_free(p);
	return 1;
}

static void
ping_once(struct raw_pcb *pcb, const ip_addr_t *dst)
{
	struct pbuf *p;
	struct icmp_echo_hdr *iecho;
	uint16_t i;

	p = pbuf_alloc(PBUF_IP, (u16_t)(sizeof(*iecho) + PI_PING_LEN),
		       PBUF_RAM);
	if (p == NULL)
		return;

	iecho = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = lwip_htons(PI_PING_ID);
	iecho->seqno = lwip_htons(++s_ping_seq);
	for (i = 0U; i < PI_PING_LEN; i++)
		((u8_t *)iecho)[sizeof(*iecho) + i] =
			(u8_t)('A' + (i % 26U));
	iecho->chksum = inet_chksum(iecho, p->len);

	LOCK_TCPIP_CORE();
	(void)raw_sendto(pcb, p, dst);
	UNLOCK_TCPIP_CORE();
	pbuf_free(p);
}

int
pi_net_ping(unsigned count, unsigned *recv)
{
	ip4_addr_t pi;
	ip_addr_t dst;
	struct raw_pcb *pcb;
	unsigned i;

	if (recv == NULL)
		return -1;
	*recv = 0U;
	if (!s_up || parse_pi_ip(&pi) != 0)
		return -1;

	pcb = raw_new(IP_PROTO_ICMP);
	if (pcb == NULL)
		return -1;
	raw_recv(pcb, ping_recv, NULL);
	s_ping_replies = 0U;
	s_ping_seq = 0U;
	ip_addr_copy_from_ip4(dst, pi);

	for (i = 0U; i < count; i++) {
		ping_once(pcb, &dst);
		sys_msleep(PI_PING_GAP_MS);
	}
	/* Give the last reply a moment to arrive. */
	sys_msleep(500U);

	*recv = (unsigned)s_ping_replies;
	raw_remove(pcb);
	return (*recv == count) ? 0 : -1;
}

static int
send_all(int fd, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;
	size_t off = 0U;
	unsigned spins = 0U;

	while (off < len) {
		int n = lwip_send(fd, p + off, len - off, 0);

		if (n > 0) {
			off += (size_t)n;
			spins = 0U;
			continue;
		}
		if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
			if (++spins > 500U) {
				CASE_INFO("pi_net: send timeout off=%lu len=%lu\n",
					  (unsigned long)off, (unsigned long)len);
				return -1;
			}
			sys_msleep(1U);
			continue;
		}
		CASE_INFO("pi_net: send error errno=%d n=%d off=%lu len=%lu\n",
			  errno, n, (unsigned long)off, (unsigned long)len);
		return -1;
	}
	return 0;
}

int
pi_sender_connect(uint16_t port)
{
	struct sockaddr_in addr;
	struct timeval tv;
	fd_set wfds;
	int fd;
	int flags;
	int rc;
	int err = 0;
	socklen_t err_len = sizeof(err);
	int one = 1;

	fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	flags = lwip_fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		flags = 0;
	(void)lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = lwip_htons(port);
	if (ip4addr_aton(CONFIG_STREAM_PI_IP, (ip4_addr_t *)&addr.sin_addr) == 0)
		goto fail;

	rc = lwip_connect(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (rc != 0) {
		if (errno != EINPROGRESS)
			goto fail;
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		rc = lwip_select(fd + 1, NULL, &wfds, NULL, &tv);
		if (rc <= 0)
			goto fail;
		if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err,
				    &err_len) != 0 || err != 0)
			goto fail;
	}

	(void)lwip_fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
	(void)lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	tv.tv_sec = 0;
	tv.tv_usec = 100000;
	(void)lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	return fd;

fail:
	CASE_INFO("pi_net: connect port=%u failed errno=%d rc=%d\n",
		  (unsigned)port, errno, rc);
	(void)lwip_close(fd);
	return -1;
}

int
pi_sender_send(int *fd, uint8_t type, const void *payload, size_t len,
	       uint32_t timestamp_us, uint32_t seq, uint32_t crc32)
{
	struct fwc_msg_hdr hdr;

	if (fd == NULL || *fd < 0 || payload == NULL ||
	    len > (size_t)FWC_MAX_PAYLOAD)
		return -1;
	if (type != FWC_TYPE_VIDEO && type != FWC_TYPE_AUDIO)
		return -1;

	memset(&hdr, 0, sizeof(hdr));
	hdr.magic = FWC_MAGIC;
	hdr.version = FWC_VERSION;
	hdr.type = type;
	hdr.flags = 0U;
	hdr.length = (uint32_t)len;
	hdr.timestamp_us = timestamp_us;
	hdr.seq = seq;
	hdr.crc32 = crc32;

	if (send_all(*fd, &hdr, sizeof(hdr)) != 0 ||
	    send_all(*fd, payload, len) != 0) {
		pi_sender_close(fd);
		return -1;
	}
	return 0;
}

void
pi_net_port_stats(struct pi_port_stats *out)
{
	if (out == NULL)
		return;
	out->sdio_intrs = ap6181_stats.sdio_intrs;
	out->rx_frames = ap6181_stats.rx_frames;
	out->tx_frames = ap6181_stats.tx_frames;
	out->buffer_fail = ap6181_stats.buffer_fail;
	out->cmd_timeouts = ap6181_stats.cmd_timeouts;
	out->dma_errors = ap6181_stats.dma_errors;
}

void
pi_sender_close(int *fd)
{
	if (fd != NULL && *fd >= 0) {
		(void)lwip_close(*fd);
		*fd = -1;
	}
}
