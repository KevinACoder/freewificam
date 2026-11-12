/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    hostap_nl80211.c
 * @brief   The Linux-world half of the hostap driver seam: the nl80211 calls
 *          the imported hostapd driver_ops table needs, in this port's world.
 *
 * hostapd's own driver wrapper (src/drivers/driver_freertos.c in the NXP tree)
 * talks to the NXP wifi middleware.  This file replaces that bottom: the same
 * operations expressed through the real cfg80211/nl80211 handler chain this
 * image already runs (linuxkpi_nl80211_probe.c) - START_AP for the beacon
 * template, FRAME for the management responses, NEW/DEL/SET_STATION for the
 * station table.  The hostap world never sees a Linux type; hostap_drv.h is the
 * only contact (see its header for why the two namespaces cannot share a TU).
 */
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/errno.h>
#include <linux/nl80211.h>
#include <net/cfg80211.h>

#include "linux80211/hostap_drv.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/sta_drv.h"

/* the lwIP adapter's transmit entry; the AP's EAPOL uses the same frame path
 * as the supplicant would on Linux (a packet-socket send on wlan0) */
#include "lkpinetif_seam.h"

static struct net_device *hostap_dev;

/* This image has exactly one wireless net_device and it keeps mac80211's
 * default name; the imported NXP entry instead names the interface after its
 * lwIP netif ("wl<N>", from netifapi_netif_index_to_name()).  A name the net
 * namespace does not know is therefore resolved to the image's wlan device. */
#define LKPI_HOSTAP_NETDEV	"wlan0"

int
lkpi_hostap_drv_open(const char *ifname)
{
	if (ifname == NULL)
		return -EINVAL;
	hostap_dev = dev_get_by_name(&init_net, ifname);
	if (hostap_dev == NULL && ifname[0] == 'w' && ifname[1] == 'l')
		hostap_dev = dev_get_by_name(&init_net, LKPI_HOSTAP_NETDEV);
	if (hostap_dev == NULL)
		return -ENODEV;
	return 0;
}

void
lkpi_hostap_drv_close(void)
{
	hostap_dev = NULL;
}

const unsigned char *
lkpi_hostap_drv_mac(void)
{
	if (hostap_dev == NULL || hostap_dev->dev_addr == NULL)
		return NULL;
	return hostap_dev->dev_addr;
}

static void
lkpi_hostap_fill_chan(struct lkpi_hostap_chan *out,
		      const struct ieee80211_channel *ch)
{
	int flags = 0;

	/* mirrors HOSTAPD_CHAN_DISABLED / NO_IR / RADAR */
	if ((ch->flags & IEEE80211_CHAN_DISABLED) != 0u)
		flags |= LKPI_HOSTAP_CHAN_DISABLED;
	if ((ch->flags & IEEE80211_CHAN_NO_IR) != 0u)
		flags |= LKPI_HOSTAP_CHAN_NO_IR;
	if ((ch->flags & IEEE80211_CHAN_RADAR) != 0u)
		flags |= LKPI_HOSTAP_CHAN_RADAR;

	/* The conversion is cfg80211's, not an arithmetic guess: the 2.4 GHz
	 * form ((freq - 2407) / 5) returns 548 for 5180 MHz, which is the
	 * "channel 548" that would reach hostapd's hw_features instead of 36. */
	out->chan = ieee80211_frequency_to_channel((int)ch->center_freq);
	out->freq = (int)ch->center_freq;
	out->flags = flags;
	out->allowed_bw = LKPI_HOSTAP_CHAN_WIDTH_20;
}

/* One wiphy band as a hostapd hw mode.  hostapd's hw_mode==a path depends on
 * the mode's own rate table (6..54 Mbit/s OFDM) and on its HT capability being
 * the 5 GHz band's, not the 2.4 GHz one: copying the 2.4 GHz caps onto an
 * 11a mode would advertise a 40 MHz/TX-STBC mask the 5 GHz band does not
 * necessarily share. */
static void
lkpi_hostap_fill_mode(struct lkpi_hostap_mode *mode,
		      const struct ieee80211_supported_band *band)
{
	int i;

	memset(mode, 0, sizeof(*mode));
	mode->num_channels = band->n_channels < LKPI_HOSTAP_MAX_CHAN ?
	    band->n_channels : LKPI_HOSTAP_MAX_CHAN;
	for (i = 0; i < mode->num_channels; i++)
		lkpi_hostap_fill_chan(&mode->channels[i], &band->channels[i]);
	mode->num_rates = band->n_bitrates < LKPI_HOSTAP_MAX_RATES ?
	    band->n_bitrates : LKPI_HOSTAP_MAX_RATES;
	for (i = 0; i < mode->num_rates; i++)
		mode->rates[i] = (int)band->bitrates[i].bitrate;
	if (band->ht_cap.ht_supported) {
		mode->ht_supported = 1;
		mode->ht_capab = band->ht_cap.cap;
		memcpy(mode->mcs_set, band->ht_cap.mcs.rx_mask, 10);
		mode->mcs_set[10] = (unsigned char)(band->ht_cap.mcs.rx_highest & 0xffu);
		mode->mcs_set[11] = (unsigned char)(band->ht_cap.mcs.rx_highest >> 8);
		mode->mcs_set[12] = band->ht_cap.mcs.tx_params;
		mode->a_mpdu_params =
		    (unsigned char)((band->ht_cap.ampdu_factor & 3u) |
				    ((band->ht_cap.ampdu_density & 7u) << 2));
	}
}

