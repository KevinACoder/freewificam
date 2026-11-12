/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    hostap_ap.c
 * @brief   The AP entry on the imported hostapd stack (NXP wpa_supplicant-rtos).
 *
 * The NXP port starts the AP in two steps and this file performs the same two:
 *
 *   1. start_wpa_supplicant() creates the supplicant/hostapd task
 *      (freertos/src/supp_main.c).  With CONFIG_HOSTAPD the task runs
 *      hostapd_main_task(): hostapd_global_init(), hostapd_interface_init()
 *      with the programmatic config (hostapd_config_read2 builds it in C, no
 *      conf file), hostapd_driver_init() - which calls our driver's hapd_init -
 *      and then posts hostapdReadySemaphoreHandle.
 *   2. the config the case wants (SSID, channel, beacon interval, DTIM, open
 *      auth, HT20, WMM) is applied to the daemon's interface, and
 *      hostapd_enable_iface() dispatches to the port's enable callback
 *      (supp_main.c), which clears start_disabled and runs
 *      hostapd_setup_interface() - the AP MLME starts beaconing.
 *
 * NXP's supp_api.c would do step 2 from a struct wlan_network; its
 * hostapd_update_bss() is static and the type comes from the NXP SDK's wlan.h,
 * which is not part of the imported tree.  The open-AP subset of that mapping
 * (SSID/channel/HT/WMM/auth) is what this file applies - config values, not
 * MLME logic.
 */
#include "includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "ap/hostapd.h"
#include "ap/ap_config.h"
#include "ap/sta_info.h"
#include "ap/wpa_auth.h"

#include "supp_main.h"
#include "fsl_os_abstraction.h"

#include "linux80211/hostap_ap.h"
#include "linux80211/hostap_drv.h"
#include "lkpinetif_seam.h"

/* supp_api.c owns these in the NXP build; this image does not compile that
 * file (it needs the NXP SDK's wlan.h), so the platform side defines them and
 * creates the objects before starting the supplicant task. */
OSA_MUTEX_HANDLE_DEFINE(wpa_supplicant_mutex);
OSA_SEMAPHORE_HANDLE_DEFINE(wpaSuppReadySemaphoreHandle);
OSA_SEMAPHORE_HANDLE_DEFINE(hostapdReadySemaphoreHandle);

/* freertos/src/supp_main.c */
extern struct hostapd_data *hostapd_get_hapd(void);

static int s_os_ready;
static int s_started;

/* WPA2 round counters, read by the case as anchors */
static unsigned s_ptk;
static unsigned s_gtk;
static unsigned s_key_fail;
static unsigned s_eapol_tx;
static unsigned s_eapol_rx;
static unsigned s_eapol_drop;

static void lkpi_hostap_eapol_sink(const unsigned char src[6],
				   const unsigned char *pdu, size_t len,
				   void *arg);

static int
lkpi_hostap_os_init(void)
{
	if (s_os_ready)
		return 0;
	if (OSA_MutexCreate((osa_mutex_handle_t)wpa_supplicant_mutex) !=
	    KOSA_StatusSuccess)
		return -ENOMEM;
	if (OSA_SemaphoreCreateBinary(
		    (osa_semaphore_handle_t)wpaSuppReadySemaphoreHandle) !=
	    KOSA_StatusSuccess)
		return -ENOMEM;
	if (OSA_SemaphoreCreateBinary(
		    (osa_semaphore_handle_t)hostapdReadySemaphoreHandle) !=
	    KOSA_StatusSuccess)
		return -ENOMEM;
	s_os_ready = 1;
	return 0;
}

