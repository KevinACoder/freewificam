/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    driver_lkpi.c
 * @brief   hostapd's wpa_driver_ops table for this image, on top of the
 *          hostap_drv.h seam (the nl80211 calls live in hostap_nl80211.c).
 *
 * This is the file that replaces the NXP port's driver_freertos.c: the same
 * table (the op set the NXP driver wraps) with the NXP wifi middleware calls
 * swapped for this port's direct cfg80211/nl80211 surface.  The hostap world
 * stays on its own side of the seam - no Linux type appears here.
 *
 * The open AP round needs only the MLME ops.  The WPA2 round adds the two
 * key-management ops hostapd looks for - hapd_send_eapol (msg1/msg3) and
 * set_key (PTK/GTK, plus the DEL_KEY form) - on the same seam.  Both were
 * absent before because hostapd silently skips an op a driver does not
 * implement; the EAPOL path in particular failed with no error.
 */
#include "includes.h"

#include "utils/common.h"
#include "utils/dl_list.h"
#include "drivers/driver.h"
#include "l2_packet/l2_packet.h"

#include "linux80211/hostap_drv.h"
#include "linux80211/hostap_ap.h"

struct lkpi_drv {
	struct hostapd_data	*hapd;
	int			 opened;
};

/*
 * The STA-side init.  This image is the AP role, but the imported entry also
 * initialises a wpa_supplicant interface (freertos/src/supp_main.c) and that
 * interface needs a driver context before it can exist.  The STA role has no
 * state here yet (the AP round uses the hostapd side), so the context is a
 * marker: what matters is that the interface initialises and the entry's eloop
 * runs, which is the task that dispatches hostapd's own events.
 */
static int lkpi_sta_priv;

static void *
lkpi_sta_init2(void *ctx, const char *ifname, void *global_priv)
{
	(void)ctx;
	(void)ifname;
	(void)global_priv;
	return &lkpi_sta_priv;
}

static void
lkpi_sta_deinit(void *priv)
{
	(void)priv;
}

static void *
lkpi_hapd_init(struct hostapd_data *hapd, struct wpa_init_params *params)
{
	struct lkpi_drv *drv;
	const unsigned char *mac;

	if (hapd == NULL || params == NULL || params->ifname == NULL)
		return NULL;
	if (lkpi_hostap_drv_open(params->ifname) != 0) {
		wpa_printf(MSG_ERROR, "lkpi: no netdev for %s", params->ifname);
		return NULL;
	}
	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL) {
		lkpi_hostap_drv_close();
		return NULL;
	}
	drv->hapd = hapd;
	drv->opened = 1;
	/* hostapd's own_addr buffer: the interface's address, the same source
	 * GT's hostapd reads through nl80211 */
	mac = lkpi_hostap_drv_mac();
	if (mac != NULL && params->own_addr != NULL)
		os_memcpy(params->own_addr, mac, ETH_ALEN);
	return drv;
}

static void
lkpi_hapd_deinit(void *priv)
{
	struct lkpi_drv *drv = priv;

	if (drv != NULL && drv->opened)
		lkpi_hostap_drv_close();
	os_free(drv);
}

static int
lkpi_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	(void)priv;
	if (capa == NULL)
		return -1;
	os_memset(capa, 0, sizeof(*capa));
	return 0;
}

static int
lkpi_get_ext_capab(void *priv, enum wpa_driver_if_type type,
		   const u8 **ext_capab, const u8 **ext_capab_mask,
		   unsigned int *ext_capab_len)
{
	(void)priv;
	(void)type;
	if (ext_capab != NULL)
		*ext_capab = NULL;
	if (ext_capab_mask != NULL)
		*ext_capab_mask = NULL;
	if (ext_capab_len != NULL)
		*ext_capab_len = 0;
	return -1;
}

