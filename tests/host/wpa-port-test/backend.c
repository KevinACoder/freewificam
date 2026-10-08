/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    backend.c
 * @brief   The scripted vendor backend: NIC side and scripted AP peer.
 *
 * The NIC side implements the nine vendor operations (ports/wpa_supplicant/
 * wpa_vendor_port.h): it records what the supplicant asked for (survey,
 * connect, keys, EAPOL out) and reports events through the registered pump,
 * exactly as a driver worker would. The AP side builds the 4-way messages
 * from the clause-level frame layout and checks the supplicant's replies
 * against key material this file derives itself (PMK from the published
 * PBKDF2 sample pair, PTK from its own PRF-512 loop over the HMAC-SHA1
 * primitive) - the peer's expected values never come from the
 * implementation's own output.
 *
 * @design scripted peer (suite harness; PRODUCT code does not live here).
 *         Frame layout: EAPOL header (IEEE 802.1X-2004 clause 7) + EAPOL-Key
 *         body (IEEE 802.11-2020 Table 12-6 field order: descriptor type 1,
 *         key info 2, key length 2, replay counter 8, nonce 32, IV 16, RSC 8,
 *         key ID 8, MIC, key data length 2, key data). GTK KDE selector
 *         00:0f:ac:01 per the standard's KDE registry. M1 key_info 0x008a
 *         (ver 2 | pairwise | ACK) matches this project's recorded board
 *         trace of a real AP's M1.
 *
 * @date    09.10.2026
 */

#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include <includes.h>

#include "utils/common.h"
#include "common/wpa_common.h"
#include "crypto/sha1.h"
#include "crypto/aes_wrap.h"

#include "wpa_vendor_port.h"

#include "backend.h"

/* ---- the scripted identities ---------------------------------------------- */

static const uint8_t ap_bssid[6] = { 0x02, 0x00, 0x00, 0x00, 0x01, 0x00 };
static const uint8_t sta_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
#define BEACON_INT 100

/* The published IEEE 802.11i sample pair (passphrase "password", SSID
 * "IEEE" -> PMK f42c6f...a12e); the suite's vector case re-derives and
 * checks it, so the smoke's key material stands on published truth. */
#define TEST_SSID "IEEE"
#define TEST_PSK "password"

static const uint8_t anonce_vec[32] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static const uint8_t gtk_vec[16] = {
	0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,
	0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00
};

/* the RSN IE a WPA2-PSK/CCMP AP advertises (IEEE 802.11-2020 9.4.2.25:
 * version, group CCMP, pairwise CCMP, AKM PSK, caps) */
static const uint8_t rsn_ie[22] = {
	0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
	0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
	0x00, 0x0f, 0xac, 0x02, 0x00, 0x00
};

/* ---- recorded conversation ------------------------------------------------ */

struct rec {
	int scan_start_calls;
	int scan_fetch_calls;
	uint8_t scan_ssid[33];
	size_t scan_ssid_len;

	int connect_calls;
	uint8_t conn_ssid[33];
	size_t conn_ssid_len;
	uint8_t conn_wpa_ie[128];
	size_t conn_wpa_ie_len;
	int conn_bssid_set;
	uint8_t conn_bssid[6];

	int eapol_tx_count;
	int disconnect_calls;
	int clear_key_calls;

	int key_count;
	struct {
		int alg;
		int key_idx;
		int set_tx;
		int group;
		uint8_t buf[32];
		size_t len;
	} keys[4];

	/* the AP snapshot */
	uint8_t pmk[32];
	int ap_enabled;
	uint8_t snonce[32];
	int have_snonce;
	uint8_t ptk[48];
	int ptk_ok;
	int m3_sent;
	int m4_ok;
	int m1_count;

	/* fault knobs */
	const char *psk_override;
	int deny_scan_done;
	int stall_scan_ms;
	volatile int stall_active;
	int emit_on_connect;	/* emit ASSOC (+M1) from the connect op */
};