/* the open-AP configuration subset of NXP's hostapd_update_bss() */
static int
lkpi_hostap_apply_config(struct hostapd_iface *iface,
			 const struct lkpi_hostap_cfg *cfg)
{
	struct hostapd_config *conf = iface->conf;
	struct hostapd_data *hapd = iface->bss[0];
	struct hostapd_bss_config *bss;
	const unsigned char *mac;
	size_t len;

	if (conf == NULL || hapd == NULL || hapd->conf == NULL)
		return -EINVAL;
	bss = hapd->conf;

	len = os_strlen(cfg->ssid);
	if (len < 1u || len > SSID_MAX_LEN)
		return -EINVAL;
	os_memcpy(bss->ssid.ssid, cfg->ssid, len);
	bss->ssid.ssid_len = len;
	bss->ssid.ssid_set = 1;
	bss->ssid.short_ssid = 0;

	conf->hw_mode = HOSTAPD_MODE_IEEE80211G;
	conf->channel = (int)cfg->channel;
	conf->beacon_int = (int)(cfg->beacon_int != 0u ? cfg->beacon_int : 100u);
	bss->dtim_period = (int)(cfg->dtim != 0u ? cfg->dtim : 2u);
	bss->max_num_sta = 8;

	/* Open unless the case supplied the WPA2 passphrase. */
	bss->auth_algs = WPA_AUTH_ALG_OPEN;
	if (cfg->passphrase != NULL) {
		size_t plen = os_strlen(cfg->passphrase);

		if (plen < 8u || plen > 63u)
			return -EINVAL;
		os_free(bss->ssid.wpa_passphrase);
		bss->ssid.wpa_passphrase = os_strdup(cfg->passphrase);
		if (bss->ssid.wpa_passphrase == NULL)
			return -ENOMEM;
		bss->ssid.wpa_passphrase_set = 1;
		bss->ssid.wpa_psk_set = 0;
		hostapd_config_clear_wpa_psk(&bss->ssid.wpa_psk);
		bss->wpa = WPA_PROTO_RSN;
		bss->wpa_key_mgmt = WPA_KEY_MGMT_PSK;
		bss->wpa_pairwise = WPA_CIPHER_CCMP;
		bss->rsn_pairwise = WPA_CIPHER_CCMP;
		bss->wpa_group = WPA_CIPHER_CCMP;
		bss->ieee802_1x = 0;
		/* derive wpa_group/security_policy from the fields above; the
		 * config was built as open earlier and that derivation must be
		 * redone, not inferred at check time. */
		hostapd_set_security_params(bss, 1);
	} else {
		bss->wpa = 0;
		bss->ieee802_1x = 0;
		bss->wpa_key_mgmt = 0;
		bss->rsn_pairwise = 0;
		bss->wpa_pairwise = 0;
	}
	conf->ieee80211n = 1;
	conf->ht_capab = HT_CAP_INFO_SHORT_GI20MHZ;
	conf->secondary_channel = 0;
	bss->wmm_enabled = 1;
	bss->wmm_uapsd = 0;

	/* own_addr: hapd_init() already wrote it from the interface; keep the
	 * fill for the case where hostapd was started before the driver bound */
	mac = lkpi_hostap_drv_mac();
	if (mac != NULL && is_zero_ether_addr(hapd->own_addr))
		os_memcpy(hapd->own_addr, mac, ETH_ALEN);
	os_memcpy(bss->bssid, hapd->own_addr, ETH_ALEN);
	return 0;
}

int
lkpi_hostap_start(const struct lkpi_hostap_cfg *cfg)
{
	struct hostapd_iface *iface;
	int rc;

	if (cfg == NULL || cfg->ifname == NULL || cfg->ssid == NULL)
		return -EINVAL;
	if (s_started)
		return -EBUSY;
	rc = lkpi_hostap_os_init();
	if (rc != 0) {
		wpa_printf(MSG_ERROR, "lkpi: OSA init failed: %d", rc);
		return rc;
	}
	/* the AP's EAPOL receive: the lwIP adapter classifies ETH_P_PAE and
	 * calls this sink; it only deep-copies and posts to the eloop task */
	lkpinetif_set_eapol_sink(lkpi_hostap_eapol_sink, NULL);
	if (start_wpa_supplicant((char *)cfg->ifname) != 0) {
		printf("lkpi: start_wpa_supplicant failed\n");
		return -EIO;
	}
	/*
	 * hostapd_main_task() posts hostapdReadySemaphoreHandle when the daemon
	 * is ready; a bounded poll of the interface registry is used here
	 * instead of taking that semaphore, so the wait yields the CPU to the
	 * daemon task and the port does not depend on a token that the NXP
	 * entry also consumes elsewhere (it re-posts it only for its own
	 * get_hostapd_handle()).
	 */
	iface = NULL;
	for (rc = 0; rc < 150; rc++) {
		iface = hostapd_get_interface(cfg->ifname);
		if (iface != NULL)
			break;
		OSA_TimeDelay(100u);
	}
	if (iface == NULL)
		return -ETIMEDOUT;
	if (iface == NULL || iface->conf == NULL || iface->bss[0] == NULL) {
		wpa_printf(MSG_ERROR, "lkpi: no hostapd interface for %s",
			   cfg->ifname);
		return -ENODEV;
	}
	rc = lkpi_hostap_apply_config(iface, cfg);
	if (rc != 0)
		return rc;
	rc = hostapd_enable_iface(iface);
	if (rc != 0) {
		wpa_printf(MSG_ERROR, "lkpi: hostapd_enable_iface failed");
		return -EIO;
	}
	/*
	 * The interface can come up asynchronously: setup defers some steps
	 * (the channel-list update among them) to the eloop, which runs in the
	 * NXP entry's task.  The AP is usable when hostapd reports ENABLED, so
	 * wait for that state instead of reporting success early.
	 */
	for (rc = 0; rc < 200; rc++) {
		struct hostapd_iface *cur = hostapd_get_interface(NULL);

		if (cur != NULL && cur->state == HAPD_IFACE_ENABLED)
			break;
		OSA_TimeDelay(100u);
	}
	if (rc == 200)
		return -ETIMEDOUT;
	s_started = 1;
	return 0;
}

