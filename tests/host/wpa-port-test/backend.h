/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    backend.h
 * @brief   The scripted vendor backend for the wpa port suite: it plays the
 *          NIC side of a WPA2-PSK connection (survey, MLME connect, key
 *          installation, EAPOL carriage) and the AP side of the 4-way
 *          handshake, so the real supplicant can be driven to COMPLETED on
 *          the host.
 *
 * @design scripted peer (suite-level harness, not product code): the peer's
 *         behaviour is the standard procedure - it constructs M1/M3 from the
 *         clause-derived frame layout and verifies the supplicant's M2/M4 -
 *         while the expected values it checks against are hand-derived
 *         vectors and independently computed key material (never the
 *         implementation's own output). The vendor contract it implements is
 *         ports/wpa_supplicant/wpa_vendor_port.h.
 *
 * @date    09.10.2026
 */

#ifndef WPA_PORT_TEST_BACKEND_H
#define WPA_PORT_TEST_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "wpa_vendor_port.h"

/* failures the backend's own checks can report; each names a case */
void backend_reset(void);

/* register the scripted backend with the port layer */
void backend_register(void);

/* the recorded conversation */
int backend_scan_start_calls(void);
int backend_scan_fetch_calls(void);
const uint8_t *backend_connect_ssid(size_t *len);
const uint8_t *backend_connect_wpa_ie(size_t *len);
int backend_connect_bssid_set(void);
const uint8_t *backend_connect_bssid(void);
int backend_eapol_tx_count(void);
int backend_disconnect_calls(void);
void backend_deny_scan_done(void);

/* the two keys the supplicant installed; -1 when absent */
int backend_key_count(void);
int backend_key_group_index(int n);	/* 1 = group key (multicast addr) */
int backend_key_alg(int n);
const uint8_t *backend_key_bytes(int n, size_t *len);
int backend_key_idx(int n);
int backend_key_set_tx(int n);

/* the AP side: the peer answers M2 with M3 itself and checks M4's MIC.
 * These report how far the exchange got. */
int backend_ap_sent_m3(void);
int backend_ap_expect_m4(void);

/* the key material the AP side derived (for cross-checks in the suite) */
const uint8_t *backend_ap_tk(size_t *len);
const uint8_t *backend_ap_gtk(size_t *len);
const uint8_t *backend_ap_kck(size_t *len);

/* inject port events from the test's thread (the contract says the pump
 * runs in a driver worker context and may only queue) */
void backend_emit_scan_done(void);
void backend_emit_eapol(const uint8_t *buf, size_t len);
void backend_inject_bad_m1(void);

/* the scenario knobs (fault injection for the red controls) */
void backend_set_psk_override(const char *wrong_passphrase);
void backend_set_slow_connect(int enable);
int backend_stall_active(void);

#endif /* WPA_PORT_TEST_BACKEND_H */
