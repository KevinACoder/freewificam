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

	out->chan = (int)((ch->center_freq - 2407u) / 5u);
	out->freq = (int)ch->center_freq;
	out->flags = flags;
	out->allowed_bw = LKPI_HOSTAP_CHAN_WIDTH_20;
}

int
lkpi_hostap_drv_modes(struct lkpi_hostap_mode *modes, int max_modes)
{
	struct wireless_dev *wdev;
	struct wiphy *wiphy;
	struct ieee80211_supported_band *band;
	int n = 0;
	int i;

	if (hostap_dev == NULL || modes == NULL || max_modes < 1)
		return -EINVAL;
	wdev = hostap_dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL)
		return -EINVAL;
	wiphy = wdev->wiphy;
	band = wiphy->bands[NL80211_BAND_2GHZ];
	if (band == NULL)
		return -ENODEV;

	/* 802.11g (and the HT caps the driver advertises on it) */
	memset(&modes[n], 0, sizeof(modes[n]));
	modes[n].mode = LKPI_HOSTAP_MODE_11G;
	modes[n].num_channels = band->n_channels < LKPI_HOSTAP_MAX_CHAN ?
	    band->n_channels : LKPI_HOSTAP_MAX_CHAN;
	for (i = 0; i < modes[n].num_channels; i++)
		lkpi_hostap_fill_chan(&modes[n].channels[i], &band->channels[i]);
	modes[n].num_rates = band->n_bitrates < LKPI_HOSTAP_MAX_RATES ?
	    band->n_bitrates : LKPI_HOSTAP_MAX_RATES;
	for (i = 0; i < modes[n].num_rates; i++)
		modes[n].rates[i] = (int)band->bitrates[i].bitrate;
	if (band->ht_cap.ht_supported) {
		modes[n].ht_supported = 1;
		modes[n].ht_capab = band->ht_cap.cap;
		memcpy(modes[n].mcs_set, band->ht_cap.mcs.rx_mask, 10);
		modes[n].mcs_set[10] = (unsigned char)(band->ht_cap.mcs.rx_highest & 0xffu);
		modes[n].mcs_set[11] = (unsigned char)(band->ht_cap.mcs.rx_highest >> 8);
		modes[n].mcs_set[12] = band->ht_cap.mcs.tx_params;
		modes[n].a_mpdu_params =
		    (unsigned char)((band->ht_cap.ampdu_factor & 3u) |
				    ((band->ht_cap.ampdu_density & 7u) << 2));
	}
	n++;

	/* 802.11b: the four DSSS rates on the same channel set */
	if (n < max_modes) {
		static const int b_rates[4] = { 10, 20, 55, 110 };

		memset(&modes[n], 0, sizeof(modes[n]));
		modes[n].mode = LKPI_HOSTAP_MODE_11B;
		modes[n].num_channels = modes[0].num_channels;
		memcpy(modes[n].channels, modes[0].channels,
		       sizeof(modes[n].channels));
		modes[n].num_rates = 4;
		memcpy(modes[n].rates, b_rates, sizeof(b_rates));
		n++;
	}

	return n;
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
				     params->tail, (unsigned int)params->tail_len);
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
