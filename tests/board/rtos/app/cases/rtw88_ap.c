/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw88_ap.c
 * @brief  Case rtw88_ap: the RTL8821CU becomes a 2.4 GHz AP (A1), the Pi's
 *         MT7921AU joins it (A2), the data plane is proven both ways (A3).
 *
 * The attach is rtw88_c5's (CherryUSB on the EHCI pair, the URB engine, the
 * compiled-in rtw8821c firmware, wlan0, dev_open).  What changes is the role
 * and the owner of the MLME:
 *
 *   - The AP MLME is the imported NXP hostapd (third-party/wpa_supplicant-rtos,
 *     CONFIG_HOSTAP_AP), the same one the AC 7260, the 8189FTV and the 8851BU
 *     AP arms use.  hostapd builds the beacon's head/tail and drives
 *     NL80211_CMD_START_AP; mac80211 keeps the AP state machine, and rtw88
 *     pushes the beacon to the firmware through its reserved-page path
 *     (rtw_fw_download_rsvd_page + rtw_send_rsvd_page_h2c, driven by
 *     BSS_CHANGED_BEACON).  The firmware sends the beacons, not this task.
 *   - Because there is no netlink socket in this image, A2's witness is
 *     mac80211's own AP station table (lkpi_nl80211_sta_count, the atomic
 *     iterator), with hostapd's count as the second opinion.
 *   - The channel comes from nl80211's chandef and from hostapd's config; both
 *     are the same fact here (ch5/2432), and the case prints the wiphy's
 *     channel flags and `cfg80211_reg_can_beacon` before touching the role.
 *
 * There is no phase table (A4) in this case on purpose: the round's target is
 * the functional role (runs/2026-11-14-rtw88-8821cu-sta-ap/PREREG.md), and a
 * throughput verdict needs its own pre-registration and its own image.
 *
 * Console discipline: the 8189 AP round and the GT AP path both showed that a
 * per-frame line on the association/beacon path is paid in milliseconds.  The
 * sink prints only the firmware/start whitelist and a bounded set of driver
 * error lines; no data-path line is formatted per frame.
 *
 * The anchors are the 8851BU AP case's, with this case's prefix, so the same
 * tools/rtos/ap-round.sh drives both arms:
 *   rtw88_ap: core ready
 *   rtw88_ap: usb backend ready
 *   rtw88_ap: initcalls run
 *   rtw88_ap: usb drivers=<n>
 *   rtw88_ap: fw registered=0 name=rtw88/rtw8821c_fw.bin
 *   rtw88_ap: usb buses=1,0
 *   rtw88_ap: usb bus=<n> hcd=<ehci|xhci> base=0x... started
 *   rtw88_ap: firmware <driver line>
 *   rtw88_ap: wlan0 present=1 ifindex=<n> mac=<mac> iftype=<n>
 *   rtw88_ap: chan freq=2432 present=<0|1> flags=0x<hex> can_beacon=<0|1>
 *   rtw88_ap: set_iface rc=<n> type=<n>
 *   rtw88_ap: start rc=<n>
 *   rtw88_ap: netif up name=wl0 ip=<ip>/24 mac=<mac>
 *   rtw88_ap: security wpa2=<0|1> ssid=<ssid> channel=<n> bcn_int=<n> dtim_req=<n>
 *   rtw88_ap: hostap start rc=<n> ready=<0|1>
 *   rtw88_ap: link freq=<mhz>
 *   rtw88_ap: PASS A1
 *   rtw88_ap: waiting station ms=<n>
 *   rtw88_ap: sta n=<n> hostap=<n> mac=<mac>
 *   rtw88_ap: PASS A2
 *   rtw88_ap: peer window ms=<n>   (the peer's ping window)
 *   rtw88_ap: ping payload=<n> tx=<n> rx=<n> dup=<n> rtt_ms=<min>/<avg>/<max>
 *   rtw88_ap: link window rx_frames=<n> rx_foreign=<n> rx_no_netif=<n>
 *              rx_input_fail=<n> rx_bytes=<n> tx_frames=<n> tx_fail=<n>
 *   rtw88_ap: PASS A3
 *   rtw88_ap: PASS AP
 *   rtw88_ap: FAIL <reason>
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

/* CherryUSB first (see rtw88_c3.c): its <stdlib.h> must be parsed before the
 * Linux face's compiler-attributes header is. */
#include "usbh_core.h"
#include "usb_glue.h"
#include "usb_domain.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/hostap_ap.h"
#include "linux80211/hostap_drv.h"	/* the hw_mode values, plain C */
#include "linux80211/net.h"
#include "linux80211/diag.h"
#include "linuxkpi_usb.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

/* --- this AP ---------------------------------------------------------------
 *
 * The SSID must not collide with the bench AP's (labwifi-ar-ht20s) or with the
 * other cards' AP arms (rtos-7260-ap, rtos-8189-ap, rtos-8851-ap): the Pi's STA
 * scans for this one.  The channel is the station round's ch5, the channel the
 * GT station arm uses on this card, so the two arms put the same BSS on the
 * same air.
 */
#ifndef RTW88_AP_SSID
#define RTW88_AP_SSID		"rtos-8821-ap"
#endif
#ifndef RTW88_AP_FREQ_MHZ
#define RTW88_AP_FREQ_MHZ	2432u
#endif
#ifndef RTW88_AP_CHANNEL
#define RTW88_AP_CHANNEL	5u
#endif
#ifndef RTW88_AP_BOARD_IP
#define RTW88_AP_BOARD_IP	"192.168.99.1"
#endif
#ifndef RTW88_AP_BOARD_MASK
#define RTW88_AP_BOARD_MASK	"255.255.255.0"
#endif
#ifndef RTW88_AP_PEER_IP
#define RTW88_AP_PEER_IP	"192.168.99.2"
#endif

#define AP_BEACON_INT		100u
#define AP_DTIM			2u
#define AP_WAIT_MS		60000U
#define AP_POLL_MS		100U
#define AP_STA_WAIT_MS		45000U
#define AP_PEER_WINDOW_MS	12000U
#define AP_SETTLE_MS		3000U
#define AP_PING_COUNT		20U
#define AP_PING_TIMEOUT_MS	300U
#define AP_PING_SMALL		56U
#define AP_PING_LARGE		1400U
#define AP_LOG_MAX		120U

#if CONFIG_RTW88_USB_BUS == 0
static const unsigned int ap_buses[] = { 3U, 2U };
#define AP_BUS_LIST_TXT	"3,2"
#else
static const unsigned int ap_buses[] = { 1U, 0U };
#define AP_BUS_LIST_TXT	"1,0"
#endif

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);
void usb_osal_timer_service_start(void);
int lkpi_fw_rtw8821c_fw_register(void);

