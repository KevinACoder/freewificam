/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * eth_net.c - the wired-Ethernet half of the F429 -> Pi 4B stream.
 *
 * The same pi_net.h contract pi_net.c implements over the AP6181's WICED
 * driver, here over the board's own LAN8720A: the imported ST HAL ETH driver
 * runs the MAC/DMA/PHY, lwIP's DHCP client takes the router's lease, and the
 * two TCP senders are lwIP sockets.  The camera case never includes a HAL or
 * lwIP header; everything it needs is in pi_net.h.
 *
 * The link is up when the lease is in the netif and three ICMP echoes came
 * back from the Pi; the caller prints the link anchor from the filled
 * pi_link_info.  The receive thread is the same 1 ms drain of the 8-frame
 * ring the acceptance case uses.
 */

#if CONFIG_STREAM_LINK_ETH

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "cases.h"

#include "pi_net.h"
#include "fwc_proto.h"

#include "eth.h"
#include "stm32eth_netif.h"

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
#include "lwip/tcpip.h"
#include "lwip/errno.h"

#include "os/cmsis2/cmsis2.h"

#ifndef CONFIG_STREAM_PI_IP
#define CONFIG_STREAM_PI_IP "192.168.0.17"
#endif

#define ETH_DHCP_WAIT_MS	20000U
#define PI_PING_LEN		32U
#define PI_PING_GAP_MS		200U
#define PI_PING_ID		0x4336U

static struct netif s_netif;
static struct stm32eth_netif_stats s_bridge;
static struct pi_link_info s_info;
static osThreadId_t s_rx_thread;
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

static void
eth_rx_thread(void *arg)
{
	(void)arg;
	for (;;) {
		unsigned fed = stm32eth_netif_poll(&s_netif, 8U);

		if (fed != 0U) {
			(void)osDelay(0U);
		} else {
			(void)osDelay(1U);
		}
	}
}

static int
netif_start(void)
{
	ip4_addr_t any;

	ip4_addr_set_zero(&any);
	LOCK_TCPIP_CORE();
	stm32eth_netif_add(&s_netif, &any, &any, &any);
	netif_set_default(&s_netif);
	netif_set_link_up(&s_netif);
	netif_set_up(&s_netif);
	UNLOCK_TCPIP_CORE();

	s_rx_thread = osThreadNew(eth_rx_thread, NULL,
				  &(const osThreadAttr_t){
					  .name = "eth-stream-rx",
					  .priority = osPriorityNormal,
					  .stack_size = 4096U,
				  });
	return (s_rx_thread != NULL) ? 0 : -1;
}

static int
dhcp_lease(void)
{
	uint32_t waited = 0U;
	err_t err;

	LOCK_TCPIP_CORE();
	err = dhcp_start(&s_netif);
	UNLOCK_TCPIP_CORE();
	if (err != ERR_OK)
		return -1;
	for (;;) {
		int done;

		LOCK_TCPIP_CORE();
		done = dhcp_supplied_address(&s_netif) ? 1 : 0;
		if (done) {
			copy_ip(s_info.ip, sizeof(s_info.ip),
				ip4addr_ntoa(netif_ip4_addr(&s_netif)));
			copy_ip(s_info.gw, sizeof(s_info.gw),
				ip4addr_ntoa(netif_ip4_gw(&s_netif)));
		}
		UNLOCK_TCPIP_CORE();
		if (done)
			return 0;
		if (waited >= ETH_DHCP_WAIT_MS)
			return -1;
		(void)osDelay(100U);
		waited += 100U;
	}
}

int
pi_net_up(struct pi_link_info *out)
{
	struct f429_eth_link link;
	uint32_t t0 = osKernelGetTickCount();
	int rc;

	memset(&s_info, 0, sizeof(s_info));
	if (out == NULL)
		return -1;

	s_info.join_rc = -1;
	rc = f429_eth_init();
	if (rc != 0)
		goto fail;
	memset(&link, 0, sizeof(link));
	(void)f429_eth_link(&link);
	if (link.speed_mbit == 0U)
		goto fail;
	if (case_net_stack_up() != 0)
		goto fail;
	if (netif_start() != 0)
		goto fail;
	if (dhcp_lease() != 0)
		goto fail;

	s_info.join_rc = 0;
	s_info.join_ms = (unsigned)(osKernelGetTickCount() - t0);
	s_info.dhcp = 1;
	s_up = 1;
	{
		unsigned recv = 0U;
		int hrc = pi_net_ping(3U, &recv);

		CASE_INFO("pi_net: eth health ping recv=%u rc=%d\n", recv, hrc);
	}
	*out = s_info;
	return 0;

fail:
	s_info.join_ms = (unsigned)(osKernelGetTickCount() - t0);
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
				CASE_INFO("pi_net: eth send timeout off=%lu len=%lu\n",
					  (unsigned long)off, (unsigned long)len);
				return -1;
			}
			sys_msleep(1U);
			continue;
		}
		CASE_INFO("pi_net: eth send error errno=%d n=%d off=%lu len=%lu\n",
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
	int rc = -1;
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
	CASE_INFO("pi_net: eth connect port=%u failed errno=%d rc=%d\n",
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
pi_sender_close(int *fd)
{
	if (fd != NULL && *fd >= 0) {
		(void)lwip_close(*fd);
		*fd = -1;
	}
}

void
pi_net_port_stats(struct pi_port_stats *out)
{
	struct f429_eth_stats eth;

	if (out == NULL)
		return;
	f429_eth_stats_snapshot(&eth);
	stm32eth_netif_stats_snapshot(&s_bridge);
	out->sdio_intrs = 0U;		/* no SDIO on this link */
	out->rx_frames = (uint32_t)eth.rx_frames;
	out->tx_frames = (uint32_t)eth.tx_frames;
	out->buffer_fail = (uint32_t)s_bridge.rx_pbuf_fail;
	out->cmd_timeouts = (uint32_t)eth.tx_busy;
	out->dma_errors = (uint32_t)eth.dma_missed;
}

#endif /* CONFIG_STREAM_LINK_ETH */
