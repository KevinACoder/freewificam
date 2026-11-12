/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_sta_wpa2.c
 * @brief  G2: the AC 7260 joins a WPA2-PSK/CCMP AP as a station.
 *
 * The bring-up is C5's (core, firmware, PCI, wlan0, dev_open).  What changes is
 * the connection owner: wpa_supplicant now scans, associates and runs the
 * 4-way handshake, while this case only starts it, waits for COMPLETED, opens
 * the receive filter, acquires the address and probes the peer.  The next case
 * in the image (iwl7260_c6) then runs the throughput table over the same netif.
 *
 * Two profiles share this source: the Pi WPA2 AP (static address, peer on
 * link) and QiQiJia (DHCP, with a labelled static fallback).  SSID, BSSID,
 * frequency, address mode and peer candidates are Kconfig strings; the
 * passphrase is either the non-secret Pi bench constant or a generated
 * sta_psk.h for the external AP.
 *
 * Anchors:
 *   iwl7260_sta_wpa2: core ready
 *   iwl7260_sta_wpa2: fw registered=<n>
 *   iwl7260_sta_wpa2: pci devs=<n>
 *   iwl7260_sta_wpa2: wlan0 present=1 ifindex=<n> mac=<mac>
 *   iwl7260_sta_wpa2: start rc=<n>
 *   iwl7260_sta_wpa2: netif up name=<n> ip=<ip>/<mask> mac=<mac>
 *   iwl7260_sta_wpa2: security wpa2=1 ssid=<ssid>
 *   iwl7260_sta_wpa2: sta start rc=<n>
 *   iwl7260_sta_wpa2: sta state=<n> waited_ms=<n> bssid=<mac>
 *   iwl7260_sta_wpa2: sta eapol tx=<n> rx=<n> drop=<n>
 *   iwl7260_sta_wpa2: sta key ptk=<n> gtk=<n> fail=<n>
 *   iwl7260_sta_wpa2: mcast allmulti=1
 *   iwl7260_sta_wpa2: dhcp ip=<ip> mask=<mask> gw=<gw> waited_ms=<n>
 *   iwl7260_sta_wpa2: static ip=<ip> mask=<mask> gw=<gw>
 *   iwl7260_sta_wpa2: peer ip=<ip>
 *   iwl7260_sta_wpa2: ping tx=<n> rx=<n> payload=<n> rtt_ms=<min>/<avg>/<max>
 *   iwl7260_sta_wpa2: diag rx_frames=<n> tx_frames=<n> rx_bytes=<n> tx_bytes=<n> tx_fail=<n>
 *   iwl7260_sta_wpa2: PASS STAW
 *   iwl7260_sta_wpa2: FAIL <reason>
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"
#include "linux80211/sta_wpa.h"
#include "linuxkpi_pci.h"
#include "adapter.h"

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/net_namespace.h>

#ifndef CONFIG_STA_SSID
#define CONFIG_STA_SSID ""
#endif
#ifndef CONFIG_STA_BSSID
#define CONFIG_STA_BSSID ""
#endif
#ifndef CONFIG_STA_FREQ
#define CONFIG_STA_FREQ 2432
#endif
#ifndef CONFIG_STA_DHCP
#define CONFIG_STA_DHCP 0
#endif
#ifndef CONFIG_STA_STATIC_IP
#define CONFIG_STA_STATIC_IP ""
#endif
#ifndef CONFIG_STA_STATIC_MASK
#define CONFIG_STA_STATIC_MASK "255.255.255.0"
#endif
#ifndef CONFIG_STA_STATIC_GW
#define CONFIG_STA_STATIC_GW ""
#endif
#ifndef CONFIG_STA_FALLBACK_IP
#define CONFIG_STA_FALLBACK_IP "192.168.3.200"
#endif
#ifndef CONFIG_STA_FALLBACK_GW
#define CONFIG_STA_FALLBACK_GW "192.168.3.1"
#endif
#ifndef CONFIG_STA_PEER_CANDIDATES
#define CONFIG_STA_PEER_CANDIDATES ""
#endif

