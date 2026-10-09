/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    main.c
 * @brief   Gate1 suite for the wpa_supplicant port: vector anchors, then the
 *          real supplicant driven to a completed WPA2-PSK handshake against a
 *          scripted peer, plus the fail-safe and timeout shapes.
 *
 * The binary links the vendored supplicant (the PSK-only 45-unit compile set),
 * the first-party port layer under ports/wpa_supplicant/ and this suite's
 * scripted backend. Every case prints one anchor line
 * `WPAPORT: <id> PASS|FAIL <detail>`; the wrapper (tools/mock/wpa-port-test/
 * check.sh) maps those to container cases and proves each red control.
 *
 * Vector discipline: expected values are hand-derived from the clauses and
 * published samples (RFC 2202 HMAC-SHA1 vector, the IEEE 802.11i PBKDF2
 * sample pair "password"/"IEEE", and a PTK derived independently with the
 * 802.11i 8.5.1.1 PRF - all re-computed with a separate oracle before being
 * written here), never read back from the implementation.
 *
 * @date    09.10.2026
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stddef.h>

#include "os/cmsis2/cmsis2.h"

#include <includes.h>

#include "utils/common.h"
#include "utils/wpa_debug.h"
#include "common/wpa_common.h"
#include "drivers/driver.h"
#include "crypto/sha1.h"

#include "wpa_port_api.h"
#include "wpa_vendor_port.h"

#include "backend.h"

extern void exit(int);

#define TAG "WPAPORT: "
#define CASE_MAX 16

static int failures;
static int cases_total;
static const char *mode = "main";
static const char *fault;

/* ---- anchor capture (wpa_msg text, as emitted) ----------------------------- */

static volatile int seen_negotiation;
static volatile int seen_connected;

static int contains(const char *hay, size_t hay_len, const char *needle) {
	size_t n = strlen(needle);
	size_t i;

	if (n == 0 || hay_len < n) {
		return 0;
	}
	for (i = 0; i + n <= hay_len; i++) {
		if (memcmp(hay + i, needle, n) == 0) {
			return 1;
		}
	}
	return 0;
}

static void msg_capture(void *ctx, int level, enum wpa_msg_type type,
    const char *txt, size_t len) {
	(void) ctx; (void) level; (void) type;

	if (contains(txt, len, "Key negotiation completed with 02:00:00:00:01:00") &&
	    contains(txt, len, "[PTK=CCMP GTK=CCMP]")) {
		seen_negotiation = 1;
	}
	if (contains(txt, len, "CTRL-EVENT-CONNECTED")) {
		seen_connected = 1;
	}
}

/* ---- stdout capture (the port's printf face: startup, status) --------------- */

static int cap_saved = -1;
static int cap_rd = -1;
static char cap_buf[8192];
static size_t cap_used;

static void cap_drain(void) {
	for (;;) {
		ssize_t n = read(cap_rd, cap_buf + cap_used,
		    sizeof(cap_buf) - 1 - cap_used);

		if (n <= 0) {
			break;
		}
		cap_used += (size_t) n;
	}
	cap_buf[cap_used] = '\0';
}

static int cap_begin(void) {
	int p[2];

	fflush(stdout);
	if (pipe(p) != 0) {
		return -1;
	}
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	cap_saved = dup(1);
	if (cap_saved < 0 || dup2(p[1], 1) < 0) {
		return -1;
	}
	close(p[1]);
	cap_rd = p[0];
	cap_used = 0;
	return 0;
}

static const char *cap_end(void) {
	fflush(stdout);
	if (cap_rd >= 0) {
		cap_drain();
		close(cap_rd);
		cap_rd = -1;
	}
	if (cap_saved >= 0) {
		dup2(cap_saved, 1);
		close(cap_saved);
		cap_saved = -1;
	}
	fflush(stdout);
	return cap_buf;
}

/* keep capturing until the needle shows up (or the budget runs out) */
static int cap_wait_for(const char *needle, unsigned timeout_ms) {
	unsigned waited = 0;

	for (;;) {
		fflush(stdout);
		cap_drain();
		if (strstr(cap_buf, needle) != NULL) {
			return 0;
		}
		if (waited >= timeout_ms) {
			return -1;
		}
		osDelay(20);
		waited += 20;
	}
}

