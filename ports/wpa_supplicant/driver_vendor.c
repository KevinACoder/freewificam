/*
 * @file    driver_vendor.c
 * @brief   wpa_supplicant driver wrapper skeleton for vendor-driver lines.
 *
 * Serves the fork's single "embox" driver slot (CONFIG_DRIVER_EMBOX; the
 * symbol keeps the vanilla drivers.c export name so the vendored tree stays
 * untouched). Every ops call forwards to the table the consumer line
 * registered through wpa_vendor_port_register(); the translation between the
 * hostap types and the vendor-neutral contract lives HERE, the consumer never
 * sees a hostap type.
 *
 * Fail-safe shape: with no backend registered, init2 still succeeds (the
 * supplicant thread and eloop stay alive) and every operation returns -1 with
 * a "wpa: no vendor backend" line.
 *
 * Event side: the skeleton owns the vendor event pump - init2 hands
 * skel_event() to the backend's set_event_cb, which forwards the
 * vendor-neutral events into the port's deep-copy queue (wpa_send_event)
 * under the hostap event codes. Scan completion is pull-model: the SCAN_DONE
 * event only flips the pending flag, the table is fetched through scan_fetch()
 * when the supplicant calls get_scan_results2; a 60 s timer reports an
 * aborted scan.
 *
 * @design single-slot vtable (DESIGN.md section 1, "driver slot" row): the
 *         fork's own wpa_drivers[] entry
 *         (third-party/wpa_supplicant/src/drivers/drivers.c:61-63) is served
 *         with zero vendored-tree edits; the ops set mirrors the upstream
 *         driver face (third-party/wpa_supplicant/src/drivers/driver.h).
 */

#include <includes.h>

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "drivers/driver.h"

#include "wpa_port_glue.h"
#include "wpa_vendor_port.h"

struct wpa_vendor_drv_data {
	void *ctx; /* wpa_s */
	const struct wpa_vendor_port *vp;
	int associated;
	int scan_pending;
	/* last associate() request, reported back through get_ssid */
	uint8_t ssid[33];
	size_t ssid_len;
	/* the BSSID the ASSOC event reported */
	uint8_t bssid[6];
	struct wpa_driver_capa capa;
};

/* single iface per image: the event callback has no priv
 * slot on the contract, so the state is static */
static struct wpa_vendor_drv_data g_drv;

#define SKEL_NO_BACKEND() \
	wpa_printf(MSG_ERROR, "wpa: no vendor backend")

/* ------------------------------------------------------------------ */
/* event pump (driver worker context: queue only, no supplicant state) */

static void skel_event(int event, const uint8_t *addr, const uint8_t *buf,
    size_t len, void *arg) {
	struct wpa_vendor_drv_data *drv = arg;
	union wpa_event_data data;

	if (drv->ctx == NULL) {
		return;
	}
	memset(&data, 0, sizeof(data));
	switch (event) {
	case WPA_VENDOR_EVENT_SCAN_DONE:
		if (__atomic_exchange_n(&drv->scan_pending, 0,
			__ATOMIC_ACQ_REL)) {
			wpa_printf(MSG_DEBUG, "wpa: scan complete");
			wpa_send_event(drv->ctx, EVENT_SCAN_RESULTS, &data);
		}
		break;
	case WPA_VENDOR_EVENT_ASSOC:
		drv->associated = 1;
		if (addr != NULL) {
			os_memcpy(drv->bssid, addr, ETH_ALEN);
		}
		wpa_printf(MSG_INFO, "wpa: association complete");
		data.assoc_info.addr = addr;
		wpa_send_event(drv->ctx, EVENT_ASSOC, &data);
		break;
	case WPA_VENDOR_EVENT_DISASSOC:
		drv->associated = 0;
		wpa_printf(MSG_INFO, "wpa: disassociation");
		wpa_send_event(drv->ctx, EVENT_DISASSOC, &data);
		break;
	case WPA_VENDOR_EVENT_EAPOL_RX:
		data.eapol_rx.src = addr;
		data.eapol_rx.data = buf;
		data.eapol_rx.data_len = len;
		wpa_send_event(drv->ctx, EVENT_EAPOL_RX, &data);
		break;
	default:
		break;
	}
}

/* scan abort: report an aborted scan if no completion came */
static void skel_scan_poll(void *eloop_ctx, void *timeout_ctx) {
	union wpa_event_data data;

	(void) eloop_ctx;
	(void) timeout_ctx;

	if (__atomic_exchange_n(&g_drv.scan_pending, 0, __ATOMIC_ACQ_REL)) {
		memset(&data, 0, sizeof(data));
		data.scan_info.aborted = 1;
		wpa_printf(MSG_ERROR, "wpa: scan timed out");
		wpa_send_event(g_drv.ctx, EVENT_SCAN_RESULTS, &data);
	}
}

/* ------------------------------------------------------------------ */
/* ops */

static void *skel_global_init(void *ctx) {
	(void) ctx;
	/* the single iface is the global state */
	return &g_drv;
}

static void skel_global_deinit(void *priv) {
	(void) priv;
}

