/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8189fs_sta_wpa2.c
 * @brief  Case rtl8189fs_sta_wpa2: the board's SDIO 8189FTV joins a WPA2-PSK/
 *         CCMP AP as a station (C5 secured), with the supplicant owning the
 *         connection.
 *
 * The attach is rtl8189fs_sta's (SDIO backend, module_init, engine start, the
 * vendor probe).  What changes is the connection owner: the NXP
 * wpa_supplicant-rtos port (the same one the AC 7260 STA WPA2 arm uses) scans,
 * associates and runs the 4-way handshake; this case starts it, waits for
 * COMPLETED, opens the receive filter, acquires the address and probes the
 * peer.  The vendor stack's own MLME stays the association owner below
 * cfg80211; mac80211 is not in this data path.
 *
 * The console is quiet before the supplicant starts: a 115200 console costs
 * tens of milliseconds per line and the peer's association budget is 25 ms
 * (the GT AP round measured 110 ms of RTW_INFO against it).  Only the join's
 * milestone lines are let through, and only through the case's own sink.
 *
 * Anchors:
 *   rtl8189fs_sta_wpa2: core ready
 *   rtl8189fs_sta_wpa2: initcalls run
 *   rtl8189fs_sta_wpa2: wlan0 present=1 ifindex=<n> mac=<mac>
 *   rtl8189fs_sta_wpa2: start rc=<n>
 *   rtl8189fs_sta_wpa2: netif up name=wlan0 ip=<ip>/24 mac=<mac>
 *   rtl8189fs_sta_wpa2: security wpa2=1 ssid=<ssid>
 *   rtl8189fs_sta_wpa2: sta start rc=<n>
 *   rtl8189fs_sta_wpa2: sta state=<n> waited_ms=<n> bssid=<mac>
 *   rtl8189fs_sta_wpa2: sta eapol tx=<n> rx=<n> drop=<n>
 *   rtl8189fs_sta_wpa2: sta key ptk=<n> gtk=<n> fail=<n>
 *   rtl8189fs_sta_wpa2: peer window ms=<n>
 *   rtl8189fs_sta_wpa2: ping tx=<n> rx=<n> payload=<n> rtt_ms=<min>/<avg>/<max>
 *   rtl8189fs_sta_wpa2: diag rx_frames=<n> tx_frames=<n> rx_bytes=<n> tx_bytes=<n> tx_fail=<n>
 *   rtl8189fs_sta_wpa2: PASS STAW
 *   rtl8189fs_sta_wpa2: FAIL <reason>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/lkpi_sdio_backend.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"
#include "linux80211/sta_wpa.h"
#include "linuxkpi_sdio.h"

#include <linux/etherdevice.h>
#include <linux/mmc/sdio_func.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>
#include <net/net_namespace.h>

#ifndef CONFIG_STA_SSID
#define CONFIG_STA_SSID		"labwifi-ar-ht20s"
#endif
#ifndef CONFIG_STA_FREQ
#define CONFIG_STA_FREQ		2432
#endif
#ifndef CONFIG_STA_BSSID
#define CONFIG_STA_BSSID	""
#endif
#ifndef CONFIG_STA_STATIC_IP
#define CONFIG_STA_STATIC_IP	"192.168.99.2"
#endif
#ifndef CONFIG_STA_STATIC_MASK
#define CONFIG_STA_STATIC_MASK	"255.255.255.0"
#endif
#ifndef CONFIG_STA_STATIC_GW
#define CONFIG_STA_STATIC_GW	"192.168.99.1"
#endif
#ifndef CONFIG_STA_PSK_EXTERNAL
#define CONFIG_STA_PSK_EXTERNAL	0
#endif
#ifndef CONFIG_STA_PSK
#define CONFIG_STA_PSK		"rtos-7260-wpa2-psk"
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
#define STA_PING_COUNT		20U
#define STA_PING_TIMEOUT_MS	300U
#define STA_PING_SMALL		56U
#define STA_PING_LARGE		1400U
#define STA_LOG_MAX		40U
#define STA_HOLD_MS		20000U