/* ---- case reporting --------------------------------------------------------- */

static void case_line(const char *id, int ok, const char *detail) {
	cases_total++;
	if (ok) {
		printf(TAG "%s PASS %s\n", id, detail);
	} else {
		printf(TAG "%s FAIL %s\n", id, detail);
		failures++;
	}
	fflush(stdout);
}

/* ---- vector anchors ---------------------------------------------------------- */

static int vec_hmac(void) {
	static const uint8_t key[20] = {
		0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
		0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
		0x0b, 0x0b, 0x0b, 0x0b
	};
	/* RFC 2202 test case 1: HMAC-SHA1("Hi There", 20 x 0x0b) */
	static const uint8_t expect[20] = {
		0xb6, 0x17, 0x31, 0x86, 0x55, 0x05, 0x72, 0x64,
		0xe2, 0x8b, 0xc0, 0xb6, 0xfb, 0x37, 0x8c, 0x8e,
		0xf1, 0x46, 0xbe, 0x00
	};
	uint8_t mac[20];

	hmac_sha1(key, sizeof(key), (const uint8_t *) "Hi There", 8, mac);
	return memcmp(mac, expect, sizeof(expect)) == 0;
}

static int vec_psk(void) {
	/* the published IEEE 802.11i sample pair: PBKDF2-SHA1(password,
	 * "IEEE", 4096) - re-derived with an independent oracle */
	static const uint8_t expect[32] = {
		0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef,
		0x9e, 0xbb, 0x4b, 0x90, 0xb3, 0x8a, 0x5f, 0x90,
		0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2,
		0x3a, 0xed, 0x76, 0x2e, 0x97, 0x10, 0xa1, 0x2e
	};
	uint8_t pmk[32];

	if (pbkdf2_sha1("password", (const uint8_t *) "IEEE", 4, 4096,
		pmk, sizeof(pmk)) < 0) {
		return 0;
	}
	return memcmp(pmk, expect, sizeof(expect)) == 0;
}