void
lkpi_hostap_stop(void)
{
	struct hostapd_iface *iface;

	if (!s_started)
		return;
	iface = hostapd_get_interface(NULL);
	if (iface != NULL)
		(void)hostapd_disable_iface(iface);
	stop_wpa_supplicant();
	lkpinetif_set_eapol_sink(NULL, NULL);
	s_started = 0;
}

int
lkpi_hostap_ready(void)
{
	struct hostapd_iface *iface = hostapd_get_interface(NULL);

	if (iface == NULL)
		return 0;
	return iface->state == HAPD_IFACE_ENABLED ? 1 : 0;
}

/*
 * The management TX status.  hostapd's own MLME completes a (re)association
 * when the driver reports the fate of the response frame it sent
 * (EVENT_TX_STATUS -> ieee802_11_mgmt_cb -> handle_assoc_cb): that is where the
 * station is marked associated and authorized, and where the driver is told to
 * open the data path for it.  The NXP SDK posts this from its own TX confirm;
 * this port's frame hand-off returns once mac80211 accepted the frame, so the
 * status is posted from that point.  The ACK bit is the hand-off's, not the
 * air's - see the ADR's seam note.
 */
int
lkpi_hostap_mgmt_tx_status(const void *frame, unsigned int len, int ack)
{
	const unsigned char *fc = frame;
	struct hostapd_data *hapd;
	struct wpa_supplicant_event_msg *msg;
	union wpa_event_data *ev;

	if (frame == NULL || len < 24u)
		return -EINVAL;
	hapd = hostapd_get_hapd();
	if (hapd == NULL)
		return -ENODEV;
	msg = os_zalloc(sizeof(*msg));
	ev = os_zalloc(sizeof(*ev));
	if (msg == NULL || ev == NULL) {
		os_free(msg);
		os_free(ev);
		return -ENOMEM;
	}
	msg->hostapd = 1;
	msg->event = EVENT_TX_STATUS;
	msg->ctx = hapd;
	msg->data = ev;
	ev->tx_status.type = (u16)((fc[0] >> 2) & 0x3u);
	ev->tx_status.stype = (u16)((fc[0] >> 4) & 0xfu);
	ev->tx_status.data = (const u8 *)frame;
	ev->tx_status.data_len = len;
	ev->tx_status.ack = ack ? 1 : 0;
	/* copies the frame into msg->data; the eloop task frees both */
	if (wpa_supplicant_event_wrapper_deep_copy(msg, EVENT_TX_STATUS, ev) < 0) {
		os_free(ev);
		os_free(msg);
		return -ENOMEM;
	}
	if (send_wpa_supplicant_event(msg) < 0)
		return -EIO;
	return 0;
}

int
lkpi_hostap_sta_count(void)
{
	struct hostapd_data *hapd = hostapd_get_hapd();

	return hapd != NULL ? hapd->num_sta : 0;
}

