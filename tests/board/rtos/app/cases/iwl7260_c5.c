/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_c5.c
 * @brief  Case iwl7260_c5: the AC 7260 joins the open bench AP (C5-open) and
 *         moves ICMP through the lwIP netif (C6-open).
 *
 * The bring-up is C4's (core, firmware, PCI, wlan0, dev_open) and the scan is
 * C4's helper.  What is new is the connection: NL80211_CMD_CONNECT is built as
 * the real attribute stream `iw connect` would send and dispatched through
 * nl80211's own handler chain; mac80211 runs auth/assoc asynchronously, and the
 * case waits (bounded) for cfg80211 to mark the station connected.  Then the
 * lwIP netif is assembled over wlan0 with the lab's static address and two
 * ICMP rounds (64 B and 1400 B, matching the GT baseline) are sent.
 *
 * What this is not: the WPA2 round.  The bench AP is open; there is no EAPOL,
 * no key install and no supplicant.  C5-WPA2 is the next handoff, and it will
 * reuse the connect helper and the lwIP egress proven here.
 *
 * Anchors:
 *   iwl7260_c5: core ready
 *   iwl7260_c5: fw registered=<0|err>
 *   iwl7260_c5: initcalls run
 *   iwl7260_c5: pci devs=<n>
 *   iwl7260_c5: wlan0 present=1 ifindex=<n> mac=<mac>
 *   iwl7260_c5: start rc=<n>
 *   iwl7260_c5: scan ssid=<ssid> freq=<mhz> rc=<n>
 *   iwl7260_c5: bss ssid=<ssid> bssid=<mac> freq=<mhz> signal=<dbm>
 *   iwl7260_c5: connect ssid=<ssid> bssid=<mac> freq=<mhz> rc=<n>
 *   iwl7260_c5: connected bssid=<mac> waited_ms=<n>
 *   iwl7260_c5: PASS C5
 *   iwl7260_c5: netif up name=<n> ip=<ip>/<mask> mac=<mac>
 *   iwl7260_c5: ping tx=<n> rx=<n> payload=<n> rtt_ms=<min>/<avg>/<max>
 *   iwl7260_c5: diag rx_frames=<n> tx_frames=<n> rx_bytes=<n> tx_bytes=<n> tx_fail=<n>
 *   iwl7260_c5: PASS C6
 *   iwl7260_c5: PASS
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"
#include "linuxkpi_pci.h"
#include "adapter.h"

#include <linux/etherdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#define C5_SSID			"labwifi-ar-ht20s"
#define C5_FREQ_MHZ		2432u
#define C5_BOARD_IP		"192.168.99.2"
#define C5_BOARD_MASK		"255.255.255.0"
#define C5_AP_IP		"192.168.99.1"
#define C5_WAIT_MS		40000U
#define C5_POLL_MS		100U
#define C5_SCAN_WAIT_MS		10000U
#define C5_CONNECT_WAIT_MS	20000U
#define C5_PING_COUNT		20U
#define C5_PING_TIMEOUT_MS	300U
#define C5_PING_SMALL		56U
#define C5_PING_LARGE		1400U
#define C5_LOG_MAX		120U

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
int lkpi_fw_iwl7260_fw_register(void);

static unsigned c5_log_lines;

static void
c5_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL || c5_log_lines >= C5_LOG_MAX)
		return;
	if (strstr(line, "loaded firmware version") != NULL)
		CASE_ANCHOR("iwl7260_c5: firmware %s\n", line);
	else if (strstr(line, "claimed") != NULL)
		CASE_INFO("iwl7260_c5: drv %s\n", line);
	else if (strstr(line, "authenticate") != NULL ||
		 strstr(line, "associate") != NULL ||
		 strstr(line, "deauthenticate") != NULL ||
		 strstr(line, "disconnect") != NULL ||
		 strstr(line, "add key") != NULL ||
		 strstr(line, "remove key") != NULL ||
		 strstr(line, "wlan0:") != NULL) {
		CASE_INFO("iwl7260_c5: drv %s\n", line);
		c5_log_lines++;
	}
}

static int
c5_wait_connected(struct net_device *dev, unsigned char bssid[6],
		  unsigned int *waited_ms)
{
	unsigned int waited = 0U;

	while (waited < C5_CONNECT_WAIT_MS) {
		int connected = lkpi_nl80211_connected(dev, bssid);

		if (connected == 1) {
			*waited_ms = waited;
			return 0;
		}
		if (connected < 0)
			return connected;
		osDelay(C5_POLL_MS);
		waited += C5_POLL_MS;
	}
	*waited_ms = waited;
	return -1;
}

