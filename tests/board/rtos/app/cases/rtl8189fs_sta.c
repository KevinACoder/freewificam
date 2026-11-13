/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8189fs_sta.c
 * @brief  Case rtl8189fs_sta: the board's SDIO 8189FTV joins the open bench AP
 *         (C4 scan, C5 association, C6 ICMP) through the vendor stack's own
 *         cfg80211 ops.
 *
 * The attach is the C3 case's (SDIO backend, module_init, engine start, the
 * vendor probe's firmware and MAC init).  What is new is the control plane:
 * the case builds the real NL80211_CMD_TRIGGER_SCAN and NL80211_CMD_CONNECT
 * attribute streams through `lkpi_nl80211_*` and runs nl80211's own handlers,
 * which dispatch to the vendor driver's `.scan` / `.connect` ops.  The vendor
 * stack has its own MLME; cfg80211 is the control plane and mac80211 is not in
 * this data path.
 *
 * The console is quiet before the attach starts: the driver's runtime log
 * level goes to errors only, because the GT round proved a 115200 console
 * costs tens of milliseconds per line and the association budget is 25 ms on
 * the peer.  The attach's own firmware evidence belongs to the C3 arm.
 *
 * What this is not: WPA2 (no GT denominator yet), the AP role, monitor, and
 * the throughput arm (rk3568-rtl8189fs-c6t, same join then the phase table).
 *
 * Anchors:
 *   rtl8189fs_sta: core ready
 *   rtl8189fs_sta: initcalls run
 *   rtl8189fs_sta: wlan0 present=1 ifindex=<n> mac=<mac>
 *   rtl8189fs_sta: start rc=<n>
 *   rtl8189fs_sta: scan ssid=<ssid> freq=<mhz> rc=<n>
 *   rtl8189fs_sta: bss ssid=<ssid> bssid=<mac> freq=<mhz> signal=<dbm>
 *   rtl8189fs_sta: connect ssid=<ssid> bssid=<mac> freq=<mhz> rc=<n>
 *   rtl8189fs_sta: connected bssid=<mac> waited_ms=<n>
 *   rtl8189fs_sta: PASS C5
 *   rtl8189fs_sta: netif up name=wlan0 ip=<ip>/24 mac=<mac>
 *   rtl8189fs_sta: peer window ms=<n>
 *   rtl8189fs_sta: ping tx=<n> rx=<n> payload=<n> rtt_ms=<min>/<avg>/<max>
 *   rtl8189fs_sta: diag rx_frames=<n> tx_frames=<n> rx_bytes=<n> tx_bytes=<n> tx_fail=<n>
 *   rtl8189fs_sta: PASS C6
 *   rtl8189fs_sta: FAIL <reason>
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/lkpi_sdio_backend.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"
#include "linuxkpi_sdio.h"

#include <linux/etherdevice.h>
#include <linux/mmc/sdio_func.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>
#include <net/net_namespace.h>

#ifndef RTL8189FS_SSID
#define RTL8189FS_SSID		"labwifi-ar-ht20s"
#endif
#ifndef RTL8189FS_FREQ_MHZ
#define RTL8189FS_FREQ_MHZ	2432u
#endif
#ifndef RTL8189FS_BOARD_IP
#define RTL8189FS_BOARD_IP	"192.168.99.2"
#endif
#ifndef RTL8189FS_BOARD_MASK
#define RTL8189FS_BOARD_MASK	"255.255.255.0"
#endif
#ifndef RTL8189FS_AP_IP
#define RTL8189FS_AP_IP		"192.168.99.1"
#endif

#define STA_WAIT_MS		30000U
#define STA_POLL_MS		100U
#define STA_SCAN_WAIT_MS	10000U
#define STA_CONNECT_WAIT_MS	20000U
#define STA_PING_COUNT		20U
#define STA_PING_TIMEOUT_MS	300U
#define STA_PING_SMALL		56U
#define STA_PING_LARGE		1400U
#define STA_LOG_MAX		40U

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
		CASE_INFO("rtl8189fs_sta: drv %s\n", line);
		sta_log_lines++;
	}
}

static int
sta_wait_connected(struct net_device *dev, unsigned char bssid[6],
		   unsigned int *waited_ms)
{
	unsigned int waited = 0U;

	while (waited < STA_CONNECT_WAIT_MS) {
		int connected = lkpi_nl80211_connected(dev, bssid);

		if (connected == 1) {
			*waited_ms = waited;
			return 0;
		}
		if (connected < 0)
			return connected;
		osDelay(STA_POLL_MS);
		waited += STA_POLL_MS;
	}
	*waited_ms = waited;
	return -1;
}