void lkpi_sdio_fsl_install(void);
void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));

extern int rtw_power_mgnt;
extern int rtw_lps_level;
extern unsigned int rtw_drv_log_level;

#define RTW_LOG_ERR		2	/* _DRV_ERR_ */
#define PS_MODE_ACTIVE		0
#define LPS_OFF			0

static unsigned sta_log_lines;

static void
sta_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL || sta_log_lines >= STA_LOG_MAX)
		return;
	/* Only the join's own milestone lines; everything else would be console
	 * time the association cannot afford. */
	if (strstr(line, "OnAssocReq") != NULL || strstr(line, "OnAssocRsp") != NULL ||
	    strstr(line, "joinbss") != NULL || strstr(line, "connect") != NULL) {
		CASE_INFO("rtl8189fs_sta_wpa2: drv %s\n", line);
		sta_log_lines++;
	}
}

static int
sta_parse_mac(const char *s, unsigned char out[6])
{
	unsigned int v[6];
	int i;

	if (s == NULL || s[0] == '\0')
		return -1;
	if (sscanf(s, "%x:%x:%x:%x:%x:%x",
		   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
		return -1;
	for (i = 0; i < 6; i++) {
		if (v[i] > 0xffu)
			return -1;
		out[i] = (unsigned char)v[i];
	}
	return 0;
}

static int
sta_wait_ready(struct net_device *dev, unsigned int *waited_ms, int *state,
	       unsigned char bssid[6])
{
	unsigned int waited = 0U;

	while (waited < STA_WAIT_MS) {
		*state = lkpi_sta_state();
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

int
case_rtl8189fs_sta_wpa2_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct lkpi_sta_cfg cfg;
	unsigned char bssid[6] = { 0 };
	unsigned char cfg_bssid[6];
	unsigned char mac[6];
	char netif_mac[6];
	unsigned int waited = 0U;
	unsigned int ping_rx = 0U;
	unsigned int rtt_min = 0U, rtt_avg = 0U, rtt_max = 0U;
	unsigned int eapol_tx = 0U, eapol_rx = 0U, eapol_drop = 0U;
	unsigned int ptk = 0U, gtk = 0U, key_fail = 0U;
	int state = -1;
	struct lkpi_net_diag diag;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("rtl8189fs_sta_wpa2: core ready\n");
	lkpi_log_sink_register(sta_sink);

	rtw_power_mgnt = PS_MODE_ACTIVE;
	rtw_lps_level = LPS_OFF;
	rtw_drv_log_level = RTW_LOG_ERR;

	lkpi_sdio_fsl_install();
	lkpi_module_init_run();
	CASE_ANCHOR("rtl8189fs_sta_wpa2: initcalls run\n");

	lkpi_sdio_start();

	while (waited < STA_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(STA_POLL_MS);
		waited += STA_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("rtl8189fs_sta_wpa2: FAIL no wlan0 after %u ms\n",
			    (unsigned)waited);
		return 1;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	rc = dev_open(dev);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sta_wpa2: FAIL start rc=%d\n", rc);
		return 2;
	}

	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("rtl8189fs_sta_wpa2: FAIL no wireless_dev\n");
		return 3;
	}

	memcpy(netif_mac, mac, 6);
	rc = lkpi_net_wlan_up(CONFIG_STA_STATIC_IP, CONFIG_STA_STATIC_MASK,
			      CONFIG_STA_STATIC_GW, netif_mac);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: netif up name=wlan0 ip=%s/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    CONFIG_STA_STATIC_IP, mac[0], mac[1], mac[2], mac[3], mac[4],
		    mac[5]);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sta_wpa2: FAIL netif rc=%d\n", rc);
		return 4;
	}
	CASE_ANCHOR("rtl8189fs_sta_wpa2: security wpa2=1 ssid=%s\n",
		    CONFIG_STA_SSID);

	memset(&cfg, 0, sizeof(cfg));
	cfg.ssid = CONFIG_STA_SSID;
	cfg.psk = STA_PSK_VALUE;
	cfg.freq_mhz = CONFIG_STA_FREQ;
	cfg.bssid = NULL;
	if (CONFIG_STA_BSSID[0] != '\0' &&
	    sta_parse_mac(CONFIG_STA_BSSID, cfg_bssid) == 0)
		cfg.bssid = cfg_bssid;
	rc = lkpi_sta_start(&cfg);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: sta start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sta_wpa2: FAIL sta start rc=%d\n", rc);
		return 5;
	}

	if (sta_wait_ready(dev, &waited, &state, bssid) != 0) {
		lkpi_sta_counts(&eapol_tx, &eapol_rx, &eapol_drop, &ptk, &gtk,
				&key_fail);
		CASE_ANCHOR("rtl8189fs_sta_wpa2: FAIL sta timeout state=%d waited_ms=%u eapol_tx=%u rx=%u drop=%u ptk=%u gtk=%u fail=%u\n",
			    state, waited, eapol_tx, eapol_rx, eapol_drop, ptk,
			    gtk, key_fail);
		lkpi_sta_stop();
		return 6;
	}
	lkpi_sta_counts(&eapol_tx, &eapol_rx, &eapol_drop, &ptk, &gtk,
			&key_fail);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: sta state=%d waited_ms=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    state, waited, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4],
		    bssid[5]);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: sta eapol tx=%u rx=%u drop=%u\n",
		    eapol_tx, eapol_rx, eapol_drop);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: sta key ptk=%u gtk=%u fail=%u\n",
		    ptk, gtk, key_fail);
	/* `fail` counts key-install errors the supplicant recovered from (the
	 * handshake may still complete with the keys installed); only a missing
	 * PTK is fatal here, and the count is printed so a retry is visible. */
	if (ptk == 0U) {
		CASE_ANCHOR("rtl8189fs_sta_wpa2: FAIL key ptk=%u gtk=%u fail=%u\n",
			    ptk, gtk, key_fail);
		lkpi_sta_stop();
		return 7;
	}

	/* The peer arm (the Pi pings the board) is driven from the host by this
	 * anchor, the same window the C5 round uses. */
	CASE_ANCHOR("rtl8189fs_sta_wpa2: peer window ms=6000\n");
	osDelay(6000u);

	rc = lkpi_net_ping(CONFIG_STA_STATIC_GW, STA_PING_COUNT, STA_PING_SMALL,
			   STA_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    STA_PING_COUNT, ping_rx, STA_PING_SMALL, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != STA_PING_COUNT) {
		CASE_ANCHOR("rtl8189fs_sta_wpa2: FAIL ping small rc=%d rx=%u\n",
			    rc, ping_rx);
		lkpi_sta_stop();
		return 8;
	}

	rc = lkpi_net_ping(CONFIG_STA_STATIC_GW, STA_PING_COUNT, STA_PING_LARGE,
			   STA_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    STA_PING_COUNT, ping_rx, STA_PING_LARGE, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != STA_PING_COUNT) {
		CASE_ANCHOR("rtl8189fs_sta_wpa2: FAIL ping large rc=%d rx=%u\n",
			    rc, ping_rx);
		lkpi_sta_stop();
		return 9;
	}

	lkpi_net_diag(&diag);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: diag rx_frames=%lu tx_frames=%lu rx_bytes=%lu tx_bytes=%lu tx_fail=%lu\n",
		    diag.rx_frames, diag.tx_frames, diag.rx_bytes, diag.tx_bytes,
		    diag.tx_fail);
	CASE_ANCHOR("rtl8189fs_sta_wpa2: PASS STAW\n");

	/* The reverse direction (the AP pings the board) is driven from the host
	 * after the PASS anchor; the hold keeps the link up for that window. */
	CASE_ANCHOR("rtl8189fs_sta_wpa2: hold ms=%u\n", STA_HOLD_MS);
	osDelay(STA_HOLD_MS);

	return 0;
}