int
case_iwl7260_c5_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	unsigned char bssid[6];
	unsigned char mac[6];
	char netif_mac[6];
	unsigned int waited = 0U;
	unsigned int connected_ms = 0U;
	unsigned int ping_rx = 0U;
	unsigned int rtt_min = 0U, rtt_avg = 0U, rtt_max = 0U;
	struct lkpi_net_diag diag;
	int freq = 0;
	int sig = 0;
	int rc;
	int c5_ok = 0;
	int c6_ok = 0;

	lkpi_core_init();
	CASE_ANCHOR("iwl7260_c5: core ready\n");
	lkpi_log_sink_register(c5_sink);

	lkpi_pcie_port_install();

	rc = lkpi_fw_iwl7260_fw_register();
	CASE_ANCHOR("iwl7260_c5: fw registered=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c5: FAIL firmware registration rc=%d\n", rc);
		return 1;
	}

	lkpi_module_init_run();
	CASE_ANCHOR("iwl7260_c5: initcalls run\n");

	lkpi_pci_start();
	CASE_ANCHOR("iwl7260_c5: pci devs=%u\n",
		    (unsigned)lkpi_pci_dev_count_get());

	while (waited < C5_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(C5_POLL_MS);
		waited += C5_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_c5: FAIL no wlan0 after %u ms\n",
			    (unsigned)waited);
		return 2;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("iwl7260_c5: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	rc = dev_open(dev);
	CASE_ANCHOR("iwl7260_c5: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c5: FAIL start rc=%d\n", rc);
		return 3;
	}

	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("iwl7260_c5: FAIL no wireless_dev\n");
		return 4;
	}

	/* --- C5-open: scan, connect, wait for the completion ---------------- */

	rc = lkpi_nl80211_trigger_scan(dev, C5_SSID, C5_FREQ_MHZ);
	CASE_ANCHOR("iwl7260_c5: scan ssid=%s freq=%u rc=%d\n",
		    C5_SSID, C5_FREQ_MHZ, rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c5: FAIL scan rc=%d\n", rc);
		return 5;
	}

	for (waited = 0U; waited < C5_SCAN_WAIT_MS; waited += C5_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, C5_SSID, bssid, &freq,
					  &sig) == 0)
			goto found;
		osDelay(C5_POLL_MS);
	}

	rc = lkpi_nl80211_trigger_scan(dev, NULL, 0);
	CASE_ANCHOR("iwl7260_c5: scan wildcard rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c5: FAIL wildcard scan rc=%d\n", rc);
		return 6;
	}
	for (waited = 0U; waited < C5_SCAN_WAIT_MS; waited += C5_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, C5_SSID, bssid, &freq,
					  &sig) == 0)
			goto found;
		osDelay(C5_POLL_MS);
	}
	CASE_ANCHOR("iwl7260_c5: FAIL bss timeout (scan started, no BSS)\n");
	return 7;

found:
	CASE_ANCHOR("iwl7260_c5: bss ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x freq=%d signal=%d\n",
		    C5_SSID, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4],
		    bssid[5], freq, sig / 100);

	rc = lkpi_nl80211_connect(dev, C5_SSID, bssid, (unsigned int)freq);
	CASE_ANCHOR("iwl7260_c5: connect ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x freq=%d rc=%d\n",
		    C5_SSID, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4],
		    bssid[5], freq, rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c5: FAIL connect rc=%d\n", rc);
		return 8;
	}

	rc = c5_wait_connected(dev, bssid, &connected_ms);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c5: FAIL connect timeout waited_ms=%u\n",
			    connected_ms);
		(void)lkpi_nl80211_disconnect(dev, 3u);
		return 9;
	}
	CASE_ANCHOR("iwl7260_c5: connected bssid=%02x:%02x:%02x:%02x:%02x:%02x waited_ms=%u\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    connected_ms);
	CASE_ANCHOR("iwl7260_c5: PASS C5\n");
	c5_ok = 1;

	/* The station entry is authorized by the association for an open AP;
	 * a short settle keeps the first ARP out of the completion's tail. */
	osDelay(200u);

	/* --- C6-open: the lwIP netif and two ICMP rounds ------------------- */

	rc = lkpi_net_wlan_up(C5_BOARD_IP, C5_BOARD_MASK, C5_AP_IP, netif_mac);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c5: FAIL netif up rc=%d\n", rc);
		return 10;
	}
	CASE_ANCHOR("iwl7260_c5: netif up name=wl0 ip=%s/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    C5_BOARD_IP, (unsigned char)netif_mac[0],
		    (unsigned char)netif_mac[1], (unsigned char)netif_mac[2],
		    (unsigned char)netif_mac[3], (unsigned char)netif_mac[4],
		    (unsigned char)netif_mac[5]);

	/* The peer-side arm (the Pi pings the board) is driven from the host by
	 * this anchor.  The window is the bench's, not the stack's: it gives the
	 * runner time to start `ping -c 20` against 192.168.99.2 while the netif
	 * is up, before this case's own ICMP rounds begin. */
	CASE_ANCHOR("iwl7260_c5: peer window ms=6000\n");
	osDelay(6000u);

	rc = lkpi_net_ping(C5_AP_IP, C5_PING_COUNT, C5_PING_SMALL,
			   C5_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("iwl7260_c5: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    C5_PING_COUNT, ping_rx, C5_PING_SMALL, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != C5_PING_COUNT) {
		CASE_ANCHOR("iwl7260_c5: FAIL ping small rc=%d rx=%u\n", rc,
			    ping_rx);
		return 11;
	}

	rc = lkpi_net_ping(C5_AP_IP, C5_PING_COUNT, C5_PING_LARGE,
			   C5_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("iwl7260_c5: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    C5_PING_COUNT, ping_rx, C5_PING_LARGE, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != C5_PING_COUNT) {
		CASE_ANCHOR("iwl7260_c5: FAIL ping large rc=%d rx=%u\n", rc,
			    ping_rx);
		return 12;
	}

	lkpi_net_diag(&diag);
	CASE_ANCHOR("iwl7260_c5: diag rx_frames=%lu tx_frames=%lu rx_bytes=%lu tx_bytes=%lu tx_fail=%lu\n",
		    diag.rx_frames, diag.tx_frames, diag.rx_bytes, diag.tx_bytes,
		    diag.tx_fail);
	CASE_ANCHOR("iwl7260_c5: PASS C6\n");
	c6_ok = 1;

	if (c5_ok && c6_ok) {
		CASE_ANCHOR("iwl7260_c5: PASS\n");
		return 0;
	}
	return 13;
}
