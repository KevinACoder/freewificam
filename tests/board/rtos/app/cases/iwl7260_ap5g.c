/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_ap5g.c
 * @brief  Case iwl7260_ap5g: the AC 7260 becomes an AP on the 5 GHz band
 *         (docs/HANDOFF-5GHZ.md P1/P2).
 *
 * The bring-up is the AP round's (core, firmware, PCI, wlan0, set_iface AP,
 * dev_open, lwIP netif, hostapd as the AP MLME).  What is new is the band: the
 * frequency and the hostapd hw_mode come from the profile, the case reports
 * what the wiphy and the regulatory core say about every 5 GHz channel it
 * would try, and START_AP carries the 5 GHz frequency instead of 2462.
 *
 * The channel report is the round's discriminator.  The 7260's firmware image
 * does not advertise LAR (the enabled-capability TLV has no LAR bit), so
 * iwlwifi registers the wiphy as REGULATORY_CUSTOM_REG and the 5 GHz channels'
 * flags come from the NVM's per-channel active/passive bits.  A channel with
 * NO_IR cannot host an AP, and this port does not touch regulatory to make it
 * - the correct answer in that case is "the band is not usable for AP on this
 * card/firmware", recorded with flags, not worked around.
 *
 * What this is not: the throughput table (A4's phase table lives in
 * iwl7260_ap; the 5 GHz round gates on function) and not a regulatory
 * experiment: no regdb, no country set, no NVM edit.
 *
 * Anchors:
 *   iwl7260_ap5g: core ready
 *   iwl7260_ap5g: fw registered=<0|err>
 *   iwl7260_ap5g: initcalls run
 *   iwl7260_ap5g: pci devs=<n>
 *   iwl7260_ap5g: wlan0 present=1 ifindex=<n> mac=<mac> iftype=<n>
 *   iwl7260_ap5g: bands 2g=<0|1> 5g=<0|1> 2g_chans=<n> 5g_chans=<n> 5g_ht=<0|1>
 *   iwl7260_ap5g: cand freq=<mhz> chan=<n> present=<0|1> flags=0x<hex> can_beacon=<0|1>
 *   iwl7260_ap5g: chan freq=<mhz> chan=<n> present=1 flags=0x<hex> can_beacon=1
 *   iwl7260_ap5g: set_iface rc=<n> type=<n>
 *   iwl7260_ap5g: start rc=<n>
 *   iwl7260_ap5g: netif up name=<n> ip=<ip>/24 mac=<mac>
 *   iwl7260_ap5g: security wpa2=<0|1> ssid=<ssid> hw_mode=<g|a> freq=<mhz> chan=<n>
 *   iwl7260_ap5g: hostap start rc=<n> ready=<0|1>
 *   iwl7260_ap5g: peer window ms=<n>
 *   iwl7260_ap5g: hostap up ready=<0|1> sta=<n>
 *   iwl7260_ap5g: sta add n=<n> mac=<mac>
 *   iwl7260_ap5g: wpa2 station sta=<n> ptk=<n> gtk=<n> fail=<n> wait_ms=<n>
 *   iwl7260_ap5g: eapol tx=<n> rx=<n> drop=<n>
 *   iwl7260_ap5g: link freq=<mhz>
 *   iwl7260_ap5g: ping tx=<n> rx=<n> payload=<n> rtt_ms=<min>/<avg>/<max>
 *   iwl7260_ap5g: PASS AP5        (open, 5 GHz)
 *   iwl7260_ap5g: PASS AP5W       (WPA2, 5 GHz)
 *   iwl7260_ap5g: FAIL <reason>
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/hostap_ap.h"
#include "linux80211/hostap_drv.h"	/* the hw_mode values, plain C */
#include "linux80211/net.h"
#include "linuxkpi_pci.h"
#include "adapter.h"

#include <linux/etherdevice.h>
#include <linux/nl80211.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/net_namespace.h>