int
lkpi_hostap_drv_modes(struct lkpi_hostap_mode *modes, int max_modes)
{
	struct wireless_dev *wdev;
	struct wiphy *wiphy;
	const struct ieee80211_supported_band *band;
	int n = 0;

	if (hostap_dev == NULL || modes == NULL || max_modes < 1)
		return -EINVAL;
	wdev = hostap_dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL)
		return -EINVAL;
	wiphy = wdev->wiphy;

	/* 802.11a first when the wiphy has a 5 GHz band: hostapd picks
	 * iface->current_mode by hw_mode, so the order only matters for a
	 * hw_mode==any configuration - but a table that does not list 11a at
	 * all is what keeps `hw_mode=a` from ever working. */
	band = wiphy->bands[NL80211_BAND_5GHZ];
	if (band != NULL && n < max_modes) {
		lkpi_hostap_fill_mode(&modes[n], band);
		modes[n].mode = LKPI_HOSTAP_MODE_11A;
		n++;
	}

	band = wiphy->bands[NL80211_BAND_2GHZ];
	if (band != NULL && n < max_modes) {
		lkpi_hostap_fill_mode(&modes[n], band);
		modes[n].mode = LKPI_HOSTAP_MODE_11G;
		n++;
	}

	/* 802.11b: the four DSSS rates on the 2.4 GHz channel set */
	if (band != NULL && n < max_modes) {
		static const int b_rates[4] = { 10, 20, 55, 110 };

		lkpi_hostap_fill_mode(&modes[n], band);
		modes[n].mode = LKPI_HOSTAP_MODE_11B;
		modes[n].num_rates = 4;
		memcpy(modes[n].rates, b_rates, sizeof(b_rates));
		n++;
	}

	if (n == 0)
		return -ENODEV;
	return n;
}

int
lkpi_hostap_drv_chan_of(unsigned int freq_mhz)
{
	return ieee80211_frequency_to_channel((int)freq_mhz);
}

int
lkpi_hostap_drv_set_ap(const struct lkpi_hostap_ap_params *params)
{
	char ssid[33];
	size_t len;
	unsigned int width;

	if (params == NULL || params->head == NULL || params->ssid == NULL)
		return -EINVAL;
	len = params->ssid_len;
	if (len > 32u)
		return -EINVAL;
	memcpy(ssid, params->ssid, len);
	ssid[len] = '\0';
	width = NL80211_CHAN_WIDTH_20;
	if (params->bw_mhz >= 160)
		width = NL80211_CHAN_WIDTH_160;
	else if (params->bw_mhz >= 80)
		width = NL80211_CHAN_WIDTH_80;
	else if (params->bw_mhz >= 40)
		width = NL80211_CHAN_WIDTH_40;
	return lkpi_nl80211_start_ap(hostap_dev, ssid,
				     (unsigned int)params->freq,
				     width,
				     (unsigned int)params->beacon_int,
				     (unsigned int)params->dtim,
				     params->head, (unsigned int)params->head_len,
				     params->tail, (unsigned int)params->tail_len,
				     params->privacy);
}

int
lkpi_hostap_drv_send_mlme(const void *frame, size_t len, int noack)
{
	return lkpi_nl80211_mgmt_tx(hostap_dev, frame, (unsigned int)len,
				    noack);
}

int
lkpi_hostap_drv_sta_add(const struct lkpi_hostap_sta_add *sta)
{
	struct lkpi_ap_sta_add add;

	if (sta == NULL || sta->addr == NULL || sta->rates == NULL)
		return -EINVAL;
	memset(&add, 0, sizeof(add));
	memcpy(add.mac, sta->addr, 6);
	add.aid = sta->aid;
	add.listen_interval = (uint16_t)sta->listen_interval;
	add.capab = sta->capability;
	add.rates = sta->rates;
	add.rates_len = (unsigned int)sta->rates_len;
	add.ht_capa = sta->ht_capa_len >= 26u ? sta->ht_capa : NULL;
	add.wmm = sta->wmm ? (const unsigned char *)1 : NULL;
	return lkpi_nl80211_new_station(hostap_dev, &add);
}