static struct rec r;

/* the event pump the skeleton registered */
static void (*ev_fn)(int event, const uint8_t *addr, const uint8_t *buf,
    size_t len, void *arg);
static void *ev_arg;

void backend_reset(void) {
	memset(&r, 0, sizeof(r));
	r.emit_on_connect = 1;
}

/* ---- independent key derivation (the peer's own arithmetic) --------------- */

/* PRF-512 of IEEE 802.11i 8.5.1.1: HMAC-SHA1 over label||0x00||data||i,
 * i as one byte counting from zero. Written here, over the SHA-1 primitive,
 * so the peer does not share the product's PTK code path. */
static void peer_prf512(const uint8_t *key, size_t key_len,
    const char *label, const uint8_t *data, size_t data_len,
    uint8_t *out, size_t out_len) {
	uint8_t buf[3 * 64 + 1];
	size_t label_len = strlen(label) + 1;
	size_t pos = 0;
	uint8_t i = 0;
	uint8_t hash[20];

	memcpy(buf, label, label_len);
	memcpy(buf + label_len, data, data_len);
	buf[label_len + data_len] = 0;

	while (pos < out_len) {
		size_t n = out_len - pos;

		buf[label_len + data_len] = i;
		if (hmac_sha1(key, key_len, buf, label_len + data_len + 1,
			hash) < 0) {
			return; /* never on this path */
		}
		if (n > sizeof(hash)) {
			n = sizeof(hash);
		}
		memcpy(out + pos, hash, n);
		pos += n;
		i++;
	}
}

static void peer_ptk(const uint8_t *pmk, const uint8_t *anonce,
    const uint8_t *snonce, uint8_t ptk[48]) {
	uint8_t data[2 * 6 + 2 * 32];

	if (memcmp(sta_mac, ap_bssid, 6) < 0) {
		memcpy(data, sta_mac, 6);
		memcpy(data + 6, ap_bssid, 6);
	} else {
		memcpy(data, ap_bssid, 6);
		memcpy(data + 6, sta_mac, 6);
	}
	if (memcmp(anonce, snonce, 32) < 0) {
		memcpy(data + 12, anonce, 32);
		memcpy(data + 44, snonce, 32);
	} else {
		memcpy(data + 12, snonce, 32);
		memcpy(data + 44, anonce, 32);
	}
	peer_prf512(pmk, 32, "Pairwise key expansion", data, sizeof(data),
	    ptk, 48);
}

/* HMAC-SHA1 over the frame with its MIC field zeroed - the EAPOL-Key MIC
 * rule (IEEE 802.11-2020 12.7.2: the MIC covers the whole EAPOL-Key frame
 * with the MIC field set to zero). */
static int peer_check_mic(const uint8_t *eapol, size_t len, size_t mic_off,
    const uint8_t *kck) {
	uint8_t tmp[256];
	uint8_t mac[20];

	if (len > sizeof(tmp) || mic_off + 16 > len) {
		return -1;
	}
	memcpy(tmp, eapol, len);
	memset(tmp + mic_off, 0, 16);
	if (hmac_sha1(kck, 16, tmp, len, mac) < 0) {
		return -1;
	}
	return memcmp(mac, eapol + mic_off, 16) == 0 ? 0 : -1;
}

static void peer_put_mic(uint8_t *eapol, size_t len, size_t mic_off,
    const uint8_t *kck) {
	uint8_t mac[20];

	memset(eapol + mic_off, 0, 16);
	hmac_sha1(kck, 16, eapol, len, mac);
	memcpy(eapol + mic_off, mac, 16);
}

/* ---- EAPOL-Key frame building --------------------------------------------- */

#define EAPOL_HDR_LEN 4
#define KEY_BODY_LEN 77		/* through key ID, Table 12-6 field order */
#define MIC_LEN 16		/* PSK: HMAC-SHA1 MIC length */
#define KD_LEN_OFF (EAPOL_HDR_LEN + KEY_BODY_LEN + MIC_LEN)
#define KD_OFF (KD_LEN_OFF + 2)

