/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    ap_frames.h
 * @brief   The scripted AP's frame builders (SSID, BSSID, the three management
 *          answers the scan and join flow needs).
 */

#ifndef AP_FRAMES_H
#define AP_FRAMES_H

#include <stdint.h>
#include <stddef.h>

#define AP_SSID			"WlanTestAP"
#define AP_SSID_LEN		10
#define AP_BSSID_BYTES		{ 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 }
#define AP_BINTVAL		100
#define AP_AID			0x0042

extern const uint8_t ap_bssid[6];

/*
 * The frame-format vocabulary this suite's builders use that the driver-visible
 * face does not carry (the driver never reads these, so the face has no reason
 * to declare them). Values are 802.11-2020 clause 9.3.1.4 (subtypes) and
 * clause 9.4.2 (element ids) / 9.4.1.4 (capability bits).
 */
#ifndef IEEE80211_FCTL_STYPE
#define	IEEE80211_FCTL_STYPE		0x00f0
#endif
#ifndef IEEE80211_STYPE_ASSOC_RESP
#define	IEEE80211_STYPE_ASSOC_RESP	0x0010
#endif
#ifndef IEEE80211_STYPE_PROBE_RESP
#define	IEEE80211_STYPE_PROBE_RESP	0x0050
#endif
#ifndef IEEE80211_ELEMID_SSID
#define	IEEE80211_ELEMID_SSID		0
#endif
#ifndef IEEE80211_ELEMID_RATES
#define	IEEE80211_ELEMID_RATES		1
#endif
#ifndef IEEE80211_ELEMID_DSPARMS
#define	IEEE80211_ELEMID_DSPARMS	3
#endif
#ifndef IEEE80211_CAPINFO_ESS
#define	IEEE80211_CAPINFO_ESS		0x0001
#endif
#ifndef IEEE80211_CAPINFO_SHORT_PREAMBLE
#define	IEEE80211_CAPINFO_SHORT_PREAMBLE	0x0020
#endif

size_t	ap_build_probe_resp(uint8_t *buf, const uint8_t *dst);
size_t	ap_build_auth_resp(uint8_t *buf, const uint8_t *dst);
size_t	ap_build_assoc_resp(uint8_t *buf, const uint8_t *dst);

/* answer-to-a-transmitted-frame dispatcher; 0 when the frame needs none */
int	ap_answer_to(const uint8_t *tx, size_t tx_len, uint8_t *out,
	    size_t *out_len);

#endif /* AP_FRAMES_H */
