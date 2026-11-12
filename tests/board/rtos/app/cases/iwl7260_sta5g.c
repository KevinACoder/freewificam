/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_sta5g.c
 * @brief  Case iwl7260_sta5g: the AC 7260 joins a 5 GHz AP as a station
 *         (docs/HANDOFF-5GHZ.md P3/P4).
 *
 * The bring-up is C5's (core, firmware, PCI, wlan0, dev_open); the association
 * is the imported supplicant's, exactly as in iwl7260_sta_wpa2.  Two things are
 * this case's own:
 *
 *   1. the band is an input (STA_FREQ) and a verdict input: after the
 *      supplicant says COMPLETED the case reads the association's frequency
 *      back out of cfg80211 and prints it.  A 5 GHz attempt that quietly fell
 *      back to 2.4 GHz must not be able to read as a pass, and `COMPLETED`
 *      alone cannot tell the two apart.
 *   2. an empty passphrase means an open network, so the same source covers the
 *      band gate (open) and the crypto gate (WPA2) as two profiles.  The round
 *      needs them separated: a 4-way failure and a band failure are different
 *      findings and must not be able to hide each other.
 *
 * What this is not: the throughput table (iwl7260_c6 owns that), and not a
 * scan survey - the scan is filtered to STA_FREQ so a successful scan cannot be
 * mistaken for "the band works" when it actually found the AP on another band.
 *
 * Anchors:
 *   iwl7260_sta5g: core ready
 *   iwl7260_sta5g: fw registered=<n>
 *   iwl7260_sta5g: initcalls run
 *   iwl7260_sta5g: pci devs=<n>
 *   iwl7260_sta5g: wlan0 present=1 ifindex=<n> mac=<mac>
 *   iwl7260_sta5g: bands 2g=<0|1> 5g=<0|1> 5g_chans=<n> 5g_ht=<0|1>
 *   iwl7260_sta5g: chan freq=<mhz> chan=<n> present=<0|1> flags=0x<hex>
 *   iwl7260_sta5g: start rc=<n>
 *   iwl7260_sta5g: netif up name=<n> ip=<ip>/<mask> mac=<mac>
 *   iwl7260_sta5g: security wpa2=<0|1> ssid=<ssid> freq=<mhz>
 *   iwl7260_sta5g: sta start rc=<n>
 *   iwl7260_sta5g: sta state=<n> waited_ms=<n> bssid=<mac>
 *   iwl7260_sta5g: link freq=<mhz>
 *   iwl7260_sta5g: mcast allmulti=1
 *   iwl7260_sta5g: peer ip=<ip>
 *   iwl7260_sta5g: ping tx=<n> rx=<n> payload=<n> rtt_ms=<min>/<avg>/<max>
 *   iwl7260_sta5g: PASS STA5      (5 GHz associated, data plane)
 *   iwl7260_sta5g: PASS STA5W     (same, WPA2)
 *   iwl7260_sta5g: FAIL <reason>
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
#define CONFIG_STA_FREQ 5180
#endif
#ifndef CONFIG_STA_DHCP
#define CONFIG_STA_DHCP 0
#endif
#ifndef CONFIG_STA_STATIC_IP
#define CONFIG_STA_STATIC_IP "192.168.99.2"
#endif
#ifndef CONFIG_STA_STATIC_MASK
#define CONFIG_STA_STATIC_MASK "255.255.255.0"
#endif
#ifndef CONFIG_STA_STATIC_GW
#define CONFIG_STA_STATIC_GW ""
#endif
#ifndef CONFIG_STA_PEER_CANDIDATES
#define CONFIG_STA_PEER_CANDIDATES ""
#endif
#ifndef CONFIG_STA_PSK
#define CONFIG_STA_PSK ""
#endif
#ifndef CONFIG_STA_PSK_EXTERNAL
#define CONFIG_STA_PSK_EXTERNAL 0
#endif

#if CONFIG_STA_PSK_EXTERNAL
#include "sta_psk.h"
#ifndef LKPI_STA_PSK
#error "CONFIG_STA_PSK_EXTERNAL needs build/kconfig/<profile>/sta_psk.h"
#endif
#define STA5_PSK_VALUE LKPI_STA_PSK
#else
#define STA5_PSK_VALUE CONFIG_STA_PSK
#endif