static void put_be16(uint8_t *p, uint16_t v) {
	p[0] = (uint8_t) (v >> 8);
	p[1] = (uint8_t) v;
}

/* header: version 2 (RSN), type 3 (EAPOL-Key), BE16 body length */
static size_t peer_build_head(uint8_t *f, uint16_t body_len) {
	f[0] = 0x02;
	f[1] = 0x03;
	put_be16(f + 2, body_len);
	return EAPOL_HDR_LEN;
}

static void peer_build_key_body(uint8_t *f, uint16_t key_info,
    uint16_t key_length, uint64_t replay, const uint8_t *nonce,
    const uint8_t *rsc) {
	size_t o = EAPOL_HDR_LEN;

	memset(f + o, 0, KEY_BODY_LEN);
	f[o] = 0x02;			/* descriptor type: RSN */
	put_be16(f + o + 1, key_info);
	put_be16(f + o + 3, key_length);
	/* replay counter, 8 bytes big endian */
	for (int i = 0; i < 8; i++) {
		f[o + 5 + i] = (uint8_t) (replay >> (8 * (7 - i)));
	}
	if (nonce != NULL) {
		memcpy(f + o + 13, nonce, 32);
	}
	if (rsc != NULL) {
		memcpy(f + o + 63, rsc, 8);	/* key IV(16) then RSC(8) */
	}
}

/* M1: no MIC, empty key data - 99 bytes, the length a real AP's M1 had on
 * this project's board trace; key_info 0x008a. */
static size_t peer_build_m1(uint8_t *f) {
	peer_build_head(f, KEY_BODY_LEN + MIC_LEN + 2);
	peer_build_key_body(f, 0x008a, 0, 1, anonce_vec, NULL);
	memset(f + EAPOL_HDR_LEN + KEY_BODY_LEN, 0, MIC_LEN);
	put_be16(f + KD_LEN_OFF, 0);
	return KD_OFF;
}

/* M3: MIC + SECURE + INSTALL + ENCR_KEY_DATA; Key Data = the RSN IE (which
 * must match the Beacon/ProbeResp IE byte for byte) + the GTK KDE + the
 * 0xdd 0x00 pad the parser skips, all wrapped with the KEK (AES Key Wrap,
 * RFC 3394; the unwrap the supplicant performs). */
static size_t peer_build_m3(uint8_t *f, const uint8_t *kck,
    const uint8_t *kek) {
	uint8_t kd[48];
	uint8_t wrapped[56];

	memcpy(kd, rsn_ie, sizeof(rsn_ie));	/* 22 bytes */
	kd[22] = 0xdd;
	kd[23] = 22;		/* GTK KDE: 2 hdr + 4 selector + 2 info + 16 GTK */
	kd[24] = 0x00;
	kd[25] = 0x0f;
	kd[26] = 0xac;
	kd[27] = 0x01;		/* GTK KDE selector */
	kd[28] = 0x01;		/* key index 1, tx bit clear */
	kd[29] = 0x00;		/* reserved */
	memcpy(kd + 30, gtk_vec, 16);
	kd[46] = 0xdd;		/* padding IE the parser skips */
	kd[47] = 0x00;

	if (aes_wrap(kek, 16, 6, kd, wrapped) < 0) {
		return 0;
	}

	peer_build_head(f, KEY_BODY_LEN + MIC_LEN + 2 + 56);
	peer_build_key_body(f, 0x13ca, 16, 2, anonce_vec, NULL);
	put_be16(f + KD_LEN_OFF, 56);
	memcpy(f + KD_OFF, wrapped, 56);
	peer_put_mic(f, KD_OFF + 56, EAPOL_HDR_LEN + KEY_BODY_LEN, kck);
	return KD_OFF + 56;
}

/* ---- the MIC checks the AP side performs on the supplicant's replies ------- */