#if CONFIG_STA_PSK_EXTERNAL
#include "sta_psk.h"
#ifndef LKPI_STA_PSK
#error "CONFIG_STA_PSK_EXTERNAL needs build/kconfig/<profile>/sta_psk.h"
#endif
#define STA_PSK_VALUE LKPI_STA_PSK
#else
#define STA_PSK_VALUE CONFIG_STA_PSK
#endif

#define STA_WAIT_MS		40000U
#define STA_POLL_MS		100U
#define STA_DHCP_WAIT_MS	15000U
#define STA_PING_COUNT		20U
#define STA_PING_TIMEOUT_MS	300U
#define STA_PING_SMALL		56U
#define STA_PING_LARGE		1400U
#define STA_LOG_MAX		160U

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
int lkpi_fw_iwl7260_fw_register(void);

static unsigned sta_log_lines;

static void
sta_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL || sta_log_lines >= STA_LOG_MAX)
		return;
	if (strstr(line, "loaded firmware version") != NULL)
		CASE_ANCHOR("iwl7260_sta_wpa2: firmware %s\n", line);
	else if (strstr(line, "wlan0:") != NULL ||
		 strstr(line, "WPA:") != NULL ||
		 strstr(line, "EAPOL") != NULL ||
		 strstr(line, "RSN") != NULL ||
		 strstr(line, "4-Way") != NULL ||
		 strstr(line, "CTRL-EVENT") != NULL ||
		 strstr(line, "Failed") != NULL ||
		 strstr(line, "WPA2") != NULL) {
		CASE_INFO("iwl7260_sta_wpa2: drv %s\n", line);
		sta_log_lines++;
	}
}

static void
sta_copy(char *dst, size_t size, const char *src)
{
	if (dst == NULL || size == 0u)
		return;
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, size - 1u);
	dst[size - 1u] = '\0';
}

static int
sta_hex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int
sta_parse_mac(const char *text, unsigned char mac[6])
{
	int i;

	if (text == NULL || strlen(text) != 17u)
		return -1;
	for (i = 0; i < 6; i++) {
		int hi = sta_hex(text[i * 3]);
		int lo = sta_hex(text[i * 3 + 1]);

		if (hi < 0 || lo < 0)
			return -1;
		mac[i] = (unsigned char)((hi << 4) | lo);
		if (i < 5 && text[i * 3 + 2] != ':')
			return -1;
	}
	return 0;
}

static int
sta_wait_ready(unsigned int *waited_ms, int *state_out,
	       unsigned char bssid[6])
{
	struct net_device *dev = dev_get_by_name(&init_net, "wlan0");
	unsigned int waited = 0u;

	if (dev == NULL)
		return -1;
	while (waited < STA_WAIT_MS) {
		int state = lkpi_sta_state();

		if (state_out != NULL)
			*state_out = state;
		if (lkpi_sta_ready()) {
			*waited_ms = waited;
			(void)lkpi_nl80211_connected(dev, bssid);
			return 0;
		}
		osDelay(STA_POLL_MS);
		waited += STA_POLL_MS;
	}
	*waited_ms = waited;
	return -1;
}

static int
sta_static_fallback(char ip_out[16])
{
	const char *base = CONFIG_STA_FALLBACK_IP;
	const char *gw = CONFIG_STA_FALLBACK_GW;
	char ip[16];
	char *dot;
	unsigned int last;
	unsigned int n;
	unsigned int rx;
	unsigned int rmin, ravg, rmax;

	if (base == NULL || base[0] == '\0' || gw == NULL || gw[0] == '\0')
		return -1;
	sta_copy(ip, sizeof(ip), base);
	dot = strrchr(ip, '.');
	if (dot == NULL)
		return -1;
	last = (unsigned int)strtoul(dot + 1, NULL, 10);
	for (n = 0u; n < 3u; n++) {
		snprintf(dot + 1, sizeof(ip) - (size_t)(dot + 1 - ip), "%u",
			 last + n);
		if (lkpi_net_set_static(ip, "255.255.255.0", gw) != 0)
			continue;
		rx = 0u;
		if (lkpi_net_ping(gw, 3u, STA_PING_SMALL, 300u, &rx, &rmin,
				  &ravg, &rmax) == 0 && rx == 3u) {
			sta_copy(ip_out, 16, ip);
			CASE_ANCHOR("iwl7260_sta_wpa2: static ip=%s mask=%s gw=%s\n",
				    ip, "255.255.255.0", gw);
			return 0;
		}
	}
	return -1;
}