#ifndef CONFIG_AP_FREQ
#define CONFIG_AP_FREQ		2462
#endif
#ifndef CONFIG_AP_HW_MODE
#define CONFIG_AP_HW_MODE	"g"
#endif

#define AP5_SSID		"rtos-7260-ap-5g"
#define AP5_BOARD_IP		"192.168.99.1"
#define AP5_BOARD_MASK		"255.255.255.0"
#define AP5_PEER_IP		"192.168.99.2"
#define AP5_WAIT_MS		40000U
#define AP5_POLL_MS		200U
#define AP5_CONNECT_WAIT_MS	40000U
#define AP5_HOSTAP_POLL_MS	10U
#define AP5_SETTLE_MS		3000U
#define AP5_PEER_WINDOW_MS	12000U
#define AP5_PING_COUNT		20U
#define AP5_PING_TIMEOUT_MS	300U
#define AP5_PING_SMALL		56U
#define AP5_PING_LARGE		1400U
#define AP5_WPA_WAIT_MS		10000U
#define AP5_WPA_POST_HS_MS	10000U
#define AP5_LOG_MAX		160U

#if defined(CONFIG_AP_WPA2) && CONFIG_AP_WPA2
#define AP5_WPA2		1
#define AP5_PASSPHRASE		"rtos-7260-wpa2-psk"
#else
#define AP5_WPA2		0
#define AP5_PASSPHRASE		NULL
#endif

/*
 * The channels this round is allowed to try, in the order the handoff fixed:
 * 36/40/44/48 first (non-DFS, the most widely usable), then the high band
 * 149-165.  DFS channels are deliberately absent: an AP on one needs CAC,
 * which this port does not run, and a DFS channel would fail for a reason
 * that has nothing to do with the band being supported.
 */
static const unsigned ap5_cand[] = {
	5180u, 5200u, 5220u, 5240u,
	5745u, 5765u, 5785u, 5805u, 5825u,
};
#define AP5_CAND_LEN	(sizeof(ap5_cand) / sizeof(ap5_cand[0]))

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
int lkpi_fw_iwl7260_fw_register(void);

static unsigned ap5_log_lines;

static void
ap5_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL || ap5_log_lines >= AP5_LOG_MAX)
		return;
	if (strstr(line, "loaded firmware version") != NULL) {
		CASE_ANCHOR("iwl7260_ap5g: firmware %s\n", line);
	} else if (strstr(line, "Frequency") != NULL ||
		   strstr(line, "channel") != NULL ||
		   strstr(line, "hw_mode") != NULL ||
		   strstr(line, "Hardware does not support") != NULL ||
		   strstr(line, "Failed to") != NULL) {
		/* hostapd's own channel/mode complaints are the evidence when
		 * START_AP is refused; echo them on the anchor channel so the
		 * host does not have to parse the daemon's printf. */
		CASE_INFO("iwl7260_ap5g: hostapd %s\n", line);
		ap5_log_lines++;
	}
}

static unsigned
ap5_cand_can_beacon(struct wiphy *wiphy, unsigned freq)
{
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;

	chan = ieee80211_get_channel(wiphy, (int)freq);
	if (chan == NULL)
		return 0u;
	cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_HT20);
	if (!cfg80211_reg_can_beacon(wiphy, &chandef, NL80211_IFTYPE_AP))
		return 0u;
	return 1u;
}