int
case_rtl8189fs_sta_run(void)
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

	lkpi_core_init();
	CASE_ANCHOR("rtl8189fs_sta: core ready\n");
	lkpi_log_sink_register(sta_sink);

	rtw_power_mgnt = PS_MODE_ACTIVE;
	rtw_lps_level = LPS_OFF;
	rtw_drv_log_level = RTW_LOG_ERR;

	lkpi_sdio_fsl_install();
	lkpi_module_init_run();
	CASE_ANCHOR("rtl8189fs_sta: initcalls run\n");

	lkpi_sdio_start();

	while (waited < STA_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(STA_POLL_MS);
		waited += STA_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL no wlan0 after %u ms\n",
			    (unsigned)waited);
		return 1;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("rtl8189fs_sta: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	rc = dev_open(dev);
	CASE_ANCHOR("rtl8189fs_sta: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL start rc=%d\n", rc);
		return 2;
	}

	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL no wireless_dev\n");
		return 3;
	}

	/* --- C4: scan through the real nl80211 handler --------------------- */

	rc = lkpi_nl80211_trigger_scan(dev, RTL8189FS_SSID, RTL8189FS_FREQ_MHZ);
	CASE_ANCHOR("rtl8189fs_sta: scan ssid=%s freq=%u rc=%d\n",
		    RTL8189FS_SSID, RTL8189FS_FREQ_MHZ, rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL scan rc=%d\n", rc);
		return 4;
	}

	for (waited = 0U; waited < STA_SCAN_WAIT_MS; waited += STA_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, RTL8189FS_SSID, bssid,
					  &freq, &sig) == 0)
			goto found;
		osDelay(STA_POLL_MS);
	}

	/* A directed scan the peer did not answer in time: the wildcard shape is
	 * the bench's fallback, the same one the AC 7260 round uses. */
	rc = lkpi_nl80211_trigger_scan(dev, NULL, 0);
	CASE_ANCHOR("rtl8189fs_sta: scan wildcard rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL wildcard scan rc=%d\n", rc);
		return 5;
	}
	for (waited = 0U; waited < STA_SCAN_WAIT_MS; waited += STA_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, RTL8189FS_SSID, bssid,
					  &freq, &sig) == 0)
			goto found;
		osDelay(STA_POLL_MS);
	}
	CASE_ANCHOR("rtl8189fs_sta: FAIL bss timeout (scan started, no BSS)\n");
	return 6;

found:
	CASE_ANCHOR("rtl8189fs_sta: bss ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x freq=%d signal=%d\n",
		    RTL8189FS_SSID, bssid[0], bssid[1], bssid[2], bssid[3],
		    bssid[4], bssid[5], freq, sig / 100);

	/* --- C5: connect through the real nl80211 handler ------------------ */

	rc = lkpi_nl80211_connect(dev, RTL8189FS_SSID, bssid, (unsigned int)freq);
	CASE_ANCHOR("rtl8189fs_sta: connect ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x freq=%d rc=%d\n",
		    RTL8189FS_SSID, bssid[0], bssid[1], bssid[2], bssid[3],
		    bssid[4], bssid[5], freq, rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL connect rc=%d\n", rc);
		return 7;
	}

	rc = sta_wait_connected(dev, bssid, &connected_ms);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL connect timeout waited_ms=%u\n",
			    connected_ms);
		(void)lkpi_nl80211_disconnect(dev, 3u);
		return 8;
	}
	CASE_ANCHOR("rtl8189fs_sta: connected bssid=%02x:%02x:%02x:%02x:%02x:%02x waited_ms=%u\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    connected_ms);
	CASE_ANCHOR("rtl8189fs_sta: PASS C5\n");

	osDelay(200u);

	/* --- C6: the lwIP netif and two ICMP rounds ------------------------ */

	rc = lkpi_net_wlan_up(RTL8189FS_BOARD_IP, RTL8189FS_BOARD_MASK,
			      RTL8189FS_AP_IP, netif_mac);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL netif up rc=%d\n", rc);
		return 9;
	}
	CASE_ANCHOR("rtl8189fs_sta: netif up name=wlan0 ip=%s/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    RTL8189FS_BOARD_IP, (unsigned char)netif_mac[0],
		    (unsigned char)netif_mac[1], (unsigned char)netif_mac[2],
		    (unsigned char)netif_mac[3], (unsigned char)netif_mac[4],
		    (unsigned char)netif_mac[5]);

	/* The peer arm (the Pi pings the board) is driven from the host by this
	 * anchor, the same window the C5 round uses. */
	CASE_ANCHOR("rtl8189fs_sta: peer window ms=6000\n");
	osDelay(6000u);

	rc = lkpi_net_ping(RTL8189FS_AP_IP, STA_PING_COUNT, STA_PING_SMALL,
			   STA_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtl8189fs_sta: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    STA_PING_COUNT, ping_rx, STA_PING_SMALL, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != STA_PING_COUNT) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL ping small rc=%d rx=%u\n", rc,
			    ping_rx);
		return 10;
	}

	rc = lkpi_net_ping(RTL8189FS_AP_IP, STA_PING_COUNT, STA_PING_LARGE,
			   STA_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtl8189fs_sta: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    STA_PING_COUNT, ping_rx, STA_PING_LARGE, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != STA_PING_COUNT) {
		CASE_ANCHOR("rtl8189fs_sta: FAIL ping large rc=%d rx=%u\n", rc,
			    ping_rx);
		return 11;
	}

	lkpi_net_diag(&diag);
	CASE_ANCHOR("rtl8189fs_sta: diag rx_frames=%lu tx_frames=%lu rx_bytes=%lu tx_bytes=%lu tx_fail=%lu\n",
		    diag.rx_frames, diag.tx_frames, diag.rx_bytes, diag.tx_bytes,
		    diag.tx_fail);
	CASE_ANCHOR("rtl8189fs_sta: PASS C6\n");

	return 0;
}