static struct hostapd_hw_modes *
lkpi_get_hw_feature_data(void *priv, u16 *num_modes, u16 *flags, u8 *dfs)
{
	struct lkpi_hostap_mode modes[2];
	struct hostapd_hw_modes *out;
	int n;
	int i;
	int j;

	(void)priv;
	if (num_modes == NULL || flags == NULL || dfs == NULL)
		return NULL;
	n = lkpi_hostap_drv_modes(modes, 2);
	if (n <= 0) {
		wpa_printf(MSG_ERROR, "lkpi: no 2.4 GHz band on the interface");
		return NULL;
	}
	out = os_zalloc((size_t)n * sizeof(*out));
	if (out == NULL)
		return NULL;
	*flags = 0;
	*dfs = 0;

	for (i = 0; i < n; i++) {
		out[i].mode = (enum hostapd_hw_mode)modes[i].mode;
		out[i].num_channels = modes[i].num_channels;
		out[i].channels = os_zalloc((size_t)modes[i].num_channels *
					    sizeof(struct hostapd_channel_data));
		out[i].num_rates = modes[i].num_rates;
		out[i].rates = os_zalloc((size_t)modes[i].num_rates *
					 sizeof(int));
		if (out[i].channels == NULL || out[i].rates == NULL)
			return NULL;
		for (j = 0; j < modes[i].num_channels; j++) {
			struct hostapd_channel_data *ch = &out[i].channels[j];

			ch->chan = (short)modes[i].channels[j].chan;
			ch->freq = modes[i].channels[j].freq;
			ch->flag = modes[i].channels[j].flags;
			ch->allowed_bw = modes[i].channels[j].allowed_bw;
			ch->max_tx_power = 20;
			dl_list_init(&ch->survey_list);
		}
		for (j = 0; j < modes[i].num_rates; j++)
			out[i].rates[j] = modes[i].rates[j];
		if (modes[i].ht_supported) {
			out[i].ht_capab = (u16)modes[i].ht_capab;
			os_memcpy(out[i].mcs_set, modes[i].mcs_set, 16);
			out[i].a_mpdu_params = modes[i].a_mpdu_params;
			out[i].flags |= HOSTAPD_MODE_FLAG_HT_INFO_KNOWN;
		}
	}
	*num_modes = (u16)n;
	return out;
}

static int
lkpi_set_ap(void *priv, struct wpa_driver_ap_params *params)
{
	struct lkpi_hostap_ap_params p;

	(void)priv;
	if (params == NULL || params->head == NULL || params->head_len == 0)
		return -1;
	os_memset(&p, 0, sizeof(p));
	p.head = params->head;
	p.head_len = params->head_len;
	p.tail = params->tail;
	p.tail_len = params->tail_len;
	p.ssid = params->ssid;
	p.ssid_len = params->ssid_len;
	if (params->freq != NULL) {
		p.freq = params->freq->freq;
		p.bw_mhz = params->freq->bandwidth;
	}
	p.beacon_int = params->beacon_int;
	p.dtim = params->dtim_period;
	{
		int rc = lkpi_hostap_drv_set_ap(&p);

		printf("lkpi: set_ap head=%u tail=%u freq=%d rc=%d\n",
		       (unsigned)p.head_len, (unsigned)p.tail_len, p.freq, rc);
		if (rc != 0) {
			wpa_printf(MSG_ERROR, "lkpi: START_AP failed");
			return -1;
		}
	}
	return 0;
}

static int
lkpi_send_mlme(void *priv, const u8 *data, size_t data_len, int noack,
	       unsigned int freq, const u16 *csa_offs, size_t csa_offs_len,
	       int no_encrypt, unsigned int wait)
{
	(void)priv;
	(void)freq;
	(void)csa_offs;
	(void)csa_offs_len;
	(void)no_encrypt;
	(void)wait;
	if (data == NULL || data_len < 24)
		return -1;
	lkpi_tx_health_mgmt(data, (unsigned int)data_len);
	{
		int rc = lkpi_hostap_drv_send_mlme(data, data_len, noack);

		if (rc != 0) {
			wpa_printf(MSG_DEBUG, "lkpi: mgmt tx len=%u rc=%d",
				   (unsigned)data_len, rc);
			return -1;
		}
	}
	/*
	 * A frame sent with noack=0 is one hostapd wants the fate of (the
	 * (re)association responses are the ones that matter): report it, so
	 * hostapd's handle_assoc_cb runs and the station is marked associated
	 * and authorized - which is what opens the data path in mac80211.
	 */
	if (!noack && lkpi_hostap_mgmt_tx_status(data, (unsigned int)data_len,
						 1) != 0)
		wpa_printf(MSG_DEBUG, "lkpi: tx status post failed");
	return 0;
}