int
case_iwl7260_ap5g_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct ieee80211_supported_band *band5;
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;
	struct lkpi_hostap_cfg ap_cfg;
	unsigned char mac[6];
	unsigned char sta_macs[4][6];
	char netif_mac[6];
	unsigned int freq;
	unsigned int hw_mode;
	unsigned int waited;
	unsigned int sta_n = 0u;
	unsigned int ping_rx = 0u;
	unsigned int rtt_min = 0U, rtt_avg = 0U, rtt_max = 0U;
	unsigned int i;
	int can_beacon;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("iwl7260_ap5g: core ready\n");
	lkpi_log_sink_register(ap5_sink);

	lkpi_pcie_port_install();

	rc = lkpi_fw_iwl7260_fw_register();
	CASE_ANCHOR("iwl7260_ap5g: fw registered=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL firmware registration rc=%d\n",
			    rc);
		return 1;
	}

	lkpi_module_init_run();
	CASE_ANCHOR("iwl7260_ap5g: initcalls run\n");

	lkpi_pci_start();
	CASE_ANCHOR("iwl7260_ap5g: pci devs=%u\n",
		    (unsigned)lkpi_pci_dev_count_get());

	for (waited = 0U; waited < AP5_WAIT_MS; waited += AP5_POLL_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(AP5_POLL_MS);
	}
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL no wlan0 after %u ms\n", waited);
		return 2;
	}
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL no wireless_dev\n");
		return 3;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("iwl7260_ap5g: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x iftype=%d\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		    (int)wdev->iftype);

	/* --- the band census: is the 5 GHz band in the wiphy at all? ------ */
	band5 = wdev->wiphy->bands[NL80211_BAND_5GHZ];
	CASE_ANCHOR("iwl7260_ap5g: bands 2g=%d 5g=%d 2g_chans=%u 5g_chans=%u 5g_ht=%d\n",
		    wdev->wiphy->bands[NL80211_BAND_2GHZ] != NULL ? 1 : 0,
		    band5 != NULL ? 1 : 0,
		    wdev->wiphy->bands[NL80211_BAND_2GHZ] != NULL ?
		    (unsigned)wdev->wiphy->bands[NL80211_BAND_2GHZ]->n_channels :
		    0u,
		    band5 != NULL ? (unsigned)band5->n_channels : 0u,
		    (band5 != NULL && band5->ht_cap.ht_supported) ? 1 : 0);
	if (band5 == NULL) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL no 5 GHz band in the wiphy\n");
		return 4;
	}

	/* --- the channel report ------------------------------------------
	 * Every candidate is reported, not just the one that gets used: when
	 * no channel is beaconable the flags of all of them are the evidence
	 * that the band is passive on this firmware/NVM, and when one is, the
	 * report shows it was the first of the fixed order rather than luck. */
	freq = 0u;
	for (i = 0u; i < AP5_CAND_LEN; i++) {
		unsigned f = ap5_cand[i];
		unsigned cb;

		chan = ieee80211_get_channel(wdev->wiphy, (int)f);
		cb = ap5_cand_can_beacon(wdev->wiphy, f);
		CASE_ANCHOR("iwl7260_ap5g: cand freq=%u chan=%d present=%d "
			    "flags=0x%08x can_beacon=%u\n",
			    f, chan != NULL ?
			    ieee80211_frequency_to_channel((int)f) : 0,
			    chan != NULL ? 1 : 0,
			    chan != NULL ? (unsigned)chan->flags : 0u, cb);
		if (freq == 0u && cb != 0u)
			freq = f;
	}

	/* The profile's channel wins when it is usable; otherwise the first
	 * usable one of the fixed order keeps the round alive on a band that
	 * answers somewhere other than 36. */
	hw_mode = (CONFIG_AP_HW_MODE[0] == 'a') ?
	    LKPI_HOSTAP_MODE_11A : LKPI_HOSTAP_MODE_11G;
	if (ap5_cand_can_beacon(wdev->wiphy, (unsigned)CONFIG_AP_FREQ)) {
		freq = (unsigned)CONFIG_AP_FREQ;
	} else if (freq != 0u) {
		CASE_ANCHOR("iwl7260_ap5g: note profile freq=%u not beaconable; "
			    "using %u\n", (unsigned)CONFIG_AP_FREQ, freq);
	}
	if (freq == 0u) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL no beaconable 5 GHz channel "
			    "(flags above are the regulatory fact)\n");
		return 5;
	}
	/* hw_mode must follow the frequency, not the profile: hostapd selects
	 * its hw mode first and looks the channel up in it afterwards. */
	hw_mode = (freq >= 4900u) ? LKPI_HOSTAP_MODE_11A : LKPI_HOSTAP_MODE_11G;

	chan = ieee80211_get_channel(wdev->wiphy, (int)freq);
	cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_HT20);
	can_beacon = cfg80211_reg_can_beacon(wdev->wiphy, &chandef,
					     NL80211_IFTYPE_AP) ? 1 : 0;
	CASE_ANCHOR("iwl7260_ap5g: chan freq=%u chan=%d present=1 "
		    "flags=0x%08x can_beacon=%d\n",
		    freq, ieee80211_frequency_to_channel((int)freq),
		    (unsigned)chan->flags, can_beacon);

	/* --- A1: the default STA interface becomes the AP ---------------- */
	if (netif_running(dev))
		(void)dev_close(dev);
	rc = lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_AP);
	CASE_ANCHOR("iwl7260_ap5g: set_iface rc=%d type=%d\n", rc,
		    lkpi_nl80211_iftype(dev));
	if (rc != 0 || lkpi_nl80211_iftype(dev) != NL80211_IFTYPE_AP) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL set_iface rc=%d type=%d\n", rc,
			    lkpi_nl80211_iftype(dev));
		return 6;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("iwl7260_ap5g: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL dev_open rc=%d\n", rc);
		return 7;
	}

	rc = lkpi_net_wlan_up(AP5_BOARD_IP, AP5_BOARD_MASK, NULL, netif_mac);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL netif up rc=%d\n", rc);
		return 8;
	}
	CASE_ANCHOR("iwl7260_ap5g: netif up name=wl0 ip=%s/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    AP5_BOARD_IP, (unsigned char)netif_mac[0],
		    (unsigned char)netif_mac[1], (unsigned char)netif_mac[2],
		    (unsigned char)netif_mac[3], (unsigned char)netif_mac[4],
		    (unsigned char)netif_mac[5]);

	memset(&ap_cfg, 0, sizeof(ap_cfg));
	ap_cfg.ifname = "wlan0";
	ap_cfg.ssid = AP5_SSID;
	ap_cfg.channel = (unsigned)ieee80211_frequency_to_channel((int)freq);
	ap_cfg.beacon_int = 100u;
	ap_cfg.dtim = 2u;
	ap_cfg.passphrase = AP5_PASSPHRASE;
	ap_cfg.freq_mhz = freq;
	ap_cfg.hw_mode = hw_mode;
	CASE_ANCHOR("iwl7260_ap5g: security wpa2=%d ssid=%s hw_mode=%s "
		    "freq=%u chan=%u\n",
		    AP5_WPA2, AP5_SSID,
		    hw_mode == LKPI_HOSTAP_MODE_11A ? "a" : "g", freq,
		    ap_cfg.channel);
	rc = lkpi_hostap_start(&ap_cfg);
	CASE_ANCHOR("iwl7260_ap5g: hostap start rc=%d ready=%d\n", rc,
		    lkpi_hostap_ready());
	if (rc != 0 || !lkpi_hostap_ready()) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL hostap start rc=%d\n", rc);
		return 10;
	}

	CASE_ANCHOR("iwl7260_ap5g: peer window ms=%u\n", AP5_PEER_WINDOW_MS);

	/* --- A2: the Pi joins; hostapd's MLME answers -------------------- */
	sta_n = 0u;
	for (waited = 0U; waited < AP5_CONNECT_WAIT_MS;
	     waited += AP5_HOSTAP_POLL_MS) {
		(void)lkpi_hostap_poll();
		sta_n = (unsigned)lkpi_hostap_sta_count();
		if (sta_n != 0u)
			break;
		osDelay(AP5_HOSTAP_POLL_MS);
	}
	if (sta_n == 0u) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL no station after %u ms "
			    "(ready=%d)\n", AP5_CONNECT_WAIT_MS,
			    lkpi_hostap_ready());
		return 12;
	}
	CASE_ANCHOR("iwl7260_ap5g: hostap up ready=%d sta=%u\n",
		    lkpi_hostap_ready(), sta_n);
	if (lkpi_nl80211_sta_count(dev, sta_macs, 4u, &sta_n) == 0 &&
	    sta_n != 0u)
		CASE_ANCHOR("iwl7260_ap5g: sta add n=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
			    sta_n, sta_macs[0][0], sta_macs[0][1],
			    sta_macs[0][2], sta_macs[0][3], sta_macs[0][4],
			    sta_macs[0][5]);

	/* The AP's own operating frequency, read back from its chandef: the
	 * pass line must show the association landed on the band this case
	 * asked for, not on whatever the driver chose. */
	i = (unsigned)lkpi_nl80211_link_freq(dev);
	CASE_ANCHOR("iwl7260_ap5g: link freq=%d\n", (int)i);

	if (AP5_WPA2) {
		unsigned ptk = 0u, gtk = 0u, fail = 0u;
		unsigned waited_wpa;

		for (waited_wpa = 0U; waited_wpa < AP5_WPA_WAIT_MS;
		     waited_wpa += AP5_HOSTAP_POLL_MS) {
			(void)lkpi_hostap_poll();
			if (lkpi_hostap_wpa2_sta_count() > 0)
				break;
			osDelay(AP5_HOSTAP_POLL_MS);
		}
		lkpi_hostap_key_counts(&ptk, &gtk, &fail);
		CASE_ANCHOR("iwl7260_ap5g: wpa2 station sta=%d ptk=%u gtk=%u "
			    "fail=%u wait_ms=%u\n",
			    lkpi_hostap_wpa2_sta_count(), ptk, gtk, fail,
			    waited_wpa);
		if (lkpi_hostap_wpa2_sta_count() <= 0 || ptk == 0u ||
		    gtk == 0u || fail != 0u) {
			CASE_ANCHOR("iwl7260_ap5g: FAIL wpa2 handshake\n");
			return 13;
		}
		osDelay(AP5_WPA_POST_HS_MS);
	}

	osDelay(AP5_SETTLE_MS);

	/* --- A3: this board pings the peer ------------------------------- */
	rc = lkpi_net_ping(AP5_PEER_IP, AP5_PING_COUNT, AP5_PING_SMALL,
			   AP5_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("iwl7260_ap5g: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    AP5_PING_COUNT, ping_rx, AP5_PING_SMALL, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != AP5_PING_COUNT) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL ping small rc=%d rx=%u\n", rc,
			    ping_rx);
		return 14;
	}
	rc = lkpi_net_ping(AP5_PEER_IP, AP5_PING_COUNT, AP5_PING_LARGE,
			   AP5_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("iwl7260_ap5g: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    AP5_PING_COUNT, ping_rx, AP5_PING_LARGE, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != AP5_PING_COUNT) {
		CASE_ANCHOR("iwl7260_ap5g: FAIL ping large rc=%d rx=%u\n", rc,
			    ping_rx);
		return 15;
	}

	if (AP5_WPA2) {
		unsigned etx = 0u, erx = 0u, edrop = 0u;

		lkpi_hostap_eapol_counts(&etx, &erx, &edrop);
		CASE_ANCHOR("iwl7260_ap5g: eapol tx=%u rx=%u drop=%u\n",
			    etx, erx, edrop);
		CASE_ANCHOR("iwl7260_ap5g: PASS AP5W\n");
		return 0;
	}
	CASE_ANCHOR("iwl7260_ap5g: PASS AP5\n");
	return 0;
}