static const char *
sta_pick_peer(char peer[16])
{
	char list[96];
	char *save = NULL;
	char *token;
	unsigned int rx;
	unsigned int rmin, ravg, rmax;

	if (CONFIG_STA_PEER_CANDIDATES == NULL ||
	    CONFIG_STA_PEER_CANDIDATES[0] == '\0')
		return NULL;
	sta_copy(list, sizeof(list), CONFIG_STA_PEER_CANDIDATES);
	for (token = strtok_r(list, " ", &save); token != NULL;
	     token = strtok_r(NULL, " ", &save)) {
		rx = 0u;
		if (lkpi_net_ping(token, 3u, STA_PING_SMALL, 300u, &rx, &rmin,
				  &ravg, &rmax) == 0 && rx >= 2u) {
			sta_copy(peer, 16, token);
			return peer;
		}
	}
	return NULL;
}

int
case_iwl7260_sta_wpa2_run(void)
{
	struct net_device *dev = NULL;
	unsigned char bssid[6];
	unsigned char cfg_bssid[6];
	unsigned char mac[6];
	char netif_mac[6];
	char peer[16];
	char ip_out[16];
	struct lkpi_sta_cfg cfg;
	struct lkpi_net_lease lease;
	unsigned int waited = 0u;
	unsigned int dhcp_waited = 0u;
	unsigned int ping_rx = 0u;
	unsigned int rtt_min = 0u, rtt_avg = 0u, rtt_max = 0u;
	unsigned int eapol_tx = 0u, eapol_rx = 0u, eapol_drop = 0u;
	unsigned int ptk = 0u, gtk = 0u, key_fail = 0u;
	int state = -1;
	int rc;
	int peer_ok = 0;

	lkpi_core_init();
	CASE_ANCHOR("iwl7260_sta_wpa2: core ready\n");
	lkpi_log_sink_register(sta_sink);

	lkpi_pcie_port_install();
	rc = lkpi_fw_iwl7260_fw_register();
	CASE_ANCHOR("iwl7260_sta_wpa2: fw registered=%d\n", rc);
	if (rc != 0)
		return 1;
	lkpi_module_init_run();
	CASE_ANCHOR("iwl7260_sta_wpa2: initcalls run\n");
	lkpi_pci_start();
	CASE_ANCHOR("iwl7260_sta_wpa2: pci devs=%u\n",
		    (unsigned)lkpi_pci_dev_count_get());

	while (waited < STA_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(STA_POLL_MS);
		waited += STA_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_sta_wpa2: FAIL no wlan0\n");
		return 2;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("iwl7260_sta_wpa2: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	rc = dev_open(dev);
	CASE_ANCHOR("iwl7260_sta_wpa2: start rc=%d\n", rc);
	if (rc != 0)
		return 3;

	if (CONFIG_STA_SSID[0] == '\0') {
		CASE_ANCHOR("iwl7260_sta_wpa2: FAIL empty SSID\n");
		return 4;
	}
	if (CONFIG_STA_DHCP) {
		rc = lkpi_net_wlan_up("0.0.0.0", "0.0.0.0", NULL, netif_mac);
	} else {
		rc = lkpi_net_wlan_up(CONFIG_STA_STATIC_IP,
				      CONFIG_STA_STATIC_MASK,
				      CONFIG_STA_STATIC_GW[0] != '\0' ?
				      CONFIG_STA_STATIC_GW : NULL, netif_mac);
	}
	CASE_ANCHOR("iwl7260_sta_wpa2: netif up name=wl0 ip=%s/%s mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    CONFIG_STA_DHCP ? "0.0.0.0" : CONFIG_STA_STATIC_IP,
		    CONFIG_STA_DHCP ? "0.0.0.0" : CONFIG_STA_STATIC_MASK,
		    (unsigned char)netif_mac[0], (unsigned char)netif_mac[1],
		    (unsigned char)netif_mac[2], (unsigned char)netif_mac[3],
		    (unsigned char)netif_mac[4], (unsigned char)netif_mac[5]);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_sta_wpa2: FAIL netif rc=%d\n", rc);
		return 5;
	}
	CASE_ANCHOR("iwl7260_sta_wpa2: security wpa2=1 ssid=%s\n",
		    CONFIG_STA_SSID);

	memset(&cfg, 0, sizeof(cfg));
	cfg.ssid = CONFIG_STA_SSID;
	cfg.psk = STA_PSK_VALUE;
	cfg.freq_mhz = CONFIG_STA_FREQ;
	cfg.bssid = NULL;
	if (CONFIG_STA_BSSID[0] != '\0') {
		if (sta_parse_mac(CONFIG_STA_BSSID, cfg_bssid) == 0)
			cfg.bssid = cfg_bssid;
	}
	rc = lkpi_sta_start(&cfg);
	CASE_ANCHOR("iwl7260_sta_wpa2: sta start rc=%d\n", rc);
	if (rc != 0)
		return 6;

	if (sta_wait_ready(&waited, &state, bssid) != 0) {
		lkpi_sta_counts(&eapol_tx, &eapol_rx, &eapol_drop, &ptk, &gtk,
				&key_fail);
		CASE_ANCHOR("iwl7260_sta_wpa2: FAIL sta timeout state=%d waited_ms=%u eapol_tx=%u rx=%u drop=%u ptk=%u gtk=%u fail=%u\n",
			    state, waited, eapol_tx, eapol_rx, eapol_drop, ptk,
			    gtk, key_fail);
		return 7;
	}
	lkpi_sta_counts(&eapol_tx, &eapol_rx, &eapol_drop, &ptk, &gtk,
			&key_fail);
	CASE_ANCHOR("iwl7260_sta_wpa2: sta state=%d waited_ms=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    state, waited, bssid[0], bssid[1], bssid[2], bssid[3],
		    bssid[4], bssid[5]);
	CASE_ANCHOR("iwl7260_sta_wpa2: sta eapol tx=%u rx=%u drop=%u\n",
		    eapol_tx, eapol_rx, eapol_drop);
	CASE_ANCHOR("iwl7260_sta_wpa2: sta key ptk=%u gtk=%u fail=%u\n",
		    ptk, gtk, key_fail);
	if (ptk == 0u || gtk == 0u || key_fail != 0u) {
		CASE_ANCHOR("iwl7260_sta_wpa2: FAIL key ptk=%u gtk=%u fail=%u\n",
			    ptk, gtk, key_fail);
		return 8;
	}

	/* The station path never calls configure_filter; without this the
	 * firmware multicast filter stays NULL and broadcast ARP/DHCP never
	 * reaches the board.  Allmulti is the bench's deliberate choice. */
	dev->flags |= IFF_ALLMULTI;
	dev_set_rx_mode(dev);
	CASE_ANCHOR("iwl7260_sta_wpa2: mcast allmulti=1\n");

	if (CONFIG_STA_DHCP) {
		unsigned int start = 0u;

		(void)lkpi_net_dhcp_start();
		while (start < STA_DHCP_WAIT_MS) {
			if (lkpi_net_dhcp_wait(&lease, 100u) == 0)
				break;
			start += 100u;
		}
		dhcp_waited = start;
		if (lease.ip[0] != '\0') {
			if (lease.gw[0] == '\0')
				sta_copy(lease.gw, sizeof(lease.gw),
					 CONFIG_STA_FALLBACK_GW);
			CASE_ANCHOR("iwl7260_sta_wpa2: dhcp ip=%s mask=%s gw=%s waited_ms=%u\n",
				    lease.ip, lease.mask, lease.gw, dhcp_waited);
			sta_copy(ip_out, sizeof(ip_out), lease.ip);
		} else {
			CASE_ANCHOR("iwl7260_sta_wpa2: dhcp timeout waited_ms=%u; static fallback\n",
				    dhcp_waited);
			if (sta_static_fallback(ip_out) != 0) {
				CASE_ANCHOR("iwl7260_sta_wpa2: FAIL no address\n");
				return 9;
			}
			sta_copy(lease.mask, sizeof(lease.mask), "255.255.255.0");
			sta_copy(lease.gw, sizeof(lease.gw),
				 CONFIG_STA_FALLBACK_GW);
		}
	} else {
		sta_copy(ip_out, sizeof(ip_out), CONFIG_STA_STATIC_IP);
		CASE_ANCHOR("iwl7260_sta_wpa2: static ip=%s mask=%s gw=%s\n",
			    ip_out, CONFIG_STA_STATIC_MASK,
			    CONFIG_STA_STATIC_GW);
	}

	peer[0] = '\0';
	peer_ok = (sta_pick_peer(peer) != NULL);
	if (peer_ok) {
		CASE_ANCHOR("iwl7260_sta_wpa2: peer ip=%s\n", peer);
#if defined(RTOS_CASE_IWL7260_C6)
		lkpi_c6_peer_ip_set(peer);
#endif
	} else {
		CASE_ANCHOR("iwl7260_sta_wpa2: peer none candidates=%s\n",
			    CONFIG_STA_PEER_CANDIDATES);
	}

	rc = lkpi_net_ping(CONFIG_STA_DHCP ? lease.gw : CONFIG_STA_STATIC_GW,
			   STA_PING_COUNT, STA_PING_SMALL, STA_PING_TIMEOUT_MS,
			   &ping_rx, &rtt_min, &rtt_avg, &rtt_max);
	CASE_ANCHOR("iwl7260_sta_wpa2: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    STA_PING_COUNT, ping_rx, STA_PING_SMALL, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != STA_PING_COUNT) {
		CASE_ANCHOR("iwl7260_sta_wpa2: FAIL ping small rc=%d rx=%u\n",
			    rc, ping_rx);
		return 10;
	}
	rc = lkpi_net_ping(CONFIG_STA_DHCP ? lease.gw : CONFIG_STA_STATIC_GW,
			   STA_PING_COUNT, STA_PING_LARGE, STA_PING_TIMEOUT_MS,
			   &ping_rx, &rtt_min, &rtt_avg, &rtt_max);
	CASE_ANCHOR("iwl7260_sta_wpa2: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    STA_PING_COUNT, ping_rx, STA_PING_LARGE, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != STA_PING_COUNT) {
		CASE_ANCHOR("iwl7260_sta_wpa2: FAIL ping large rc=%d rx=%u\n",
			    rc, ping_rx);
		return 11;
	}
	{
		struct lkpi_net_diag diag;

		lkpi_net_diag(&diag);
		CASE_ANCHOR("iwl7260_sta_wpa2: diag rx_frames=%lu tx_frames=%lu rx_bytes=%lu tx_bytes=%lu tx_fail=%lu\n",
			    diag.rx_frames, diag.tx_frames, diag.rx_bytes,
			    diag.tx_bytes, diag.tx_fail);
	}
	/* The peer's own ping/ARP walk gets a window before C6 starts; without
	 * it the Pi's first measurement races the throughput phases. */
	CASE_ANCHOR("iwl7260_sta_wpa2: peer window ms=5000\n");
	osDelay(5000u);

	CASE_ANCHOR("iwl7260_sta_wpa2: PASS STAW\n");
	return 0;
}
