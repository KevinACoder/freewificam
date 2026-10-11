/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    ap_frames.c
 * @brief   The scripted AP's frames, byte-built from the 802.11 frame formats
 *          the same way the frame-test suite's responder builds its own, so the
 *          bridge chain's scan and join flow has something real to consume.
 *
 * @design scripted responder frames - the shapes match the sibling suite's
 *         builders (tests/host/net80211-frame-test/vwifi.c:60-161): a management
 *         header with the two-address (STA/BSSID) arrangement the frames carry,
 *         the elements the scanner reads (SSID, rates, DS parameter set) and the
 *         association response that completes the join.
 * @date    08.10.2026
 */

#include <string.h>

#include "linuxkpi_core.h"
#include <linux/ieee80211.h>

#include "ap_frames.h"

const uint8_t ap_bssid[6] = AP_BSSID_BYTES;

/* ESS plus short preamble, the capability the AP advertises */
#define AP_CAPINFO \
	(IEEE80211_CAPINFO_ESS | IEEE80211_CAPINFO_SHORT_PREAMBLE)

/* the 11b/g core rate set the AP advertises */
static const uint8_t ap_rates[] = {
	0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
};

static uint8_t *
put16(uint8_t *p, uint16_t v)
{

	*p++ = (uint8_t)(v & 0xff);
	*p++ = (uint8_t)(v >> 8);
	return p;
}

static uint8_t *
put_mac(uint8_t *p, const uint8_t mac[6])
{

	memcpy(p, mac, 6);
	return p + 6;
}

static uint8_t *
put_ie(uint8_t *p, uint8_t id, const uint8_t *body, size_t len)
{

	*p++ = id;
	*p++ = (uint8_t)len;
	memcpy(p, body, len);
	return p + len;
}

/* the 24-byte management header: AP as source and BSSID, given destination */
static uint8_t *
put_mgmt_hdr(uint8_t *p, uint8_t fc0, const uint8_t *dst)
{

	*p++ = fc0;
	*p++ = 0x00;
	p = put16(p, 0);		/* duration */
	p = put_mac(p, dst);		/* addr1 = destination */
	p = put_mac(p, ap_bssid);	/* addr2 = source = BSSID */
	p = put_mac(p, ap_bssid);	/* addr3 = BSSID */
	p = put16(p, 0);		/* sequence control */
	return p;
}

size_t
ap_build_probe_resp(uint8_t *buf, const uint8_t *dst)
{
	uint8_t *p = buf;
	uint8_t chan = 1;

	p = put_mgmt_hdr(p, IEEE80211_STYPE_PROBE_RESP, dst);
	memset(p, 0, 8);		/* timestamp */
	p += 8;
	p = put16(p, AP_BINTVAL);
	p = put16(p, AP_CAPINFO);
	p = put_ie(p, IEEE80211_ELEMID_SSID, (const uint8_t *)AP_SSID,
	    AP_SSID_LEN);
	p = put_ie(p, IEEE80211_ELEMID_RATES, ap_rates, sizeof(ap_rates));
	p = put_ie(p, IEEE80211_ELEMID_DSPARMS, &chan, 1);
	return (size_t)(p - buf);
}

size_t
ap_build_auth_resp(uint8_t *buf, const uint8_t *dst)
{
	uint8_t *p = buf;

	p = put_mgmt_hdr(p, IEEE80211_STYPE_AUTH, dst);
	p = put16(p, 0);	/* algorithm: open system */
	p = put16(p, 2);	/* sequence: second frame of the exchange */
	p = put16(p, 0);	/* status: success */
	return (size_t)(p - buf);
}

size_t
ap_build_assoc_resp(uint8_t *buf, const uint8_t *dst)
{
	uint8_t *p = buf;

	p = put_mgmt_hdr(p, IEEE80211_STYPE_ASSOC_RESP, dst);
	p = put16(p, AP_CAPINFO);
	p = put16(p, 0);	/* status: success */
	p = put16(p, AP_AID);	/* association id */
	p = put_ie(p, IEEE80211_ELEMID_RATES, ap_rates, sizeof(ap_rates));
	return (size_t)(p - buf);
}

/*
 * The AP's answer to whatever the bridge just transmitted: probe request ->
 * probe response, auth request -> auth response, assoc request -> assoc
 * response. Returns 0 when the frame needs no answer.
 */
int
ap_answer_to(const uint8_t *tx, size_t tx_len, uint8_t *out, size_t *out_len)
{
	const uint8_t *dst;

	if (tx_len < 24)
		return 0;
	dst = tx + 10;			/* addr2 of the request = the station */
	switch (tx[0] & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_PROBE_REQ:
		*out_len = ap_build_probe_resp(out, dst);
		return 1;
	case IEEE80211_STYPE_AUTH:
		*out_len = ap_build_auth_resp(out, dst);
		return 1;
	case IEEE80211_STYPE_ASSOC_REQ:
		*out_len = ap_build_assoc_resp(out, dst);
		return 1;
	default:
		return 0;
	}
}