static unsigned int ap_log_lines;

static void
ap_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	if (strstr(line, "Firmware version") != NULL) {
		CASE_ANCHOR("rtw88_ap: firmware %s\n", line);
		return;
	}
	if (strstr(line, "loaded firmware") != NULL) {
		CASE_ANCHOR("rtw88_ap: fw image %s\n", line);
		return;
	}
	if (ap_log_lines >= AP_LOG_MAX)
		return;
	if (strstr(line, "lps") != NULL || strstr(line, "LPS") != NULL) {
		/* the leave-LPS wait is 100 ms (ps.c:144); the station round's
		 * RTT spikes were exactly that size and its sink dropped the
		 * line.  Forward it here too. */
		CASE_INFO("rtw88_ap: drv %s\n", line);
		ap_log_lines++;
		return;
	}
	if (strstr(line, "failed to") != NULL ||
	    strstr(line, "error") != NULL ||
	    strstr(line, "timeout") != NULL) {
		CASE_INFO("rtw88_ap: drv %s\n", line);
		ap_log_lines++;
	}
}

static void
ap_usb_ledger(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtw88_ap: usb submits=%u completes=%u refused=%u "
		    "stage_overflow=%u slots_exhausted=%u rx=%u rx_err=%u\n",
		    st.submits, st.completed, st.refused, st.stage_overflow,
		    st.slots_exhausted, st.rx_completes, st.rx_errors);
}