static int vec_ptk(void) {
	/* fixed inputs: PMK = the published sample above, addresses
	 * 02:00:00:00:00:01 / 02:00:00:00:01:00, ANonce 00..1f, SNonce 20..3f;
	 * expected PTK computed with the 802.11i 8.5.1.1 PRF-512 in Python
	 * (HMAC-SHA1 oracle, block-by-block) before this case was written */
	static const uint8_t pmk[32] = {
		0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef,
		0x9e, 0xbb, 0x4b, 0x90, 0xb3, 0x8a, 0x5f, 0x90,
		0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2,
		0x3a, 0xed, 0x76, 0x2e, 0x97, 0x10, 0xa1, 0x2e
	};
	static const uint8_t spa[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
	static const uint8_t aa[6] = { 0x02, 0x00, 0x00, 0x00, 0x01, 0x00 };
	static const uint8_t expect[48] = {
		0x4d, 0xb8, 0x7e, 0x29, 0x32, 0xf8, 0x6f, 0x9f,
		0xc5, 0xf8, 0xd6, 0xe0, 0xa5, 0x0d, 0xc0, 0x61,
		0x10, 0x12, 0x4a, 0x46, 0xe5, 0x35, 0x55, 0x1c,
		0xdc, 0x96, 0xab, 0x3d, 0x90, 0xca, 0xc7, 0x11,
		0xea, 0xad, 0xed, 0x1f, 0x91, 0x02, 0x1a, 0x5b,
		0x97, 0x72, 0x99, 0x41, 0xd1, 0xd9, 0x98, 0xf2
	};
	uint8_t anonce[32], snonce[32];
	struct wpa_ptk ptk;
	uint8_t got[48];
	int i;

	for (i = 0; i < 32; i++) {
		anonce[i] = (uint8_t) i;
		snonce[i] = (uint8_t) (0x20 + i);
	}

	/* the product's derivation */
	memset(&ptk, 0, sizeof(ptk));
	if (wpa_pmk_to_ptk(pmk, sizeof(pmk), "Pairwise key expansion",
		spa, aa, anonce, snonce, &ptk, WPA_KEY_MGMT_PSK,
		WPA_CIPHER_CCMP, NULL, 0, 0) < 0) {
		return 0;
	}
	memcpy(got, ptk.kck, 16);
	memcpy(got + 16, ptk.kek, 16);
	memcpy(got + 32, ptk.tk, 16);
	if (ptk.kck_len != 16 || ptk.kek_len != 16 || ptk.tk_len != 16) {
		return 0;
	}
	return memcmp(got, expect, sizeof(expect)) == 0;
}

/* the EAPOL-Key body layout of IEEE 802.11-2020 Table 12-6, as the product's
 * struct must encode it (offsets inside the struct, body = 77 bytes) */
static int vec_eapolkey(void) {
	return offsetof(struct wpa_eapol_key, key_info) == 1 &&
	    offsetof(struct wpa_eapol_key, key_length) == 3 &&
	    offsetof(struct wpa_eapol_key, replay_counter) == 5 &&
	    offsetof(struct wpa_eapol_key, key_nonce) == 13 &&
	    offsetof(struct wpa_eapol_key, key_iv) == 45 &&
	    offsetof(struct wpa_eapol_key, key_rsc) == 61 &&
	    offsetof(struct wpa_eapol_key, key_id) == 69 &&
	    sizeof(struct wpa_eapol_key) == 77;
}

/* a fixed RSN IE (the WPA2-PSK/CCMP template this suite's AP advertises)
 * parsed through the product's parser: fields per 802.11-2020 9.4.2.25 */
static int vec_rsnie(void) {
	static const uint8_t ie[22] = {
		0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
		0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
		0x00, 0x0f, 0xac, 0x02, 0x00, 0x00
	};
	uint8_t bad[10];
	struct wpa_ie_data d;

	memset(&d, 0, sizeof(d));
	if (wpa_parse_wpa_ie_rsn(ie, sizeof(ie), &d) < 0) {
		return 0;
	}
	if (d.proto != WPA_PROTO_RSN || d.group_cipher != WPA_CIPHER_CCMP ||
	    d.pairwise_cipher != WPA_CIPHER_CCMP ||
	    d.key_mgmt != WPA_KEY_MGMT_PSK || d.capabilities != 0) {
		return 0;
	}
	/* negative: the same element header with a truncated body */
	memcpy(bad, ie, sizeof(bad));
	memset(&d, 0, sizeof(d));
	return wpa_parse_wpa_ie_rsn(bad, sizeof(bad), &d) < 0;
}

/* ---- the scenarios ----------------------------------------------------------- */

static int wait_flag(volatile int *flag, unsigned timeout_ms) {
	unsigned waited = 0;

	while (!*flag) {
		if (waited >= timeout_ms) {
			return -1;
		}
		osDelay(20);
		waited += 20;
	}
	return 0;
}

static int wait_expr(int (*fn)(void), unsigned timeout_ms) {
	unsigned waited = 0;

	while (!fn()) {
		if (waited >= timeout_ms) {
			return -1;
		}
		osDelay(20);
		waited += 20;
	}
	return 0;
}

static int fourway_done(void) {
	return backend_ap_expect_m4() && backend_key_count() >= 2;
}

static char detail[256];

static void case_vectors(void) {
	case_line("vectors.hmac", vec_hmac(), "RFC2202 HMAC-SHA1");
	case_line("vectors.psk", vec_psk(), "IEEE 802.11i sample pair");
	case_line("vectors.ptk", vec_ptk(), "PRF-512 oracle");
	case_line("vectors.eapolkey", vec_eapolkey(), "Table 12-6 layout");
	case_line("vectors.rsnie", vec_rsnie(), "9.4.2.25 fields + negative");
}

static int smoke_start(void) {
	const char *out;

	if (cap_begin() != 0) {
		return 0;
	}
	if (wpa_port_start() != 0) {
		cap_end();
		return 0;
	}
	if (cap_wait_for("wpa: supplicant running", 5000) != 0) {
		cap_end();
		return 0;
	}
	if (wpa_port_status() != 0) {
		cap_end();
		return 0;
	}
	if (cap_wait_for("wpa: state=", 5000) != 0) {
		cap_end();
		return 0;
	}
	out = cap_end();
	return contains(out, strlen(out), "supplicant running");
}

static int case_start(void) {
	int ok = smoke_start();

	case_line("smoke.start", ok, ok ? "running + status" : "no startup anchor");
	return ok;
}

/* connect a PSK network and run the full ladder; the scan-pull and fourway
 * cases both judge features of the same exchange */
static int ladder_ok = -1;

static void case_psk_ladder(void) {
	static const uint8_t ap_ie[22] = {
		0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
		0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
		0x00, 0x0f, 0xac, 0x02, 0x00, 0x00
	};
	size_t ie_len = 0, ssid_len = 0;
	const uint8_t *ie;
	const uint8_t *ssid;
	unsigned eapol_before;
	int rc;

	seen_negotiation = 0;
	seen_connected = 0;

	rc = wpa_port_connect("IEEE", "password");
	if (rc != 0) {
		case_line("smoke.scan-pull", 0, "connect request refused");
		case_line("smoke.fourway", 0, "connect request refused");
		ladder_ok = 0;
		return;
	}

	/* the scripted peer answers synchronously inside the association, so
	 * these bounded waits cover the whole single-threaded exchange */
	if (wait_expr(fourway_done, 15000) != 0) {
		snprintf(detail, sizeof(detail),
		    "handshake incomplete m3=%d m4=%d keys=%d tx=%d",
		    backend_ap_sent_m3(), backend_ap_expect_m4(),
		    backend_key_count(), backend_eapol_tx_count());
		case_line("smoke.scan-pull", 0, detail);
		case_line("smoke.fourway", 0, detail);
		ladder_ok = 0;
		return;
	}
	if (wait_flag(&seen_negotiation, 5000) != 0) {
		case_line("smoke.scan-pull", 0, "no negotiation anchor");
		case_line("smoke.fourway", 0, "no negotiation anchor");
		ladder_ok = 0;
		return;
	}

	/* the pull model: survey request carried the SSID, the table was
	 * fetched, and the association carried the RSN IE the network asked
	 * for (byte-identical to the suite's template) */
	ie = backend_connect_wpa_ie(&ie_len);
	ssid = backend_connect_ssid(&ssid_len);
	snprintf(detail, sizeof(detail),
	    "scan=%d fetch=%d ie=%zu", backend_scan_start_calls(),
	    backend_scan_fetch_calls(), ie_len);
	case_line("smoke.scan-pull",
	    backend_scan_start_calls() >= 1 && backend_scan_fetch_calls() >= 1 &&
	    ssid != NULL && ssid_len == 4 && memcmp(ssid, "IEEE", 4) == 0 &&
	    ie_len == 22 && memcmp(ie, ap_ie, 22) == 0,
	    detail);

	/* the four-way: both CCMP keys installed with the peer's material, in
	 * the handshake's order and with the transmit bits the exchange implies */
	{
		size_t tk_len = 0, gtk_len = 0, kn_len = 0;
		const uint8_t *tk = backend_ap_tk(&tk_len);
		const uint8_t *gtk = backend_ap_gtk(&gtk_len);
		const uint8_t *k0;
		int pair_ok = 0, group_ok = 0, order_ok = 0, n;

		for (n = 0; n < backend_key_count(); n++) {
			const uint8_t *kb = backend_key_bytes(n, &kn_len);

			if (backend_key_alg(n) != WPA_VENDOR_ALG_CCMP ||
			    kn_len != 16) {
				continue;
			}
			if (backend_key_group_index(n) == 0 && kb != NULL &&
			    memcmp(kb, tk, 16) == 0) {
				pair_ok = 1;
			}
			if (backend_key_group_index(n) == 1 && kb != NULL &&
			    backend_key_idx(n) == 1 &&
			    memcmp(kb, gtk, 16) == 0) {
				group_ok = 1;
			}
		}
		/*
		 * The order and the transmit bits, both from the exchange itself
		 * (wpa.c:963 installs the PTK with set_tx 1 as M2 is answered,
		 * wpa.c:1075 installs the GTK after M3 with the Tx bit the AP
		 * sent - clear here). The pairwise-first order is the handshake's
		 * requirement, and a port that promoted the group key to the
		 * default transmit key would put every station uplink frame under
		 * key id 1, which the peer drops: the board round had to rule that
		 * out by reading the air.
		 */
		order_ok = backend_key_count() >= 2 &&
		    backend_key_group_index(0) == 0 && backend_key_idx(0) == 0 &&
		    backend_key_set_tx(0) == 1 &&
		    backend_key_group_index(1) == 1 && backend_key_idx(1) == 1 &&
		    backend_key_set_tx(1) == 0;

		snprintf(detail, sizeof(detail),
		    "keys=%d pair=%d group=%d order=%d m4=%d tx=%d connected=%d",
		    backend_key_count(), pair_ok, group_ok, order_ok,
		    backend_ap_expect_m4(), backend_eapol_tx_count(),
		    seen_connected);
		case_line("smoke.fourway",
		    pair_ok && group_ok && order_ok && seen_connected &&
		    backend_eapol_tx_count() >= 2,
		    detail);
	}
	ladder_ok = 1;

	/* the state the control face reports */
	{
		if (cap_begin() == 0) {
			int completed;

			wpa_port_status();
			completed = cap_wait_for("wpa: state=COMPLETED", 3000) == 0;
			cap_end();
			if (!completed) {
				case_line("smoke.fourway", 0, "status not COMPLETED");
				ladder_ok = 0;
			}
		}
	}

	/* a frame whose Key Data Length overruns the buffer: discarded with
	 * no reply */
	eapol_before = (unsigned) backend_eapol_tx_count();
	backend_inject_bad_m1();
	osDelay(200);
	case_line("eapol.badframe",
	    (unsigned) backend_eapol_tx_count() == eapol_before,
	    "overflow discarded, no reply");
}

/* the open network: association completes without any 4-way, and the
 * disconnect/reconnect round closes the loop */
static void case_open_round(void) {
	int rc;
	int ok;

	rc = wpa_port_connect_bssid("OpenNet", NULL, NULL);
	if (rc != 0) {
		case_line("smoke.open", 0, "open connect refused");
		return;
	}
	/* association is emitted by the peer; wait until the control face
	 * reports COMPLETED (open networks skip the key exchange) */
	{
		unsigned waited = 0;
		int completed = 0;

		while (waited < 8000) {
			if (cap_begin() == 0) {
				wpa_port_status();
				completed = cap_wait_for("wpa: state=COMPLETED", 500) == 0;
				cap_end();
			}
			if (completed) {
				break;
			}
			osDelay(100);
			waited += 100;
		}
		if (!completed) {
			case_line("smoke.open", 0, "no COMPLETED state");
			return;
		}
	}

	/* disconnect, then reconnect: the second association must arrive */
	if (wpa_port_disconnect() != 0) {
		case_line("smoke.open", 0, "disconnect job failed");
		return;
	}
	osDelay(300);

	rc = wpa_port_connect("OpenNet", NULL);
	if (rc != 0) {
		case_line("smoke.open", 0, "reconnect refused");
		return;
	}
	ok = 0;
	{
		unsigned waited = 0;

		while (waited < 8000) {
			if (cap_begin() == 0) {
				wpa_port_status();
				ok = cap_wait_for("wpa: state=COMPLETED", 500) == 0;
				cap_end();
			}
			if (ok) {
				break;
			}
			osDelay(100);
			waited += 100;
		}
	}
	snprintf(detail, sizeof(detail), "disconnect=%d reconnected=%d",
	    backend_disconnect_calls(), ok);
	case_line("smoke.open", ok && backend_disconnect_calls() >= 1, detail);
}

/* the job timeout: a control request issued while the eloop thread is
 * inside a stalled vendor operation cannot be picked up within its budget
 * and must return -1, leaving the loop alive afterwards */
static void case_timeout(void) {
	unsigned waited;
	int rc1, rc2;
	int saw_stall = 0;
	int alive;

	backend_set_slow_connect(1);
	rc1 = wpa_port_connect("IEEE", "password");

	/* the scan radio work runs on the eloop thread shortly after the
	 * request; wait until it is inside the stall */
	for (waited = 0; waited < 4000; waited += 20) {
		if (backend_stall_active()) {
			saw_stall = 1;
			break;
		}
		osDelay(20);
	}
	backend_set_slow_connect(0);	/* the one stall is already running */

	/* this call's 5 s budget runs out while the loop is held */
	rc2 = wpa_port_status();

	/* wait for the stall to end and the loop to answer again */
	for (waited = 0; waited < 12000; waited += 50) {
		if (!backend_stall_active()) {
			break;
		}
		osDelay(50);
	}
	osDelay(500);
	alive = wpa_port_status() == 0;

	snprintf(detail, sizeof(detail),
	    "rc1=%d stalled=%d rc2=%d loop_alive=%d", rc1, saw_stall, rc2,
	    alive);
	case_line("smoke.timeout",
	    rc1 == 0 && saw_stall && rc2 == -1 && alive, detail);
}

/* the fail-safe shape: no backend registered at all */
static void mode_failsafe(void) {
	int ok;
	int rc;
	const char *out;

	if (cap_begin() != 0) {
		case_line("smoke.failsafe", 0, "capture failed");
		return;
	}
	rc = wpa_port_start();
	ok = rc == 0 && cap_wait_for("no vendor backend", 5000) == 0;
	if (ok) {
		/* an operation against the missing backend is reported and the
		 * loop survives */
		wpa_port_connect("Nowhere", "password");
		osDelay(100);
		ok = cap_wait_for("no vendor backend", 3000) == 0;
	}
	ok = ok && wpa_port_status() == 0;
	out = cap_end();
	(void) out;
	case_line("smoke.failsafe", ok, "no-backend ops reported, loop alive");
}

/* ---- platform glue ------------------------------------------------------------ */

static void usage(void) {
	fprintf(stderr, "usage: wpa-port-test [--mode main|failsafe] "
	    "[--fault tamper-psk|no-scan-done]\n");
}

static void app_task(void *arg) {
	(void) arg;

	if (strcmp(mode, "failsafe") == 0) {
		mode_failsafe();
	} else {
		case_vectors();
		if (case_start()) {
			case_psk_ladder();
			case_open_round();
			case_timeout();
		}
	}

	printf(TAG "SUITE %s cases=%d\n",
	    failures == 0 ? "PASS" : "FAIL", cases_total);
	fflush(stdout);
	if (failures != 0) {
		exit(1);
	}
	cmsis2_host_end_scheduler();
}

int main(int argc, char **argv) {
	int i;
	osThreadAttr_t attr = { "wpaport", 0, NULL, 0, NULL, 8192,
				osPriorityNormal, 0, 0 };

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
			mode = argv[++i];
		} else if (strcmp(argv[i], "--fault") == 0 && i + 1 < argc) {
			fault = argv[++i];
		} else {
			usage();
			return 2;
		}
	}
	if (strcmp(mode, "main") != 0 && strcmp(mode, "failsafe") != 0) {
		usage();
		return 2;
	}
	if (fault != NULL && strcmp(fault, "tamper-psk") != 0 &&
	    strcmp(fault, "no-scan-done") != 0) {
		fprintf(stderr, "wpa-port-test: unknown fault '%s'\n", fault);
		return 2;
	}

	backend_reset();
	if (fault != NULL && strcmp(fault, "tamper-psk") == 0) {
		backend_set_psk_override("wrong-password");
	}
	if (strcmp(mode, "main") == 0) {
		backend_register();
	}
	if (fault != NULL && strcmp(fault, "no-scan-done") == 0) {
		backend_deny_scan_done();
	}
	wpa_msg_register_cb(msg_capture);

	if (osKernelInitialize() != osOK) {
		fprintf(stderr, "wpa-port-test: kernel init failed\n");
		return 2;
	}
	if (osThreadNew(app_task, NULL, &attr) == NULL) {
		fprintf(stderr, "wpa-port-test: thread create failed\n");
		return 2;
	}
	if (osKernelStart() != osOK) {
		fprintf(stderr, "wpa-port-test: kernel start failed\n");
		return 2;
	}
	return 0;
}
