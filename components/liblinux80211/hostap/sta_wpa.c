/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    sta_wpa.c
 * @brief   The G2 STA entry: start the imported supplicant task, add one
 *          WPA2-PSK network and expose the state/counters the board case uses.
 *
 * The NXP entry (freertos/src/supp_main.c) already owns the FreeRTOS task,
 * the eloop and the wpa_supplicant_add_iface() call.  With CONFIG_HOSTAPD 0 it
 * takes its STA branch and hands the interface to us through `global`.  This
 * file is the first-party bridge that adds the network after that interface
 * exists; it does not touch the imported source.
 */
#include "includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "wpa_supplicant/config.h"
#include "wpa_supplicant/wpa_supplicant_i.h"
#include "wpa_supplicant/bss.h"
#include "crypto/sha1.h"

#include "fsl_os_abstraction.h"
#include "supp_main.h"

#include "linux80211/sta_wpa.h"

/* supp_main.c owns the task; these are the objects it waits on. */
OSA_MUTEX_HANDLE_DEFINE(wpa_supplicant_mutex);
OSA_SEMAPHORE_HANDLE_DEFINE(wpaSuppReadySemaphoreHandle);

extern struct wpa_global *global;

static struct wpa_supplicant *s_wpa;
static int s_started;
static char s_ssid[33];
static char s_psk[64];
static unsigned char s_bssid[6];
static int s_bssid_set;

static unsigned s_eapol_tx;
static unsigned s_eapol_rx;
static unsigned s_eapol_drop;
static unsigned s_ptk;
static unsigned s_gtk;
static unsigned s_key_fail;

static int
lkpi_sta_os_init(void)
{
	if (OSA_MutexCreate((osa_mutex_handle_t)wpa_supplicant_mutex) !=
	    KOSA_StatusSuccess)
		return -1;
	if (OSA_SemaphoreCreateBinary(
		    (osa_semaphore_handle_t)wpaSuppReadySemaphoreHandle) !=
	    KOSA_StatusSuccess)
		return -1;
	return 0;
}