int
lkpi_hostap_mgmt_rx(const void *frame, unsigned int len, int freq)
{
	struct hostapd_data *hapd;
	struct wpa_supplicant_event_msg *msg;
	union wpa_event_data *ev;

	if (frame == NULL || len < 24u)
		return -EINVAL;
	hapd = hostapd_get_hapd();
	if (hapd == NULL)
		return -ENODEV;
	msg = os_zalloc(sizeof(*msg));
	ev = os_zalloc(sizeof(*ev));
	if (msg == NULL || ev == NULL) {
		os_free(msg);
		os_free(ev);
		return -ENOMEM;
	}
	msg->hostapd = 1;
	msg->event = EVENT_RX_MGMT;
	msg->ctx = hapd;
	msg->data = ev;
	ev->rx_mgmt.frame = (const u8 *)frame;
	ev->rx_mgmt.frame_len = len;
	ev->rx_mgmt.freq = freq;
	ev->rx_mgmt.ssi_signal = 0;
	/* copies the frame into msg->data; the eloop task frees both */
	if (wpa_supplicant_event_wrapper_deep_copy(msg, EVENT_RX_MGMT, ev) < 0) {
		os_free(ev);
		os_free(msg);
		return -ENOMEM;
	}
	if (send_wpa_supplicant_event(msg) < 0)
		return -EIO;
	return 0;
}


/* --- the WPA2 round's counters and EAPOL receive (2026-11-12) ------------ */

void
lkpi_hostap_key_counts(unsigned *ptk, unsigned *gtk, unsigned *fail)
{
	if (ptk != NULL)
		*ptk = s_ptk;
	if (gtk != NULL)
		*gtk = s_gtk;
	if (fail != NULL)
		*fail = s_key_fail;
}

void
lkpi_hostap_eapol_counts(unsigned *tx, unsigned *rx, unsigned *drop)
{
	if (tx != NULL)
		*tx = s_eapol_tx;
	if (rx != NULL)
		*rx = s_eapol_rx;
	if (drop != NULL)
		*drop = s_eapol_drop;
}

void
lkpi_hostap_key_note(int pairwise, int default_key, int rc)
{
	if (rc != 0) {
		s_key_fail++;
		return;
	}
	if (pairwise)
		s_ptk++;
	else if (default_key)
		s_gtk++;
}

void
lkpi_hostap_eapol_tx_note(int rc)
{
	s_eapol_tx++;
	if (rc != 0)
		s_eapol_drop++;
}

int
lkpi_hostap_wpa2_sta_count(void)
{
	struct hostapd_data *hapd = hostapd_get_hapd();
	struct sta_info *sta;
	int n = 0;

	if (hapd == NULL)
		return 0;
	for (sta = hapd->sta_list; sta != NULL; sta = sta->next) {
		if (sta->wpa_sm != NULL &&
		    wpa_auth_pairwise_set(sta->wpa_sm))
			n++;
	}
	return n;
}

/*
 * One EAPOL frame from the lwIP receive path: the adapter has already
 * stripped the Ethernet header and classified ETH_P_PAE.  The hostapd event
 * carries the 802.1X PDU, not the header.  Deep copy runs here (RX context),
 * dispatch runs in the eloop task.
 */
int
lkpi_hostap_eapol_rx(const void *src, const void *pdu, unsigned int len)
{
	struct hostapd_data *hapd;
	struct wpa_supplicant_event_msg *msg;
	union wpa_event_data *ev;

	if (src == NULL || pdu == NULL || len < 4u) {
		s_eapol_drop++;
		return -EINVAL;
	}
	hapd = hostapd_get_hapd();
	if (hapd == NULL) {
		s_eapol_drop++;
		return -ENODEV;
	}
	msg = os_zalloc(sizeof(*msg));
	ev = os_zalloc(sizeof(*ev));
	if (msg == NULL || ev == NULL) {
		os_free(msg);
		os_free(ev);
		s_eapol_drop++;
		return -ENOMEM;
	}
	msg->hostapd = 1;
	msg->event = EVENT_EAPOL_RX;
	msg->ctx = hapd;
	msg->data = ev;
	ev->eapol_rx.src = (const u8 *)src;
	ev->eapol_rx.data = (const u8 *)pdu;
	ev->eapol_rx.data_len = len;
	if (wpa_supplicant_event_wrapper_deep_copy(msg, EVENT_EAPOL_RX, ev) < 0) {
		os_free(ev);
		os_free(msg);
		s_eapol_drop++;
		return -ENOMEM;
	}
	if (send_wpa_supplicant_event(msg) < 0) {
		s_eapol_drop++;
		return -EIO;
	}
	s_eapol_rx++;
	return 0;
}

static void
lkpi_hostap_eapol_sink(const unsigned char src[6],
		       const unsigned char *pdu, size_t len, void *arg)
{
	(void)arg;
	(void)lkpi_hostap_eapol_rx(src, pdu, (unsigned int)len);
}