static void *skel_init2(void *ctx, const char *ifname, void *global_priv) {
	const struct wpa_vendor_port *vp = wpa_vendor_backend();

	(void) ifname;
	(void) global_priv;

	memset(&g_drv, 0, sizeof(g_drv));
	g_drv.ctx = ctx;

	if (vp == NULL) {
		/* stay alive on purpose: the supplicant runs and every
		 * op below reports the missing backend */
		wpa_printf(MSG_ERROR, "wpa: no vendor backend registered");
		return &g_drv;
	}
	g_drv.vp = vp;

	g_drv.capa.key_mgmt = WPA_KEY_MGMT_PSK;
	g_drv.capa.enc = WPA_DRIVER_CAPA_ENC_CCMP;
	g_drv.capa.auth = WPA_DRIVER_AUTH_OPEN;
	g_drv.capa.max_scan_ssids = 1;

	vp->set_event_cb(skel_event, &g_drv);
	wpa_printf(MSG_INFO, "wpa: vendor backend bound");
	return &g_drv;
}

static void skel_deinit(void *priv) {
	struct wpa_vendor_drv_data *drv = priv;

	__atomic_store_n(&drv->scan_pending, 0, __ATOMIC_RELEASE);
	eloop_cancel_timeout(skel_scan_poll, NULL, NULL);
	if (drv->vp != NULL && drv->vp->set_event_cb != NULL) {
		drv->vp->set_event_cb(NULL, NULL);
	}
	memset(&g_drv, 0, sizeof(g_drv));
}

static int skel_get_bssid(void *priv, u8 *bssid) {
	struct wpa_vendor_drv_data *drv = priv;

	if (!drv->associated) {
		return -1;
	}
	os_memcpy(bssid, drv->bssid, ETH_ALEN);
	return 0;
}

static int skel_get_ssid(void *priv, u8 *ssid) {
	struct wpa_vendor_drv_data *drv = priv;

	if (!drv->associated) {
		return -1;
	}
	os_memcpy(ssid, drv->ssid, drv->ssid_len);
	return (int) drv->ssid_len;
}

static int skel_set_countermeasures(void *priv, int enabled) {
	(void) priv;
	(void) enabled;
	return 0;
}

static int skel_scan2(void *priv, struct wpa_driver_scan_params *params) {
	struct wpa_vendor_drv_data *drv = priv;
	const u8 *ssid = params->num_ssids ? params->ssids[0].ssid : NULL;
	size_t len = params->num_ssids ? params->ssids[0].ssid_len : 0;

	if (params->num_ssids > 1) {
		return -1;
	}
	if (drv->vp == NULL || drv->vp->scan_start == NULL) {
		SKEL_NO_BACKEND();
		return -1;
	}
	if (__atomic_exchange_n(&drv->scan_pending, 1, __ATOMIC_ACQ_REL)) {
		return -1;
	}
	eloop_cancel_timeout(skel_scan_poll, NULL, NULL);
	/* 60 s: a 13-channel sweep on this class of hardware can run for
	 * tens of seconds (per-channel tune + probe + listen), and the
	 * abort must not race the completion */
	if (eloop_register_timeout(60, 0, skel_scan_poll, NULL, NULL) < 0 ||
	    drv->vp->scan_start(ssid, len) != 0) {
		__atomic_store_n(&drv->scan_pending, 0, __ATOMIC_RELEASE);
		eloop_cancel_timeout(skel_scan_poll, NULL, NULL);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "wpa: scan requested ssid_len=%u",
	    (unsigned) len);
	return 0;
}

/*
 * One scan entry: SSID IE + the raw beacon IEs the vendor table kept
 * (RSN/WPA live there), laid out after the wpa_scan_res header.
 */
static struct wpa_scan_res *skel_scan_entry(struct wpa_scan_res_ret *in) {
	struct wpa_scan_res *r;
	size_t ssid_ie_len, ie_len, len;
	u8 *ie;

	ssid_ie_len = 2 + in->ssid_len;
	ie_len = ssid_ie_len + in->ies_len;

	len = sizeof(struct wpa_scan_res) + ie_len;
	r = os_zalloc(len);
	if (r == NULL) {
		return NULL;
	}
	os_memcpy(r->bssid, in->bssid, ETH_ALEN);
	r->freq = in->freq;
	r->beacon_int = in->beacon_int;
	r->caps = in->caps;
	r->level = in->level;
	r->ie_len = (size_t) ie_len;
	r->beacon_ie_len = 0;

	ie = (u8 *) (r + 1);
	*ie++ = WLAN_EID_SSID;
	*ie++ = (u8) in->ssid_len;
	os_memcpy(ie, in->ssid, in->ssid_len);
	ie += in->ssid_len;
	if (in->ies != NULL && in->ies_len != 0) {
		os_memcpy(ie, in->ies, in->ies_len);
	}
	return r;
}

#define SKEL_SCAN_MAX 64

static struct wpa_scan_results *skel_get_scan_results2(void *priv) {
	struct wpa_vendor_drv_data *drv = priv;
	struct wpa_scan_results *res;
	int idx;