int
lkpi_sta_start(const struct lkpi_sta_cfg *cfg)
{
	struct wpa_ssid *ssid;
	size_t len;
	unsigned int i;

	if (cfg == NULL || cfg->ssid == NULL)
		return -1;
	len = os_strlen(cfg->ssid);
	if (len == 0u || len > 32u)
		return -1;
	/* A NULL passphrase is an open network, not a missing argument: the STA
	 * 5 GHz round needs association without crypto as its own gate (a
	 * handshake failure must not be able to hide a band failure). */
	if (cfg->psk != NULL &&
	    (os_strlen(cfg->psk) < 8u || os_strlen(cfg->psk) > 63u))
		return -1;
	if (s_started)
		return -EBUSY;

	os_memcpy(s_ssid, cfg->ssid, len);
	s_ssid[len] = '\0';
	if (cfg->psk != NULL)
		os_strlcpy(s_psk, cfg->psk, sizeof(s_psk));
	else
		s_psk[0] = '\0';
	if (cfg->bssid != NULL) {
		os_memcpy(s_bssid, cfg->bssid, 6);
		s_bssid_set = 1;
	} else {
		os_memset(s_bssid, 0, sizeof(s_bssid));
		s_bssid_set = 0;
	}

	if (lkpi_sta_os_init() != 0) {
		printf("lkpi: sta OSA init failed\n");
		return -1;
	}
	if (start_wpa_supplicant((char *)"wl0") != 0) {
		printf("lkpi: start_wpa_supplicant failed\n");
		return -1;
	}

	/* supp_main.c posts the ready semaphore after add_iface but keeps the
	 * interface in `global`; polling the registry yields the CPU and does
	 * not consume the token stop_wpa_supplicant() waits for. */
	for (i = 0; i < 100u; i++) {
		/* The lwIP netif's name is assigned by netif_add() and may be
		 * wl0 or wl1 depending on the netif_num state; the registry is the
		 * authoritative list, not a guessed name. */
		s_wpa = (global != NULL) ? global->ifaces : NULL;
		if (s_wpa != NULL)
			break;
		OSA_TimeDelay(100);
	}
	if (s_wpa == NULL) {
		printf("lkpi: sta no supplicant interface\n");
		return -1;
	}
	printf("lkpi: sta iface=%s\n", s_wpa->ifname);
	s_wpa->conf->ap_scan = 1;
	s_wpa->conf->filter_ssids = 1;

	ssid = wpa_config_add_network(s_wpa->conf);
	if (ssid == NULL) {
		printf("lkpi: sta add_network failed\n");
		return -1;
	}
	ssid->ssid = os_memdup(s_ssid, len);
	if (ssid->ssid == NULL)
		return -1;
	ssid->ssid_len = len;
	if (cfg->psk == NULL) {
		/* Open: the supplicant associates and enters WPA_COMPLETED from
		 * its own assoc event (events.c), so no PSK state is set here. */
		ssid->key_mgmt = WPA_KEY_MGMT_NONE;
		ssid->proto = 0;
		ssid->pairwise_cipher = WPA_CIPHER_NONE;
		ssid->group_cipher = WPA_CIPHER_NONE;
	} else {
		/* This tree's wpas_network_disabled() treats a passphrase with a
		 * non-zero ssid_len as disabled; derive the PMK here and hand the
		 * supplicant a ready psk_set network. */
		if (pbkdf2_sha1(s_psk, (const u8 *)s_ssid, len, 4096, ssid->psk,
				PMK_LEN) != 0) {
			printf("lkpi: sta pbkdf2 failed\n");
			return -1;
		}
		ssid->psk_set = 1;
		ssid->key_mgmt = WPA_KEY_MGMT_PSK;
		ssid->proto = WPA_PROTO_RSN;
		ssid->pairwise_cipher = WPA_CIPHER_CCMP;
		ssid->group_cipher = WPA_CIPHER_CCMP;
	}
	ssid->scan_ssid = 1;
	if (s_bssid_set) {
		os_memcpy(ssid->bssid, s_bssid, 6);
		ssid->bssid_set = 1;
	}

	printf("lkpi: sta network ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
	       s_ssid, s_bssid[0], s_bssid[1], s_bssid[2], s_bssid[3],
	       s_bssid[4], s_bssid[5]);
	s_started = 1;
	wpa_supplicant_select_network(s_wpa, ssid);
	return 0;
}

int
lkpi_sta_state(void)
{
	if (s_wpa == NULL)
		return -1;
	return (int)s_wpa->wpa_state;
}

int
lkpi_sta_ready(void)
{
	return (s_wpa != NULL && s_wpa->wpa_state == WPA_COMPLETED) ? 1 : 0;
}

void
lkpi_sta_counts(unsigned *eapol_tx, unsigned *eapol_rx, unsigned *drop,
		unsigned *ptk, unsigned *gtk, unsigned *fail)
{
	if (eapol_tx != NULL)
		*eapol_tx = s_eapol_tx;
	if (eapol_rx != NULL)
		*eapol_rx = s_eapol_rx;
	if (drop != NULL)
		*drop = s_eapol_drop;
	if (ptk != NULL)
		*ptk = s_ptk;
	if (gtk != NULL)
		*gtk = s_gtk;
	if (fail != NULL)
		*fail = s_key_fail;
}

void
lkpi_sta_note_eapol_tx(int rc)
{
	s_eapol_tx++;
	if (rc != 0)
		s_eapol_drop++;
}

void
lkpi_sta_note_eapol_rx(void)
{
	s_eapol_rx++;
}

void
lkpi_sta_note_key(int pairwise, int default_key, int rc)
{
	(void)default_key;
	if (rc != 0) {
		s_key_fail++;
		return;
	}
	if (pairwise)
		s_ptk++;
	else
		s_gtk++;
}

void
lkpi_sta_stop(void)
{
	if (!s_started)
		return;
	(void)stop_wpa_supplicant();
	s_started = 0;
	s_wpa = NULL;
}