/*
 * The 4-way handshake's EAPOL frames.  The AP path does not use l2_packet:
 * hostapd hands the 802.1X PDU down, this builds the Ethernet header and
 * sends it through the normal egress, exactly as the Linux nl80211 driver's
 * nl80211_send_eapol_data() would on wlan0.
 */
static int
lkpi_send_eapol(void *priv, const u8 *addr, const u8 *data, size_t data_len,
		int encrypt, const u8 *own_addr, u32 flags)
{
	struct l2_ethhdr *eth;
	unsigned char *buf;
	size_t len;
	int rc;

	(void)priv;
	(void)encrypt;
	(void)flags;
	if (addr == NULL || data == NULL || own_addr == NULL ||
	    data_len > 2000u)
		return -1;
	len = sizeof(*eth) + data_len;
	buf = os_malloc(len);
	if (buf == NULL)
		return -1;
	eth = (struct l2_ethhdr *)buf;
	os_memcpy(eth->h_dest, addr, ETH_ALEN);
	os_memcpy(eth->h_source, own_addr, ETH_ALEN);
	eth->h_proto = host_to_be16(ETH_P_EAPOL);
	os_memcpy(eth + 1, data, data_len);
	rc = lkpi_hostap_drv_eapol_tx(buf, len);
	os_free(buf);
	lkpi_hostap_eapol_tx_note(rc);
	if (rc != 0)
		printf("lkpi: eapol tx len=%u rc=%d\n", (unsigned)len, rc);
	return rc != 0 ? -1 : 0;
}

static int
lkpi_set_key(void *priv, struct wpa_driver_set_key_params *params)
{
	struct lkpi_hostap_key key;
	int rc;

	(void)priv;
	if (params == NULL)
		return -1;
	if (params->alg == WPA_ALG_NONE) {
		rc = lkpi_hostap_drv_del_key(params->addr, params->key_idx);
		/* Deleting a key that was never installed is a normal part of
		 * the handshake's reset order (wpa_auth clears keyidx 0 before
		 * the PTK exists).  -ENOENT is not an install failure. */
		if (rc != 0 && rc != -ENOENT)
			lkpi_hostap_key_note(0, 0, rc);
		printf("lkpi: del_key keyidx=%d rc=%d\n", params->key_idx, rc);
		return rc != 0 && rc != -ENOENT ? -1 : 0;
	}
	if ((params->key_flag & KEY_FLAG_PMK) != 0u)
		return 0;	/* no PMK offload: nothing to install */
	if ((params->key_flag & KEY_FLAG_MODIFY) != 0u) {
		printf("lkpi: set_key modify unsupported\n");
		return -1;
	}
	if (params->alg != WPA_ALG_CCMP || params->key == NULL ||
	    params->key_len != 16u) {
		printf("lkpi: set_key unsupported alg=%d key_len=%u\n",
		       (int)params->alg, (unsigned)params->key_len);
		return -1;
	}
	os_memset(&key, 0, sizeof(key));
	key.addr = params->addr;
	key.key_idx = params->key_idx;
	key.cipher = LKPI_HOSTAP_CIPHER_CCMP;
	key.key = params->key;
	key.key_len = params->key_len;
	key.seq = params->seq;
	key.seq_len = params->seq_len;
	key.pairwise = (params->key_flag & KEY_FLAG_PAIRWISE) != 0u;
	key.default_key = (params->key_flag & KEY_FLAG_DEFAULT) != 0u;
	rc = lkpi_hostap_drv_set_key(&key);
	lkpi_hostap_key_note(key.pairwise, key.default_key, rc);
	printf("lkpi: set_key keyidx=%d cipher=0x%x pairwise=%d default=%d "
	       "rc=%d mac=" MACSTR "\n",
	       key.key_idx, key.cipher, key.pairwise, key.default_key, rc,
	       MAC2STR(params->addr));
	return rc != 0 ? -1 : 0;
}