	if (drv->vp == NULL || drv->vp->scan_fetch == NULL) {
		SKEL_NO_BACKEND();
		return NULL;
	}

	res = os_zalloc(sizeof(*res));
	if (res == NULL) {
		return NULL;
	}
	res->res = os_calloc(SKEL_SCAN_MAX, sizeof(struct wpa_scan_res *));
	if (res->res == NULL) {
		os_free(res);
		return NULL;
	}

	for (idx = 0; idx < SKEL_SCAN_MAX; idx++) {
		struct wpa_scan_res_ret in;
		struct wpa_scan_res *r;

		memset(&in, 0, sizeof(in));
		if (drv->vp->scan_fetch(idx, &in) != 0) {
			break;
		}
		r = skel_scan_entry(&in);
		if (r == NULL) {
			continue;
		}
		res->res[res->num++] = r;
	}
	return res;
}

static int skel_deauthenticate(void *priv, const u8 *addr, u16 reason_code) {
	struct wpa_vendor_drv_data *drv = priv;

	(void) addr;
	__atomic_store_n(&drv->scan_pending, 0, __ATOMIC_RELEASE);
	eloop_cancel_timeout(skel_scan_poll, NULL, NULL);
	drv->associated = 0;
	if (drv->vp == NULL || drv->vp->disconnect == NULL) {
		SKEL_NO_BACKEND();
		return -1;
	}
	return drv->vp->disconnect(reason_code);
}

static int skel_associate(void *priv,
    struct wpa_driver_associate_params *params) {
	struct wpa_vendor_drv_data *drv = priv;
	struct wpa_vendor_conn conn;
	int ret;

	if (drv->vp == NULL || drv->vp->connect == NULL) {
		SKEL_NO_BACKEND();
		return -1;
	}
	if (params->ssid == NULL || params->ssid_len == 0 ||
	    params->ssid_len > sizeof(drv->ssid)) {
		return -1;
	}

	memset(&conn, 0, sizeof(conn));
	conn.ssid = params->ssid;
	conn.ssid_len = params->ssid_len;
	conn.bssid = params->bssid;
	conn.bssid_set = params->bssid != NULL;
	conn.wpa_ie = params->wpa_ie;
	conn.wpa_ie_len = params->wpa_ie_len;

	ret = drv->vp->connect(&conn);
	if (ret == 0) {
		/* the state the post-connect queries report */
		os_memcpy(drv->ssid, params->ssid, params->ssid_len);
		drv->ssid_len = params->ssid_len;
		if (params->bssid != NULL) {
			os_memcpy(drv->bssid, params->bssid, ETH_ALEN);
		}
	}
	return ret;
}

static int skel_set_key(void *priv, struct wpa_driver_set_key_params *p) {
	struct wpa_vendor_drv_data *drv = priv;
	struct wpa_vendor_key k;
	int alg;

	if (drv->vp == NULL || drv->vp->set_key == NULL) {
		SKEL_NO_BACKEND();
		return -1;
	}
	switch (p->alg) {
	case WPA_ALG_NONE:
		alg = WPA_VENDOR_ALG_NONE;
		break;
	case WPA_ALG_CCMP:
		alg = WPA_VENDOR_ALG_CCMP;
		break;
	default:
		/* the PSK build only negotiates CCMP; a clear-key request
		 * with an unset algorithm is benign noise */
		wpa_printf(MSG_INFO, "wpa: unsupported key alg %d",
		    p->alg);
		return -1;
	}

	memset(&k, 0, sizeof(k));
	k.alg = alg;
	k.key_idx = p->key_idx;
	k.addr = p->addr;
	k.set_tx = p->set_tx;
	k.key = p->key;
	k.key_len = p->key_len;
	k.seq = p->seq;
	k.seq_len = p->seq_len;
	return drv->vp->set_key(&k);
}

/* the vendor MLME/firmware own the port opening: the pairwise key install
 * after the 4-way handshake is what lets data frames through */
static int skel_set_supp_port(void *priv, int authorized) {
	(void) priv;
	(void) authorized;
	return 0;
}

static int skel_get_capa(void *priv, struct wpa_driver_capa *capa) {
	struct wpa_vendor_drv_data *drv = priv;

	*capa = drv->capa;
	return 0;
}

/* the symbol keeps the vanilla drivers.c export name
 * (CONFIG_DRIVER_EMBOX) so the vendored tree stays untouched */
const struct wpa_driver_ops wpa_driver_embox_ops = {
	.name = "embox",
	.desc = "vendor driver wrapper skeleton",
	.global_init = skel_global_init,
	.global_deinit = skel_global_deinit,
	.init2 = skel_init2,
	.deinit = skel_deinit,
	.get_bssid = skel_get_bssid,
	.get_ssid = skel_get_ssid,
	.set_countermeasures = skel_set_countermeasures,
	.scan2 = skel_scan2,
	.get_scan_results2 = skel_get_scan_results2,
	.deauthenticate = skel_deauthenticate,
	.associate = skel_associate,
	.get_capa = skel_get_capa,
	.set_key = skel_set_key,
	.set_supp_port = skel_set_supp_port,
};