static int peer_on_m2(const uint8_t *f, size_t len) {
	size_t mic_off = EAPOL_HDR_LEN + KEY_BODY_LEN;
	const uint8_t *snonce = f + EAPOL_HDR_LEN + 13;

	if (len < KD_OFF) {
		fprintf(stderr, "wpa-port-test: M2 too short (%zu)\n", len);
		return -1;
	}
	memcpy(r.snonce, snonce, 32);
	r.have_snonce = 1;
	peer_ptk(r.pmk, anonce_vec, r.snonce, r.ptk);
	r.ptk_ok = 1;
	if (peer_check_mic(f, len, mic_off, r.ptk) != 0) {
		fprintf(stderr, "wpa-port-test: M2 MIC does not verify\n");
		return -1;
	}
	return 0;
}

static int peer_on_m4(const uint8_t *f, size_t len) {
	if (!r.ptk_ok) {
		return -1;
	}
	if (peer_check_mic(f, len, EAPOL_HDR_LEN + KEY_BODY_LEN, r.ptk) != 0) {
		fprintf(stderr, "wpa-port-test: M4 MIC does not verify\n");
		return -1;
	}
	r.m4_ok = 1;
	return 0;
}

/* ---- the vendor operations ------------------------------------------------- */

static int op_ensure_up(void) {
	return 0;
}

static int op_get_hwaddr(uint8_t addr[6]) {
	memcpy(addr, sta_mac, 6);
	return 0;
}

/* the active AP entry for the next fetch */
static const uint8_t *ap_ssid = (const uint8_t *) TEST_SSID;
static size_t ap_ssid_len = sizeof(TEST_SSID) - 1;
static int ap_open;		/* 1 = open network entry (no RSN IE) */

static int op_scan_start(const uint8_t *ssid, size_t len) {
	r.scan_start_calls++;
	r.scan_ssid_len = len > 32 ? 32 : len;
	if (len != 0 && ssid != NULL) {
		memcpy(r.scan_ssid, ssid, r.scan_ssid_len);
	}
	if (len != 0 && ssid != NULL && len == 7 &&
	    memcmp(ssid, "OpenNet", 7) == 0) {
		ap_open = 1;
		ap_ssid = (const uint8_t *) "OpenNet";
		ap_ssid_len = 7;
	} else {
		ap_open = 0;
		ap_ssid = (const uint8_t *) TEST_SSID;
		ap_ssid_len = sizeof(TEST_SSID) - 1;
	}

	if (r.stall_scan_ms > 0) {
		/* hold the eloop thread: the scan radio work runs there, so
		 * a control call issued meanwhile cannot be picked up */
		r.stall_active = 1;
		osDelay((uint32_t) r.stall_scan_ms);
		r.stall_active = 0;
	}
	if (!r.deny_scan_done && ev_fn != NULL) {
		ev_fn(WPA_VENDOR_EVENT_SCAN_DONE, NULL, NULL, 0, ev_arg);
	}
	return 0;
}

static int op_scan_fetch(int idx, struct wpa_scan_res_ret *out) {
	if (idx != 0) {
		return -1;
	}
	r.scan_fetch_calls++;
	memset(out, 0, sizeof(*out));
	memcpy(out->bssid, ap_bssid, 6);
	memcpy(out->ssid, ap_ssid, ap_ssid_len);
	out->ssid_len = ap_ssid_len;
	out->freq = 2412;
	out->beacon_int = BEACON_INT;
	out->caps = ap_open ? 0x0001 : 0x0011;	/* ESS; | Privacy when WPA2 */
	out->level = -40;
	if (!ap_open) {
		out->ies = rsn_ie;
		out->ies_len = sizeof(rsn_ie);
	}
	return 0;
}