/* An empty passphrase is the open-network arm; anything else must be a
 * passphrase the supplicant accepts (8..63). */
#define STA5_WPA2	(STA5_PSK_VALUE[0] != '\0')

#define STA5_WAIT_MS		40000U
#define STA5_POLL_MS		100U
#define STA5_PING_COUNT		20U
#define STA5_PING_TIMEOUT_MS	300U
#define STA5_PING_SMALL		56U
#define STA5_PING_LARGE		1400U
#define STA5_LOG_MAX		160U

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
int lkpi_fw_iwl7260_fw_register(void);

static unsigned sta5_log_lines;

static void
sta5_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL || sta5_log_lines >= STA5_LOG_MAX)
		return;
	if (strstr(line, "loaded firmware version") != NULL) {
		CASE_ANCHOR("iwl7260_sta5g: firmware %s\n", line);
	} else if (strstr(line, "wlan0:") != NULL ||
		   strstr(line, "WPA:") != NULL ||
		   strstr(line, "EAPOL") != NULL ||
		   strstr(line, "CTRL-EVENT") != NULL ||
		   strstr(line, "Failed") != NULL) {
		CASE_INFO("iwl7260_sta5g: drv %s\n", line);
		sta5_log_lines++;
	}
}

static int
sta5_parse_mac(const char *text, unsigned char mac[6])
{
	unsigned v[6];
	int i;

	if (text == NULL || sscanf(text, "%x:%x:%x:%x:%x:%x", &v[0], &v[1],
				   &v[2], &v[3], &v[4], &v[5]) != 6)
		return -1;
	for (i = 0; i < 6; i++) {
		if (v[i] > 0xffu)
			return -1;
		mac[i] = (unsigned char)v[i];
	}
	return 0;
}

/* The peer: the candidate list's first entry, or the static gateway.  One
 * address is enough for this round - the gate is "the band carries data",
 * not "the bench resolves a topology". */
static const char *
sta5_peer(void)
{
	static char first[16];

	if (CONFIG_STA_PEER_CANDIDATES[0] != '\0') {
		const char *comma = strchr(CONFIG_STA_PEER_CANDIDATES, ',');
		size_t n = (comma != NULL) ?
		    (size_t)(comma - CONFIG_STA_PEER_CANDIDATES) : 0u;

		if (n == 0u || n >= sizeof(first))
			n = strlen(CONFIG_STA_PEER_CANDIDATES);
		if (n >= sizeof(first))
			n = sizeof(first) - 1u;
		memcpy(first, CONFIG_STA_PEER_CANDIDATES, n);
		first[n] = '\0';
		return first;
	}
	if (CONFIG_STA_STATIC_GW[0] != '\0')
		return CONFIG_STA_STATIC_GW;
	return NULL;
}

static int
sta5_wait_ready(unsigned int *waited_ms, int *state_out, unsigned char bssid[6])
{
	struct net_device *dev = dev_get_by_name(&init_net, "wlan0");
	unsigned int waited = 0u;

	if (dev == NULL)
		return -1;
	while (waited < STA5_WAIT_MS) {
		int state = lkpi_sta_state();

		if (state_out != NULL)
			*state_out = state;
		if (lkpi_sta_ready()) {
			*waited_ms = waited;
			(void)lkpi_nl80211_connected(dev, bssid);
			return 0;
		}
		osDelay(STA5_POLL_MS);
		waited += STA5_POLL_MS;
	}
	*waited_ms = waited;
	return -1;
}