static int
ap_usb_up(void)
{
	unsigned int i;
	int started = 0;

	lkpi_usb_cherryusb_init();
	CASE_ANCHOR("rtw88_ap: usb backend ready\n");

	lkpi_module_init_run();
	CASE_ANCHOR("rtw88_ap: initcalls run\n");

	lkpi_usb_init();
	CASE_ANCHOR("rtw88_ap: usb drivers=%u\n",
		    (unsigned)lkpi_usb_driver_count_get());

	lkpi_usb_force_sync_set(false);
	if (lkpi_fw_rtw8821c_fw_register() != 0) {
		CASE_ANCHOR("rtw88_ap: FAIL firmware blob registration\n");
		return -1;
	}
	CASE_ANCHOR("rtw88_ap: fw registered=0 name=rtw88/rtw8821c_fw.bin\n");

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtw88_ap: usb buses=%s\n", AP_BUS_LIST_TXT);
	for (i = 0U; i < sizeof(ap_buses) / sizeof(ap_buses[0]); i++) {
		unsigned int bus = ap_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtw88_ap: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtw88_ap: bus%u usbh_initialize failed\n", bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtw88_ap: usb bus=%u hcd=%s base=0x%lx started\n", bus,
			    usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
			    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	return started;
}

/* --- A2: wait for the station, read mac80211's own table ------------------ */
static int
ap_sta_wait(struct net_device *dev, unsigned char mac[6], unsigned int *n_out,
	    unsigned int *hostap_out)
{
	unsigned int waited = 0U;
	unsigned char macs[4][6];
	unsigned int n = 0U;

	while (waited < AP_STA_WAIT_MS) {
		(void)lkpi_hostap_poll();
		memset(macs, 0, sizeof(macs));
		n = 0U;
		if (lkpi_nl80211_sta_count(dev, macs, 4U, &n) != 0)
			n = 0U;
		if (n > 0U)
			break;
		osDelay(AP_POLL_MS);
		waited += AP_POLL_MS;
	}
	*n_out = n;
	*hostap_out = (unsigned int)lkpi_hostap_sta_count();
	if (n > 0U)
		memcpy(mac, macs[0], 6);
	return n > 0U ? 0 : -1;
}

static int
ap_ping(const char *what, unsigned int payload)
{
	unsigned int rx = 0U;
	unsigned int rtt_min = 0U, rtt_avg = 0U, rtt_max = 0U;
	int rc;

	rc = lkpi_net_ping(RTW88_AP_PEER_IP, AP_PING_COUNT, payload,
			   AP_PING_TIMEOUT_MS, &rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtw88_ap: ping payload=%u tx=%u rx=%u dup=%u "
		    "rtt_ms=%u/%u/%u\n", payload, AP_PING_COUNT, rx,
		    rx > AP_PING_COUNT ? rx - AP_PING_COUNT : 0u,
		    rtt_min, rtt_avg, rtt_max);
	if (rx < AP_PING_COUNT) {
		CASE_ANCHOR("rtw88_ap: FAIL ping %s rc=%d rx=%u\n", what, rc,
			    rx);
		return -1;
	}
	return 0;
}

int
case_rtw88_ap_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;
	struct lkpi_hostap_cfg ap_cfg;
	struct lkpi_net_diag d0, d1;
	unsigned char mac[6];
	unsigned char peer_mac[6];
	char netif_mac[6];
	unsigned int waited = 0U;
	unsigned int sta_n = 0U;
	unsigned int hostap_n = 0U;
	int can_beacon;
	int freq;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("rtw88_ap: core ready\n");
	lkpi_log_sink_register(ap_sink);

	rc = ap_usb_up();
	if (rc <= 0) {
		CASE_ANCHOR("rtw88_ap: FAIL no controller started\n");
		ap_usb_ledger();
		return 1;
	}

	while (waited < AP_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(AP_POLL_MS);
		waited += AP_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("rtw88_ap: FAIL no wlan0 after %u ms\n",
			    (unsigned)waited);
		ap_usb_ledger();
		return 2;
	}
	memcpy(mac, dev->dev_addr, 6);
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("rtw88_ap: FAIL no wireless_dev\n");
		return 3;
	}
	CASE_ANCHOR("rtw88_ap: wlan0 present=1 ifindex=%d "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x iftype=%d\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		    (int)wdev->iftype);

	/* The channel must be in the driver's band and beaconable: this is the
	 * discriminator when START_AP returns -EINVAL or the role change
	 * silently falls back.  Same shape as the 7260 and 8851BU AP cases. */
	chan = ieee80211_get_channel(wdev->wiphy, (int)RTW88_AP_FREQ_MHZ);
	can_beacon = 0;
	if (chan != NULL) {
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_HT20);
		can_beacon = cfg80211_reg_can_beacon(wdev->wiphy, &chandef,
						     NL80211_IFTYPE_AP) ? 1 : 0;
	}
	CASE_ANCHOR("rtw88_ap: chan freq=%u present=%d flags=0x%08x "
		    "can_beacon=%d\n",
		    RTW88_AP_FREQ_MHZ, chan != NULL ? 1 : 0,
		    chan != NULL ? (unsigned)chan->flags : 0u, can_beacon);
	if (chan == NULL || !can_beacon) {
		CASE_ANCHOR("rtw88_ap: FAIL channel not beaconable\n");
		return 4;
	}

	/* --- A1: the default STA interface becomes the AP ---------------- */
	if (netif_running(dev))
		(void)dev_close(dev);
	rc = lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_AP);
	CASE_ANCHOR("rtw88_ap: set_iface rc=%d type=%d\n", rc,
		    lkpi_nl80211_iftype(dev));
	if (rc != 0 || lkpi_nl80211_iftype(dev) != NL80211_IFTYPE_AP) {
		CASE_ANCHOR("rtw88_ap: FAIL set_iface rc=%d type=%d\n", rc,
			    lkpi_nl80211_iftype(dev));
		return 5;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("rtw88_ap: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtw88_ap: FAIL dev_open rc=%d\n", rc);
		return 6;
	}

	/* The lwIP netif comes before hostapd: the NXP entry derives the
	 * interface name it configures from the port's uAP netif. */
	rc = lkpi_net_wlan_up(RTW88_AP_BOARD_IP, RTW88_AP_BOARD_MASK, NULL,
			      netif_mac);
	if (rc != 0) {
		CASE_ANCHOR("rtw88_ap: FAIL netif up rc=%d\n", rc);
		return 7;
	}
	CASE_ANCHOR("rtw88_ap: netif up name=wl0 ip=%s/24 "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    RTW88_AP_BOARD_IP, (unsigned char)netif_mac[0],
		    (unsigned char)netif_mac[1], (unsigned char)netif_mac[2],
		    (unsigned char)netif_mac[3], (unsigned char)netif_mac[4],
		    (unsigned char)netif_mac[5]);

	/* --- A1: hostapd brings the AP up (beacon via src/ap/beacon.c) --- */
	(void)memset(&ap_cfg, 0, sizeof(ap_cfg));
	ap_cfg.ifname = "wlan0";
	ap_cfg.ssid = RTW88_AP_SSID;
	ap_cfg.channel = (unsigned int)RTW88_AP_CHANNEL;
	ap_cfg.beacon_int = AP_BEACON_INT;
	ap_cfg.dtim = AP_DTIM;
	ap_cfg.passphrase = NULL;
	ap_cfg.freq_mhz = (unsigned int)RTW88_AP_FREQ_MHZ;
	ap_cfg.hw_mode = LKPI_HOSTAP_MODE_11G;
	CASE_ANCHOR("rtw88_ap: security wpa2=%d ssid=%s channel=%u "
		    "bcn_int=%u dtim_req=%u\n", 0, RTW88_AP_SSID,
		    (unsigned int)RTW88_AP_CHANNEL, AP_BEACON_INT, AP_DTIM);
	rc = lkpi_hostap_start(&ap_cfg);
	CASE_ANCHOR("rtw88_ap: hostap start rc=%d ready=%d\n", rc,
		    lkpi_hostap_ready());
	if (rc != 0 || !lkpi_hostap_ready()) {
		CASE_ANCHOR("rtw88_ap: FAIL hostap start rc=%d\n", rc);
		return 10;
	}

	freq = lkpi_nl80211_link_freq(dev);
	CASE_ANCHOR("rtw88_ap: link freq=%d\n", freq);
	CASE_ANCHOR("rtw88_ap: PASS A1\n");

	/* --- A2: the Pi's association, read from mac80211's table --------- */
	CASE_ANCHOR("rtw88_ap: waiting station ms=%u\n", AP_STA_WAIT_MS);
	rc = ap_sta_wait(dev, peer_mac, &sta_n, &hostap_n);
	CASE_ANCHOR("rtw88_ap: sta n=%u hostap=%u "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    sta_n, hostap_n, peer_mac[0], peer_mac[1], peer_mac[2],
		    peer_mac[3], peer_mac[4], peer_mac[5]);
	if (rc != 0) {
		CASE_ANCHOR("rtw88_ap: FAIL no station after %u ms\n",
			    AP_STA_WAIT_MS);
		return 11;
	}
	CASE_ANCHOR("rtw88_ap: PASS A2\n");

	/* The peer's ping window starts here, after the station exists: the
	 * first 8189 AP boot pinged a BSS the Pi had not joined yet. */
	CASE_ANCHOR("rtw88_ap: peer window ms=%u\n", AP_PEER_WINDOW_MS);
	osDelay(AP_PEER_WINDOW_MS);
	osDelay(AP_SETTLE_MS);

	/* --- A3: both directions, then the receive account ----------------- */
	/* The first seconds after the station appears carry the board's ARP
	 * resolution for the peer; a counted round that starts inside them
	 * measures the neighbour state, not the path (the C5 case reads the
	 * same lesson).  One throwaway echo, then the ledger. */
	{
		unsigned int warm_rx = 0U, warm_i;
		unsigned int wmin = 0U, wavg = 0U, wmax = 0U;
		int wrc = -1;

		for (warm_i = 0U; warm_i < 20U; warm_i++) {
			wrc = lkpi_net_ping(RTW88_AP_PEER_IP, 1U, AP_PING_SMALL,
					    AP_PING_TIMEOUT_MS, &warm_rx,
					    &wmin, &wavg, &wmax);
			if (wrc == 0 && warm_rx == 1U)
				break;
			osDelay(250u);
		}
		CASE_ANCHOR("rtw88_ap: warmup tries=%u rx=%u rc=%d\n",
			    warm_i + 1U, warm_rx, wrc);
	}
	osDelay(AP_SETTLE_MS);

	(void)memset(&d0, 0, sizeof(d0));
	lkpi_net_diag(&d0);
	rc = ap_ping("small", AP_PING_SMALL);
	(void)memset(&d1, 0, sizeof(d1));
	lkpi_net_diag(&d1);
	/* the receive account of the first round, printed whether it passed or
	 * not: a 0/n needs the port's counters next to it */
	CASE_ANCHOR("rtw88_ap: link window rx_frames=%lu rx_foreign=%lu "
		    "rx_no_netif=%lu rx_input_fail=%lu rx_bytes=%lu "
		    "tx_frames=%lu tx_fail=%lu\n",
		    d1.rx_frames - d0.rx_frames,
		    d1.rx_foreign - d0.rx_foreign,
		    d1.rx_no_netif - d0.rx_no_netif,
		    d1.rx_input_fail - d0.rx_input_fail,
		    d1.rx_bytes - d0.rx_bytes,
		    d1.tx_frames - d0.tx_frames, d1.tx_fail - d0.tx_fail);
	if (rc != 0)
		return 12;
	(void)memset(&d0, 0, sizeof(d0));
	lkpi_net_diag(&d0);
	if (ap_ping("large", AP_PING_LARGE) != 0)
		return 13;
	CASE_ANCHOR("rtw88_ap: PASS A3\n");

	ap_usb_ledger();
	CASE_ANCHOR("rtw88_ap: PASS AP\n");
	return 0;
}