static int op_connect(const struct wpa_vendor_conn *p) {
	r.connect_calls++;
	r.conn_ssid_len = p->ssid_len > 32 ? 32 : p->ssid_len;
	memcpy(r.conn_ssid, p->ssid, r.conn_ssid_len);
	r.conn_wpa_ie_len = p->wpa_ie_len > sizeof(r.conn_wpa_ie) ?
	    sizeof(r.conn_wpa_ie) : p->wpa_ie_len;
	if (p->wpa_ie != NULL && r.conn_wpa_ie_len != 0) {
		memcpy(r.conn_wpa_ie, p->wpa_ie, r.conn_wpa_ie_len);
	}
	if (p->bssid_set && p->bssid != NULL) {
		r.conn_bssid_set = 1;
		memcpy(r.conn_bssid, p->bssid, 6);
	}

	if (r.emit_on_connect && ev_fn != NULL) {
		ev_fn(WPA_VENDOR_EVENT_ASSOC, ap_bssid, NULL, 0, ev_arg);
		if (p->wpa_ie != NULL) {
			/* a PSK network: the AP opens the 4-way with M1 */
			uint8_t f[128];
			size_t n = peer_build_m1(f);

			r.m1_count++;
			ev_fn(WPA_VENDOR_EVENT_EAPOL_RX, ap_bssid, f, n,
			    ev_arg);
		}
	}
	return 0;
}

static int op_disconnect(uint16_t reason) {
	(void) reason;
	r.disconnect_calls++;
	if (ev_fn != NULL) {
		ev_fn(WPA_VENDOR_EVENT_DISASSOC, ap_bssid, NULL, 0, ev_arg);
	}
	return 0;
}

static int op_set_key(const struct wpa_vendor_key *k) {
	int n;

	/* key-clearing calls (ALG_NONE) arrive around association and are
	 * not key installations; they must not pollute the record */
	if (k->alg == WPA_VENDOR_ALG_NONE) {
		r.clear_key_calls++;
		return 0;
	}
	if (r.key_count >= 4) {
		return -1;
	}
	n = r.key_count++;
	r.keys[n].alg = k->alg;
	r.keys[n].key_idx = k->key_idx;
	r.keys[n].set_tx = k->set_tx;
	r.keys[n].group = k->addr == NULL ||
	    (k->addr[0] & 0x01) != 0;
	r.keys[n].len = k->key_len > 32 ? 32 : k->key_len;
	memcpy(r.keys[n].buf, k->key, r.keys[n].len);
	return 0;
}

static int op_eapol_xmit(const uint8_t dst[6], const uint8_t *buf,
    size_t len) {
	uint8_t f[256];

	(void) dst;
	r.eapol_tx_count++;
	if (buf == NULL || len > sizeof(f)) {
		return -1;
	}
	memcpy(f, buf, len);

	/* the first frame out is M2; once M3 has been sent, the next one is
	 * M4 (a repeated M2 is answered again with M3) */
	if (!r.m3_sent) {
		if (peer_on_m2(f, len) != 0) {
			return -1;
		}
		if (ev_fn != NULL) {
			uint8_t m3[160];
			size_t n = peer_build_m3(m3, r.ptk, r.ptk + 16);

			if (n == 0) {
				return -1;
			}
			r.m3_sent = 1;
			ev_fn(WPA_VENDOR_EVENT_EAPOL_RX, ap_bssid, m3, n,
			    ev_arg);
		}
	} else if (!r.m4_ok) {
		if (peer_on_m4(f, len) != 0) {
			/* a repeated M2 has no MIC over the PTK; if the MIC
			 * does not verify and no key is installed yet, treat
			 * it as the supplicant asking again and re-send M3 */
			if (r.key_count == 0) {
				r.m4_ok = 0;
				return 0;
			}
			return -1;
		}
	}
	return 0;
}

static void op_set_event_cb(void (*fn)(int event, const uint8_t *addr,
	const uint8_t *buf, size_t len, void *arg), void *arg) {
	ev_fn = fn;
	ev_arg = arg;
}

