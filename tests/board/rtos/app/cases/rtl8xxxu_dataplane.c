/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8xxxu_dataplane.c
 * @brief  Case rtl8xxxu_dataplane: prove the associated station carries data
 *         both ways, with no IP stack in the image (the presentation layer is
 *         the next round's job).
 *
 * The gate is one ARP exchange and ten ICMP echo requests on the open bench
 * AP: the frames enter net80211 through wlan_bridge_tx_raw (the stack's own
 * encap and, when a key exists, crypto), the AP answers as a plain ethernet
 * host, and the answers arrive back through the bridge's data receive seam
 * (wlan_port_set_data_rx).  What this proves is the driver's per-station
 * upload and the two data paths; the lwIP netif case (`rtl8xxxu_net`) adds the
 * IP/ARP bookkeeping on top and carries the TCP arms.
 *
 * The AP's MAC is learned from its ARP reply, never pinned in the image: the
 * bench AP is restarted between arms, and a BSSID baked into a case would turn
 * a bench restart into a data-plane failure.
 *
 * The AP's own ARP for our address is answered from this case's thread, never
 * from the receive callback: the receive context must only count/record, and
 * the reply is a transmit that goes through the bridge's own serializer.  The
 * callback latches the requester; the wait loops here drain that latch.
 *
 * Anchors:
 *   rtl8xxxu: data arp reply src=<mac> tx=<n>
 *   rtl8xxxu: data ping tx=10 rx=<n> min_ms=<n> max_ms=<n> lost=<seq,...>
 *   rtl8xxxu: data counters data_tx=<n> tx_done=<n> rx=<n> rx_data=<n>
 *   rtl8xxxu: data PASS
 *
 * A failed gate is `rtl8xxxu: data FAIL <reason>` with the same counters: no
 * ARP answer is "the AP never saw our data", an ARP answer without echo
 * replies is "the AP's replies do not reach the stack", and an empty counter
 * set names the side that never moved.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "wlan_bridge.h"

/* the port's receive seam (declared by the component's port.h; only scalars
 * cross this include, which is why the signature is repeated here) */
void wlan_port_set_data_rx(void (*fn)(const uint8_t *frame, size_t len,
    void *arg), void *arg);

#define RTL8XXXU_DATA_ARP_WAIT_MS	3000u
#define RTL8XXXU_DATA_STEP_MS	20u
#define RTL8XXXU_DATA_PING_N		20u
#define RTL8XXXU_DATA_PING_GAP_MS	500u
#define RTL8XXXU_DATA_TAIL_MS	1000u
#define RTL8XXXU_DATA_MIN_REPLIES	19u

#define RTL8XXXU_DATA_IP_A		192u
#define RTL8XXXU_DATA_IP_B		168u
#define RTL8XXXU_DATA_IP_C		99u
#define RTL8XXXU_DATA_IP_US		2u
#define RTL8XXXU_DATA_IP_AP		1u
#define RTL8XXXU_DATA_PING_ID	0x8188u

static const uint8_t rtl8xxxu_data_us_ip[4] = {
	RTL8XXXU_DATA_IP_A, RTL8XXXU_DATA_IP_B, RTL8XXXU_DATA_IP_C, RTL8XXXU_DATA_IP_US
};
static const uint8_t rtl8xxxu_data_ap_ip[4] = {
	RTL8XXXU_DATA_IP_A, RTL8XXXU_DATA_IP_B, RTL8XXXU_DATA_IP_C, RTL8XXXU_DATA_IP_AP
};

static uint8_t rtl8xxxu_data_our_mac[6];
static uint8_t rtl8xxxu_data_ap_mac[6];

static volatile unsigned int rtl8xxxu_data_arp_replies;
static volatile unsigned int rtl8xxxu_data_arp_query;
static uint8_t rtl8xxxu_data_arp_query_mac[6];
static uint8_t rtl8xxxu_data_arp_query_ip[4];

static volatile unsigned int rtl8xxxu_data_echo_seen;
static volatile unsigned int rtl8xxxu_data_echo_ok[RTL8XXXU_DATA_PING_N];
static volatile unsigned int rtl8xxxu_data_echo_rtt[RTL8XXXU_DATA_PING_N];
static volatile uint32_t rtl8xxxu_data_echo_sent[RTL8XXXU_DATA_PING_N];
static uint16_t rtl8xxxu_data_ip_id;

static uint16_t
rtl8xxxu_data_csum(const uint8_t *p, size_t n)
{
	uint32_t sum = 0;

	/*
	 * The one's-complement sum over 16-bit words **in network order**: the
	 * IPv4 and ICMP headers are big-endian, so p[0] is the high byte.  The
	 * little-endian reading this helper started with produced byte-swapped
	 * checksums, and the board said so in the only way it could: the
	 * checksum-free ARP exchange went through both ways while every echo
	 * request was silently dropped by the AP's IP stack (0/10, with the AP's
	 * own station dump counting the frames as received).
	 */
	while (n > 1u) {
		sum += (uint32_t)(((uint16_t)p[0] << 8) | p[1]);
		p += 2;
		n -= 2;
	}
	if (n != 0u)
		sum += (uint32_t)p[0] << 8;
	while ((sum >> 16) != 0u)
		sum = (sum & 0xffffu) + (sum >> 16);
	return (uint16_t)~sum;
}

static int
rtl8xxxu_data_send(const uint8_t *frame, size_t len)
{

	return wlan_bridge_tx_raw(frame, len) < 0 ? -1 : 0;
}

/*
 * Answer one latched who-has-our-address request.  Called from the case
 * thread only: the receive callback runs in the driver's context and a
 * transmit from there would enter the bridge's serializer re-entrantly.
 */
static void
rtl8xxxu_data_service_arp(void)
{
	uint8_t f[42];
	uint8_t mac[6];
	uint8_t ip[4];

	if (rtl8xxxu_data_arp_query == 0u)
		return;
	memcpy(mac, rtl8xxxu_data_arp_query_mac, 6);
	memcpy(ip, rtl8xxxu_data_arp_query_ip, 4);
	rtl8xxxu_data_arp_query = 0u;

	memcpy(f, mac, 6);		/* dst: the requester */
	memcpy(f + 6, rtl8xxxu_data_our_mac, 6);
	f[12] = 0x08;
	f[13] = 0x06;
	memset(f + 14, 0, 28);
	f[14] = 0x00;
	f[15] = 0x01;			/* htype: ethernet */
	f[16] = 0x08;
	f[17] = 0x00;			/* ptype: IPv4 */
	f[18] = 6;
	f[19] = 4;
	f[20] = 0x00;
	f[21] = 0x02;			/* op: reply */
	memcpy(f + 22, rtl8xxxu_data_our_mac, 6);
	memcpy(f + 28, rtl8xxxu_data_us_ip, 4);
	memcpy(f + 32, mac, 6);
	memcpy(f + 38, ip, 4);
	(void)rtl8xxxu_data_send(f, sizeof(f));
}

static void
rtl8xxxu_data_rx(const uint8_t *f, size_t len, void *arg)
{
	const uint8_t *a;
	uint16_t op;

	(void)arg;
	if (f == NULL || len < 42u)
		return;

	if (f[12] == 0x08 && f[13] == 0x06) {
		a = f + 14;
		op = (uint16_t)((a[6] << 8) | a[7]);
		if (op == 1) {
			/* who-has our address: latch it for the thread */
			if (memcmp(a + 24, rtl8xxxu_data_us_ip, 4) == 0) {
				memcpy(rtl8xxxu_data_arp_query_mac, a + 8, 6);
				memcpy(rtl8xxxu_data_arp_query_ip, a + 14, 4);
				rtl8xxxu_data_arp_query = 1u;
			}
			return;
		}
		if (op == 2 && memcmp(a + 14, rtl8xxxu_data_ap_ip, 4) == 0) {
			memcpy(rtl8xxxu_data_ap_mac, a + 8, 6);
			rtl8xxxu_data_arp_replies++;
		}
		return;
	}

	if (f[12] == 0x08 && f[13] == 0x00) {
		const uint8_t *ip = f + 14;
		const uint8_t *icmp;
		unsigned int hl, seq;
		uint32_t now;

		if ((ip[0] & 0xf0u) != 0x40u)
			return;
		hl = (unsigned int)(ip[0] & 0x0fu) * 4u;
		if (hl < 20u || len < 14u + hl + 8u || ip[9] != 1u)
			return;
		icmp = ip + hl;
		/* type 0 = echo reply; id/seq are network order, the same
		 * bytes this side put on the wire */
		if (icmp[0] != 0 ||
		    icmp[4] != (uint8_t)(RTL8XXXU_DATA_PING_ID >> 8) ||
		    icmp[5] != (uint8_t)(RTL8XXXU_DATA_PING_ID & 0xffu))
			return;
		seq = ((unsigned int)icmp[6] << 8) | icmp[7];
		if (seq >= RTL8XXXU_DATA_PING_N || rtl8xxxu_data_echo_ok[seq])
			return;
		now = osKernelGetTickCount();
		rtl8xxxu_data_echo_rtt[seq] = now - rtl8xxxu_data_echo_sent[seq];
		rtl8xxxu_data_echo_ok[seq] = 1u;
		rtl8xxxu_data_echo_seen++;
	}
}

static int
rtl8xxxu_data_send_arp_request(void)
{
	uint8_t f[42];

	memset(f, 0, sizeof(f));
	memset(f, 0xff, 6);		/* broadcast */
	memcpy(f + 6, rtl8xxxu_data_our_mac, 6);
	f[12] = 0x08;
	f[13] = 0x06;
	f[14] = 0x00;
	f[15] = 0x01;
	f[16] = 0x08;
	f[17] = 0x00;
	f[18] = 6;
	f[19] = 4;
	f[20] = 0x00;
	f[21] = 0x01;			/* request */
	memcpy(f + 22, rtl8xxxu_data_our_mac, 6);
	memcpy(f + 28, rtl8xxxu_data_us_ip, 4);
	memcpy(f + 38, rtl8xxxu_data_ap_ip, 4);
	return rtl8xxxu_data_send(f, sizeof(f));
}

static int
rtl8xxxu_data_send_echo(unsigned int seq)
{
	uint8_t f[42];
	uint16_t id = rtl8xxxu_data_ip_id++;

	memset(f, 0, sizeof(f));
	memcpy(f, rtl8xxxu_data_ap_mac, 6);
	memcpy(f + 6, rtl8xxxu_data_our_mac, 6);
	f[12] = 0x08;
	f[13] = 0x00;
	/* IPv4 header */
	f[14] = 0x45;
	f[16] = 0x00;
	f[17] = 28;			/* total length: 20 + 8 */
	f[18] = (uint8_t)(id >> 8);
	f[19] = (uint8_t)id;
	f[22] = 64;			/* ttl */
	f[23] = 1;			/* ICMP */
	memcpy(f + 26, rtl8xxxu_data_us_ip, 4);
	memcpy(f + 30, rtl8xxxu_data_ap_ip, 4);
	/* ICMP echo request: id/seq in network order */
	f[34] = 8;
	f[35] = 0;
	f[38] = (uint8_t)(RTL8XXXU_DATA_PING_ID >> 8);
	f[39] = (uint8_t)(RTL8XXXU_DATA_PING_ID & 0xffu);
	f[40] = (uint8_t)(seq >> 8);
	f[41] = (uint8_t)(seq & 0xffu);
	{
		uint16_t c = rtl8xxxu_data_csum(f + 14, 20);

		f[24] = (uint8_t)(c >> 8);
		f[25] = (uint8_t)(c & 0xffu);
		c = rtl8xxxu_data_csum(f + 34, 8);
		f[36] = (uint8_t)(c >> 8);
		f[37] = (uint8_t)(c & 0xffu);
	}
	return rtl8xxxu_data_send(f, sizeof(f));
}

int
case_rtl8xxxu_dataplane_run(void)
{
	struct wlan_bridge_counters c;
	unsigned int waited = 0;
	unsigned int i;
	unsigned int rx = 0;
	unsigned int min_ms = 0xffffffffu;
	unsigned int max_ms = 0;
	unsigned int lost_n = 0;
	char lostbuf[RTL8XXXU_DATA_PING_N * 3 + 8];
	size_t lp = 0;

	wlan_bridge_stats(&c);
	if (c.attached == 0u) {
		CASE_ANCHOR("rtl8xxxu: data FAIL not attached\n");
		return 1;
	}
	if (wlan_bridge_state() != 4) {
		CASE_ANCHOR("rtl8xxxu: data FAIL not associated (state=%d)\n",
		    wlan_bridge_state());
		return 1;
	}
	if (wlan_bridge_hwaddr(rtl8xxxu_data_our_mac) != 0) {
		CASE_ANCHOR("rtl8xxxu: data FAIL no interface address\n");
		return 1;
	}
	/* the AP's MAC is learned from its ARP reply (below), not pinned */
	wlan_port_set_data_rx(rtl8xxxu_data_rx, NULL);

	/* 1. one ARP exchange: the AP must see our data and answer it.  Three
	 * requests: the first data frame after association is the one the AP's
	 * bridge drops (measured: data_tx=1, no reply, a later request
	 * answered), and a retry is cheaper than a flaky gate. */
	for (unsigned int attempt = 1; attempt <= 3u; attempt++) {
		waited = 0;
		if (rtl8xxxu_data_send_arp_request() != 0) {
			CASE_ANCHOR("rtl8xxxu: data FAIL arp request refused\n");
			return 1;
		}
		while (rtl8xxxu_data_arp_replies == 0u &&
		       waited < RTL8XXXU_DATA_ARP_WAIT_MS) {
			osDelay(RTL8XXXU_DATA_STEP_MS);
			waited += RTL8XXXU_DATA_STEP_MS;
			rtl8xxxu_data_service_arp();
		}
		if (rtl8xxxu_data_arp_replies != 0u)
			break;
		CASE_INFO("rtl8xxxu: data arp attempt %u no reply\n", attempt);
	}
	wlan_bridge_stats(&c);
	if (rtl8xxxu_data_arp_replies == 0u) {
		CASE_ANCHOR("rtl8xxxu: data FAIL no arp reply (data_tx=%u rx=%u "
		    "rx_data=%u)\n", c.data_tx, c.rx_frames, c.rx_data);
		return 1;
	}
	CASE_ANCHOR("rtl8xxxu: data arp reply src=%02x:%02x:%02x:%02x:%02x:%02x "
	    "tx=%u\n", rtl8xxxu_data_ap_mac[0], rtl8xxxu_data_ap_mac[1],
	    rtl8xxxu_data_ap_mac[2], rtl8xxxu_data_ap_mac[3], rtl8xxxu_data_ap_mac[4],
	    rtl8xxxu_data_ap_mac[5], c.data_tx);

	/* 2. ten echo requests, one every 500 ms; the gaps also drain the
	 * AP's ARP for our address, or its reply never gets a destination */
	for (i = 0; i < RTL8XXXU_DATA_PING_N; i++) {
		unsigned int g = 0;

		rtl8xxxu_data_echo_sent[i] = osKernelGetTickCount();
		if (rtl8xxxu_data_send_echo(i) != 0) {
			CASE_ANCHOR("rtl8xxxu: data FAIL echo %u refused\n", i);
			return 1;
		}
		while (g < RTL8XXXU_DATA_PING_GAP_MS) {
			osDelay(RTL8XXXU_DATA_STEP_MS);
			g += RTL8XXXU_DATA_STEP_MS;
			rtl8xxxu_data_service_arp();
		}
	}
	/* the last reply may be in flight when the loop ends */
	for (i = 0; i < RTL8XXXU_DATA_TAIL_MS / RTL8XXXU_DATA_STEP_MS; i++) {
		osDelay(RTL8XXXU_DATA_STEP_MS);
		rtl8xxxu_data_service_arp();
	}

	for (i = 0; i < RTL8XXXU_DATA_PING_N; i++) {
		if (rtl8xxxu_data_echo_ok[i]) {
			unsigned int rtt = rtl8xxxu_data_echo_rtt[i];

			rx++;
			if (rtt < min_ms)
				min_ms = rtt;
			if (rtt > max_ms)
				max_ms = rtt;
		} else {
			lost_n++;
		}
	}
	for (i = 0; i < RTL8XXXU_DATA_PING_N; i++) {
		if (rtl8xxxu_data_echo_ok[i])
			continue;
		if (lp != 0)
			lostbuf[lp++] = ',';
		lostbuf[lp++] = (char)('0' + (i / 10u) % 10u);
		lostbuf[lp++] = (char)('0' + i % 10u);
	}
	if (lp == 0)
		lostbuf[lp++] = '-';
	lostbuf[lp] = '\0';

	wlan_bridge_stats(&c);
	CASE_ANCHOR("rtl8xxxu: data ping tx=%u rx=%u min_ms=%u max_ms=%u "
	    "lost=%s\n", RTL8XXXU_DATA_PING_N, rx, rx != 0u ? min_ms : 0u, max_ms,
	    lostbuf);
	CASE_ANCHOR("rtl8xxxu: data counters data_tx=%u tx_done=%u rx=%u "
	    "rx_data=%u\n", c.data_tx, c.tx_completes, c.rx_frames, c.rx_data);

	if (rx < RTL8XXXU_DATA_MIN_REPLIES) {
		CASE_ANCHOR("rtl8xxxu: data FAIL echo %u/%u\n", rx,
		    RTL8XXXU_DATA_PING_N);
		return 1;
	}
	(void)lost_n;
	CASE_ANCHOR("rtl8xxxu: data PASS\n");
	return 0;
}