static int
lkpi_sta_add(void *priv, struct hostapd_sta_add_params *params)
{
	struct lkpi_hostap_sta_add sta;

	(void)priv;
	if (params == NULL || params->addr == NULL)
		return -1;
	os_memset(&sta, 0, sizeof(sta));
	sta.addr = params->addr;
	sta.aid = params->aid;
	sta.capability = params->capability;
	sta.listen_interval = params->listen_interval;
	sta.rates = params->supp_rates;
	sta.rates_len = params->supp_rates_len;
	if (params->ht_capabilities != NULL) {
		sta.ht_capa = params->ht_capabilities;
		sta.ht_capa_len = sizeof(struct ieee80211_ht_capabilities);
	}
	sta.wmm = (params->flags & WPA_STA_WMM) != 0u;
	{
		int rc;
		const unsigned int *w = (const unsigned int *)params;

		printf("lkpi: sta add params=%p size=%u aid=%u cap=%u li=%u rl=%u fl=0x%x set=%d\n",
		       (void *)params, (unsigned)sizeof(*params),
		       (unsigned)params->aid, (unsigned)params->capability,
		       (unsigned)params->listen_interval,
		       (unsigned)params->supp_rates_len,
		       (unsigned)params->flags, params->set);
		printf("lkpi: words %08x %08x %08x %08x %08x %08x\n",
		       w[0], w[1], w[2], w[3], w[4], w[5]);
		rc = lkpi_hostap_drv_sta_add(&sta);
		printf("lkpi: sta add " MACSTR " rc=%d aid=%u set=%d fl=0x%x\n",
		       MAC2STR(params->addr), rc, (unsigned)params->aid,
		       params->set, (unsigned)params->flags);
		if (rc != 0)
			return -1;
	}
	return 0;
}

static int
lkpi_sta_remove(void *priv, const u8 *addr)
{
	(void)priv;
	if (addr == NULL)
		return -1;
	if (lkpi_hostap_drv_sta_del(addr) != 0)
		return -1;
	return 0;
}

static int
lkpi_sta_set_flags(void *priv, const u8 *addr, unsigned int total_flags,
		   unsigned int flags_or, unsigned int flags_and)
{
	unsigned int flags;

	(void)priv;
	if (addr == NULL)
		return -1;
	flags = total_flags;
	flags |= flags_or;
	flags &= flags_and;
	{
		int authorized = (flags & WPA_STA_AUTHORIZED) != 0u;
		int rc = lkpi_hostap_drv_sta_authorized(addr, authorized);

		printf("lkpi: sta authorized " MACSTR " %d rc=%d\n",
		       MAC2STR(addr), authorized, rc);
		if (rc != 0)
			return -1;
	}
	return 0;
}

static int
lkpi_set_freq(void *priv, struct hostapd_freq_params *freq)
{
	(void)priv;
	(void)freq;
	return 0;
}

static int
lkpi_set_rts(void *priv, int rts)
{
	(void)priv;
	(void)rts;
	return 0;
}

static int
lkpi_set_frag(void *priv, int frag)
{
	(void)priv;
	(void)frag;
	return 0;
}

static int
lkpi_set_country(void *priv, const char *country)
{
	(void)priv;
	(void)country;
	return 0;
}

const struct wpa_driver_ops wpa_driver_lkpi_ops = {
	.name = "lkpi",
	.desc = "mac80211 nl80211 (RTOS port)",
	.init2 = lkpi_sta_init2,
	.deinit = lkpi_sta_deinit,
	.hapd_init = lkpi_hapd_init,
	.hapd_deinit = lkpi_hapd_deinit,
	.get_capa = lkpi_get_capa,
	.get_ext_capab = lkpi_get_ext_capab,
	.get_hw_feature_data = lkpi_get_hw_feature_data,
	.set_ap = lkpi_set_ap,
	.send_mlme = lkpi_send_mlme,
	.hapd_send_eapol = lkpi_send_eapol,
	.set_key = lkpi_set_key,
	.sta_add = lkpi_sta_add,
	.sta_remove = lkpi_sta_remove,
	.sta_set_flags = lkpi_sta_set_flags,
	.set_freq = lkpi_set_freq,
	.set_rts = lkpi_set_rts,
	.set_frag = lkpi_set_frag,
	.set_country = lkpi_set_country,
};