static const struct wpa_vendor_port scripted = {
	.ensure_up = op_ensure_up,
	.get_hwaddr = op_get_hwaddr,
	.scan_start = op_scan_start,
	.scan_fetch = op_scan_fetch,
	.connect = op_connect,
	.disconnect = op_disconnect,
	.set_key = op_set_key,
	.eapol_xmit = op_eapol_xmit,
	.set_event_cb = op_set_event_cb,
};

void backend_register(void) {
	/* register first so the event pump slot exists before init2 runs */
	wpa_vendor_port_register(&scripted);
	memset(r.pmk, 0, sizeof(r.pmk));
	if (pbkdf2_sha1(r.psk_override != NULL ? r.psk_override : TEST_PSK,
		(const uint8_t *) TEST_SSID, sizeof(TEST_SSID) - 1,
		4096, r.pmk, sizeof(r.pmk)) < 0) {
		fprintf(stderr, "wpa-port-test: PMK derivation failed\n");
	}
}

/* ---- accessors ------------------------------------------------------------- */

int backend_scan_start_calls(void) { return r.scan_start_calls; }
int backend_scan_fetch_calls(void) { return r.scan_fetch_calls; }
const uint8_t *backend_connect_ssid(size_t *len) { *len = r.conn_ssid_len; return r.conn_ssid; }
const uint8_t *backend_connect_wpa_ie(size_t *len) { *len = r.conn_wpa_ie_len; return r.conn_wpa_ie; }
int backend_connect_bssid_set(void) { return r.conn_bssid_set; }
const uint8_t *backend_connect_bssid(void) { return r.conn_bssid; }
int backend_eapol_tx_count(void) { return r.eapol_tx_count; }
int backend_disconnect_calls(void) { return r.disconnect_calls; }
void backend_deny_scan_done(void) { r.deny_scan_done = 1; }
int backend_key_count(void) { return r.key_count; }
int backend_key_group_index(int n) { return r.keys[n].group; }
int backend_key_alg(int n) { return r.keys[n].alg; }
const uint8_t *backend_key_bytes(int n, size_t *len) { *len = r.keys[n].len; return r.keys[n].buf; }
int backend_key_idx(int n) { return r.keys[n].key_idx; }
int backend_key_set_tx(int n) { return r.keys[n].set_tx; }

const uint8_t *backend_ap_tk(size_t *len) { *len = 16; return r.ptk + 32; }
const uint8_t *backend_ap_kck(size_t *len) { *len = 16; return r.ptk; }
const uint8_t *backend_ap_gtk(size_t *len) { *len = 16; return gtk_vec; }

int backend_ap_sent_m3(void) { return r.m3_sent; }
int backend_ap_expect_m4(void) { return r.m4_ok; }

void backend_emit_scan_done(void) {
	if (ev_fn != NULL) {
		ev_fn(WPA_VENDOR_EVENT_SCAN_DONE, NULL, NULL, 0, ev_arg);
	}
}

void backend_emit_eapol(const uint8_t *buf, size_t len) {
	if (ev_fn != NULL) {
		ev_fn(WPA_VENDOR_EVENT_EAPOL_RX, ap_bssid, buf, len, ev_arg);
	}
}

/* an M1 whose Key Data Length field claims more bytes than the frame holds:
 * the receive path must discard it (a named overflow rejection), never
 * respond and never crash. */
void backend_inject_bad_m1(void) {
	uint8_t f[128];
	size_t n = peer_build_m1(f);

	f[KD_LEN_OFF] = 0x00;
	f[KD_LEN_OFF + 1] = 0xc8;	/* 200 > the frame's actual key data */
	r.m1_count++;
	backend_emit_eapol(f, n);
}

void backend_set_psk_override(const char *wrong_passphrase) {
	r.psk_override = wrong_passphrase;
}

void backend_set_slow_connect(int enable) {
	r.stall_scan_ms = enable ? 10000 : 0;
}

int backend_stall_active(void) {
	return r.stall_active;
}