int
lkpi_hostap_drv_sta_del(const unsigned char *addr)
{
	return lkpi_nl80211_del_station(hostap_dev, addr);
}

int
lkpi_hostap_drv_sta_authorized(const unsigned char *addr, int authorized)
{
	return lkpi_nl80211_sta_set_authorized(hostap_dev, addr, authorized);
}

/* --- the STA role (G2, docs/HANDOFF-AP-WPA2.md §P2) --------------------- */

static struct wiphy *
lkpi_sta_wiphy(void)
{
	if (hostap_dev == NULL || hostap_dev->ieee80211_ptr == NULL)
		return NULL;
	return hostap_dev->ieee80211_ptr->wiphy;
}

int
lkpi_sta_drv_scan(const char *ssid, unsigned int ssid_len,
		  unsigned int freq_mhz)
{
	char name[33];

	if (hostap_dev == NULL)
		return -ENODEV;
	if (ssid == NULL)
		return lkpi_nl80211_trigger_scan(hostap_dev, NULL, freq_mhz);
	if (ssid_len == 0u || ssid_len > 32u)
		return -EINVAL;
	memcpy(name, ssid, ssid_len);
	name[ssid_len] = '\0';
	return lkpi_nl80211_trigger_scan(hostap_dev, name, freq_mhz);
}

int
lkpi_sta_drv_connect(const struct lkpi_sta_connect *c)
{
	if (hostap_dev == NULL || c == NULL || c->ssid == NULL ||
	    c->ssid_len == 0u || c->ssid_len > 32u)
		return -EINVAL;
	return lkpi_nl80211_connect_ex(hostap_dev, c->ssid, c->ssid_len,
				       c->bssid, c->freq_mhz, c->ie,
				       c->ie_len, c->privacy);
}

int
lkpi_sta_drv_connected(unsigned char bssid[6])
{
	if (hostap_dev == NULL)
		return -ENODEV;
	return lkpi_nl80211_connected(hostap_dev, bssid);
}

int
lkpi_sta_drv_assoc_progress(void)
{
	if (hostap_dev == NULL)
		return -ENODEV;
	/* the BSS-table bit needs a name; the wait loop only needs the two
	 * state bits, and the associate path already proved the BSS was there */
	return lkpi_nl80211_assoc_progress(hostap_dev, NULL, 0u);
}

int
lkpi_sta_drv_disconnect(unsigned int reason)
{
	if (hostap_dev == NULL)
		return -ENODEV;
	return lkpi_nl80211_disconnect(hostap_dev, reason);
}

int
lkpi_sta_drv_mac(unsigned char mac[6])
{
	if (hostap_dev == NULL || hostap_dev->dev_addr == NULL || mac == NULL)
		return -ENODEV;
	memcpy(mac, hostap_dev->dev_addr, 6);
	return 0;
}

int
lkpi_sta_drv_bss_get(const char *ssid, unsigned int ssid_len,
		     struct lkpi_sta_bss *out,
		     unsigned char *ie, unsigned int ie_max,
		     unsigned int *ie_len)
{
	struct wiphy *wiphy = lkpi_sta_wiphy();

	if (wiphy == NULL || out == NULL)
		return -ENODEV;
	return lkpi_nl80211_bss_get(wiphy, ssid, ssid_len, out->bssid,
				    &out->freq, &out->signal,
				    &out->beacon_int, &out->capability,
				    ie, ie_max, ie_len);
}

int
lkpi_hostap_drv_eapol_tx(const void *frame, size_t len)
{
	if (frame == NULL || len == 0u)
		return -EINVAL;
	return lkpinetif_tx_frame(frame, len);
}

int
lkpi_hostap_drv_set_key(const struct lkpi_hostap_key *key)
{
	struct lkpi_nl80211_key k;
	int rc;

	if (key == NULL || key->key == NULL || key->key_len == 0u)
		return -EINVAL;
	memset(&k, 0, sizeof(k));
	k.cipher = key->cipher;
	k.key_idx = key->key_idx;
	k.mac = key->pairwise ? key->addr : NULL;
	k.key = key->key;
	k.key_len = (unsigned int)key->key_len;
	k.seq = key->seq;
	k.seq_len = (unsigned int)key->seq_len;
	rc = lkpi_nl80211_new_key(hostap_dev, &k);
	if (rc != 0)
		return rc;
	if (key->default_key)
		rc = lkpi_nl80211_set_default_key(hostap_dev, key->key_idx,
						  key->pairwise ? 0 : 1);
	return rc;
}

int
lkpi_hostap_drv_del_key(const unsigned char *addr, int key_idx)
{
	return lkpi_nl80211_del_key(hostap_dev, key_idx, addr);
}

int
lkpi_sta_drv_eapol_tx(const void *frame, size_t len)
{
	if (frame == NULL || len == 0u)
		return -EINVAL;
	return lkpinetif_tx_frame(frame, len);
}