int
case_iwl7260_sta5g_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct ieee80211_supported_band *band5;
	struct ieee80211_channel *chan;
	struct lkpi_sta_cfg cfg;
	unsigned char mac[6];
	unsigned char cfg_bssid[6];
	unsigned char bssid[6];
	char netif_mac[6];
	const char *peer;
	int freq;
	unsigned int waited = 0u;
	unsigned int ping_rx = 0u;
	unsigned int rtt_min = 0U, rtt_avg = 0U, rtt_max = 0U;
	unsigned eapol_tx = 0u, eapol_rx = 0u, eapol_drop = 0u;
	unsigned ptk = 0u, gtk = 0u, key_fail = 0u;
	int state = -1;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("iwl7260_sta5g: core ready\n");
	lkpi_log_sink_register(sta5_sink);

	lkpi_pcie_port_install();

	rc = lkpi_fw_iwl7260_fw_register();
	CASE_ANCHOR("iwl7260_sta5g: fw registered=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL firmware registration rc=%d\n",
			    rc);
		return 1;
	}

	lkpi_module_init_run();
	CASE_ANCHOR("iwl7260_sta5g: initcalls run\n");
	lkpi_pci_start();
	CASE_ANCHOR("iwl7260_sta5g: pci devs=%u\n",
		    (unsigned)lkpi_pci_dev_count_get());

	while (waited < STA5_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(STA5_POLL_MS);
		waited += STA5_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL no wlan0\n");
		return 2;
	}
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL no wireless_dev\n");
		return 3;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("iwl7260_sta5g: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	band5 = wdev->wiphy->bands[NL80211_BAND_5GHZ];
	CASE_ANCHOR("iwl7260_sta5g: bands 2g=%d 5g=%d 5g_chans=%u 5g_ht=%d\n",
		    wdev->wiphy->bands[NL80211_BAND_2GHZ] != NULL ? 1 : 0,
		    band5 != NULL ? 1 : 0,
		    band5 != NULL ? (unsigned)band5->n_channels : 0u,
		    (band5 != NULL && band5->ht_cap.ht_supported) ? 1 : 0);
	if (band5 == NULL) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL no 5 GHz band in the wiphy\n");
		return 4;
	}
	chan = ieee80211_get_channel(wdev->wiphy, (int)CONFIG_STA_FREQ);
	CASE_ANCHOR("iwl7260_sta5g: chan freq=%u chan=%d present=%d "
		    "flags=0x%08x\n",
		    (unsigned)CONFIG_STA_FREQ,
		    ieee80211_frequency_to_channel((int)CONFIG_STA_FREQ),
		    chan != NULL ? 1 : 0,
		    chan != NULL ? (unsigned)chan->flags : 0u);
	if (chan == NULL) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL freq %u not in the band\n",
			    (unsigned)CONFIG_STA_FREQ);
		return 5;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("iwl7260_sta5g: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL dev_open rc=%d\n", rc);
		return 6;
	}

	if (CONFIG_STA_SSID[0] == '\0') {
		CASE_ANCHOR("iwl7260_sta5g: FAIL empty SSID\n");
		return 7;
	}
	if (CONFIG_STA_DHCP) {
		rc = lkpi_net_wlan_up("0.0.0.0", "0.0.0.0", NULL, netif_mac);
	} else {
		rc = lkpi_net_wlan_up(CONFIG_STA_STATIC_IP,
				      CONFIG_STA_STATIC_MASK,
				      CONFIG_STA_STATIC_GW[0] != '\0' ?
				      CONFIG_STA_STATIC_GW : NULL, netif_mac);
	}
	CASE_ANCHOR("iwl7260_sta5g: netif up name=wl0 ip=%s/%s mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    CONFIG_STA_DHCP ? "0.0.0.0" : CONFIG_STA_STATIC_IP,
		    CONFIG_STA_DHCP ? "0.0.0.0" : CONFIG_STA_STATIC_MASK,
		    (unsigned char)netif_mac[0], (unsigned char)netif_mac[1],
		    (unsigned char)netif_mac[2], (unsigned char)netif_mac[3],
		    (unsigned char)netif_mac[4], (unsigned char)netif_mac[5]);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL netif rc=%d\n", rc);
		return 8;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.ssid = CONFIG_STA_SSID;
	cfg.psk = STA5_WPA2 ? STA5_PSK_VALUE : NULL;
	cfg.freq_mhz = CONFIG_STA_FREQ;
	cfg.bssid = NULL;
	if (CONFIG_STA_BSSID[0] != '\0') {
		if (sta5_parse_mac(CONFIG_STA_BSSID, cfg_bssid) == 0)
			cfg.bssid = cfg_bssid;
	}
	CASE_ANCHOR("iwl7260_sta5g: security wpa2=%d ssid=%s freq=%d\n",
		    STA5_WPA2 ? 1 : 0, CONFIG_STA_SSID, cfg.freq_mhz);
	rc = lkpi_sta_start(&cfg);
	CASE_ANCHOR("iwl7260_sta5g: sta start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL sta start rc=%d\n", rc);
		return 9;
	}

	if (sta5_wait_ready(&waited, &state, bssid) != 0) {
		lkpi_sta_counts(&eapol_tx, &eapol_rx, &eapol_drop, &ptk, &gtk,
				&key_fail);
		CASE_ANCHOR("iwl7260_sta5g: FAIL sta timeout state=%d "
			    "waited_ms=%u eapol_tx=%u rx=%u drop=%u ptk=%u "
			    "gtk=%u fail=%u\n",
			    state, waited, eapol_tx, eapol_rx, eapol_drop, ptk,
			    gtk, key_fail);
		return 10;
	}
	lkpi_sta_counts(&eapol_tx, &eapol_rx, &eapol_drop, &ptk, &gtk,
			&key_fail);
	CASE_ANCHOR("iwl7260_sta5g: sta state=%d waited_ms=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    state, waited, bssid[0], bssid[1], bssid[2], bssid[3],
		    bssid[4], bssid[5]);

	/* The band gate: the association's own frequency, read back from
	 * cfg80211 rather than assumed from the request. */
	freq = lkpi_nl80211_link_freq(dev);
	CASE_ANCHOR("iwl7260_sta5g: link freq=%d\n", freq);
	if (freq < 4900) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL associated on %d MHz, not 5 GHz\n",
			    freq);
		return 11;
	}

	if (STA5_WPA2) {
		CASE_ANCHOR("iwl7260_sta5g: sta eapol tx=%u rx=%u drop=%u\n",
			    eapol_tx, eapol_rx, eapol_drop);
		CASE_ANCHOR("iwl7260_sta5g: sta key ptk=%u gtk=%u fail=%u\n",
			    ptk, gtk, key_fail);
		if (ptk == 0u || gtk == 0u || key_fail != 0u) {
			CASE_ANCHOR("iwl7260_sta5g: FAIL key ptk=%u gtk=%u "
				    "fail=%u\n", ptk, gtk, key_fail);
			return 12;
		}
	}

	/* The station path never calls configure_filter; without this the
	 * firmware multicast filter stays NULL and broadcast ARP never reaches
	 * the board.  Allmulti is the bench's deliberate choice (same as G2). */
	dev->flags |= IFF_ALLMULTI;
	dev_set_rx_mode(dev);
	CASE_ANCHOR("iwl7260_sta5g: mcast allmulti=1\n");

	peer = sta5_peer();
	if (peer == NULL) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL no peer address\n");
		return 13;
	}
	CASE_ANCHOR("iwl7260_sta5g: peer ip=%s\n", peer);

	rc = lkpi_net_ping(peer, STA5_PING_COUNT, STA5_PING_SMALL,
			   STA5_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("iwl7260_sta5g: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    STA5_PING_COUNT, ping_rx, STA5_PING_SMALL, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != STA5_PING_COUNT) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL ping small rc=%d rx=%u\n", rc,
			    ping_rx);
		return 14;
	}
	rc = lkpi_net_ping(peer, STA5_PING_COUNT, STA5_PING_LARGE,
			   STA5_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("iwl7260_sta5g: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    STA5_PING_COUNT, ping_rx, STA5_PING_LARGE, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != STA5_PING_COUNT) {
		CASE_ANCHOR("iwl7260_sta5g: FAIL ping large rc=%d rx=%u\n", rc,
			    ping_rx);
		return 15;
	}

	if (STA5_WPA2) {
		CASE_ANCHOR("iwl7260_sta5g: PASS STA5W\n");
		return 0;
	}
	CASE_ANCHOR("iwl7260_sta5g: PASS STA5\n");
	return 0;
}
