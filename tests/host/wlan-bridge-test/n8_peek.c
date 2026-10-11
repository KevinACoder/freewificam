/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    n8_peek.c
 * @brief   The net80211-world half of the test harness: it answers the few
 *          questions the suite's driver-world half may ask (scan-table contents,
 *          the stack's state, the join trigger) so the two worlds meet here as
 *          scalars, exactly the way the port's seam requires.
 *
 * @design two-worlds adapter, harness half. The suite's main file lives in the
 *         linuxkpi world (it allocates socket buffers and calls the substitute);
 *         the stack it watches lives in the net80211 world. Both cannot share a
 *         translation unit - the same reason the bridge has two halves - so this
 *         file is the scalar bridge between the harness's two sides.
 * @date    08.10.2026
 */

#include <string.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_proto.h>

#include "wlan_bridge.h"
#include "n8_peek.h"

/* the bridge's ic accessor is declared in the bridge's own header (net80211
 * world); this file is the only place the suite reads through it */
void *wlan_bridge_ic(void);

int
n8_peek_scan_table(const uint8_t bssid[6], uint8_t *ssid, size_t *ssid_len,
    uint32_t *freq)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();
	struct ieee80211_node *ni;

	if (ic == NULL)
		return -1;
	/* the scan table holds the candidates the sweep found */
	TAILQ_FOREACH(ni, &ic->ic_scan.nt_node, ni_list) {
		if (memcmp(ni->ni_bssid, bssid, 6) != 0)
			continue;
		if (ssid != NULL && ssid_len != NULL) {
			size_t n = ni->ni_esslen <= *ssid_len ? ni->ni_esslen
			    : *ssid_len;

			memcpy(ssid, ni->ni_essid, n);
			*ssid_len = n;
		}
		if (freq != NULL)
			*freq = ni->ni_chan != NULL ? ni->ni_chan->ic_freq : 0;
		return 0;
	}
	return -1;
}

/* a diagnostic dump of one scan-table candidate: the facts the join decision
 * reads (capability bits, channel index, active-channel membership, rate count,
 * desired-SSID length) */
int
n8_peek_node_info(const uint8_t bssid[6], unsigned int *capinfo,
    unsigned int *chan_ieee, int *chan_active, unsigned int *rate_count,
    unsigned int *esslen)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();
	struct ieee80211_node *ni;

	if (ic == NULL)
		return -1;
	TAILQ_FOREACH(ni, &ic->ic_scan.nt_node, ni_list) {
		if (memcmp(ni->ni_bssid, bssid, 6) != 0)
			continue;
		if (capinfo != NULL)
			*capinfo = ni->ni_capinfo;
		if (chan_ieee != NULL)
			*chan_ieee = ieee80211_chan2ieee(ic, ni->ni_chan);
		if (chan_active != NULL && ni->ni_chan != NULL)
			*chan_active = isset(ic->ic_chan_active,
			    ieee80211_chan2ieee(ic, ni->ni_chan)) ? 1 : 0;
		if (rate_count != NULL)
			*rate_count = ni->ni_rates.rs_nrates;
		if (esslen != NULL)
			*esslen = ni->ni_esslen;
		return 0;
	}
	return -1;
}

/* set the interface's desired SSID (what a joiner configures before a scan):
 * the stack's match then requires the candidate to carry it */
void
n8_peek_set_ssid(const uint8_t *ssid, size_t len)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	if (ic == NULL || len > IEEE80211_NWID_LEN)
		return;
	memcpy(ic->ic_des_essid, ssid, len);
	ic->ic_des_esslen = (u_int8_t)len;
}

int
n8_peek_state(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	return ic != NULL ? (int)ic->ic_state : -1;
}

int
n8_peek_run_state(void)
{

	return IEEE80211_S_RUN;
}

int
n8_peek_scan_state(void)
{

	return IEEE80211_S_SCAN;
}

int
n8_peek_auth_state(void)
{

	return IEEE80211_S_AUTH;
}

/* the interface's own address (the bridge adopted it at attach) */
int
n8_peek_hwaddr(uint8_t addr[6])
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	if (ic == NULL)
		return -1;
	memcpy(addr, ic->ic_myaddr, 6);
	return 0;
}

/* the supplicant-visible policy state the connect path sets */
int
n8_peek_roaming(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	return ic != NULL ? (int)ic->ic_roaming : -1;
}

int
n8_peek_privacy(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	return ic != NULL && (ic->ic_flags & IEEE80211_F_PRIVACY) != 0;
}

int
n8_peek_authmode(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	return ic != NULL && ic->ic_bss != NULL ?
	    (int)ic->ic_bss->ni_authmode : -1;
}

int
n8_peek_authmode_8021x(void)
{

	return IEEE80211_AUTH_8021X;
}

/* is the slot occupied by a live cipher? (the stack-side half of one key
 * install; the driver-side half is the seam's CAM write) */
int
n8_peek_key_defined(int pairwise, int key_idx)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();
	const struct ieee80211_key *wk;

	if (ic == NULL || ic->ic_bss == NULL)
		return -1;
	if (pairwise) {
		wk = &ic->ic_bss->ni_ucastkey;
	} else {
		if (key_idx < 0 || key_idx >= IEEE80211_WEP_NKID)
			return -1;
		wk = &ic->ic_nw_keys[key_idx];
	}
	return !IEEE80211_KEY_UNDEFINED(*wk);
}

/* the default transmit key index (a group install with set_tx moves it) */
int
n8_peek_def_txkey(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	return ic != NULL ? (int)ic->ic_def_txkey : -1;
}

/* the node's authorized flag (the 802.1X port gate the pairwise key opens) */
int
n8_peek_node_authorized(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	return ic != NULL && ic->ic_bss != NULL ?
	    ieee80211_node_is_authorized(ic->ic_bss) : -1;
}

/* the number of candidates in the scan table */
int
n8_peek_scan_rows(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();
	struct ieee80211_node *ni;
	int n = 0;

	if (ic == NULL)
		return -1;
	TAILQ_FOREACH(ni, &ic->ic_scan.nt_node, ni_list)
		n++;
	return n;
}

/* the channel index the stack currently sits on - the value the beacon parser's
 * off-channel check reads (ieee80211_input.c:2118) */
int
n8_peek_curchan(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	if (ic == NULL || ic->ic_curchan == NULL)
		return -1;
	return (int)(ic->ic_curchan - ic->ic_channels);
}

/* ------------------------------------------------------------------ */
/* the CCMP round trip, net80211-world half                            */
/* ------------------------------------------------------------------ */

/*
 * Install the pairwise AES-CCM key on the joined node - what a supplicant does
 * after the 4-way handshake set the driver's key. The driver-side installation
 * is asserted separately (wlan_bridge_key_set); this is the stack-side key the
 * encap/decap decisions read.
 */
int
n8_peek_install_ccmp_key(const uint8_t *key, size_t len)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();
	struct ieee80211_key *k;

	if (ic == NULL || ic->ic_bss == NULL || len > IEEE80211_KEYBUF_SIZE)
		return -1;
	k = &ic->ic_bss->ni_ucastkey;
	ieee80211_key_update_begin(ic);
	if (!ieee80211_crypto_newkey(ic, IEEE80211_CIPHER_AES_CCM,
	    IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV, k)) {
		ieee80211_key_update_end(ic);
		return -1;
	}
	k->wk_keylen = (u_int8_t)len;
	memcpy(k->wk_key, key, len);
	if (!ieee80211_crypto_setkey(ic, k, ic->ic_bss->ni_macaddr)) {
		ieee80211_key_update_end(ic);
		return -1;
	}
	ieee80211_key_update_end(ic);
	ic->ic_flags |= IEEE80211_F_PRIVACY;
	return 0;
}

void
n8_peek_clear_privacy(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	if (ic != NULL)
		ic->ic_flags &= ~IEEE80211_F_PRIVACY;
}

/* one frame's bytes in an mbuf (the shape the injection path needs) */
static struct mbuf *
frame_mbuf(const uint8_t *frame, size_t len)
{
	struct mbuf *m;

	if (len > MHLEN)
		return NULL;
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	memcpy(mtod(m, void *), frame, len);
	m->m_len = (int)len;
	m->m_pkthdr.len = (int)len;
	return m;
}

/*
 * One AP-side frame of the exchange: built from the AP to the station (the
 * direction matters - the CCMP MIC covers the address fields, so a mirrored
 * station-direction frame would be rejected by design), marked protected and
 * sealed with the same key material through the cipher the key is bound to.
 * This is the peer's crypto, not the station's: exactly what the air would
 * carry. Shape follows the frame-test suite's peer_ccmp_frame
 * (tests/host/net80211-frame-test/main.c:547-578).
 */
int
n8_peek_seal_from_ap(const uint8_t *payload, size_t len, uint16_t seq,
    uint8_t *out, size_t *out_len)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();
	struct ieee80211_key *k;
	struct mbuf *m;
	uint8_t *p;
	size_t cap;

	if (ic == NULL || ic->ic_bss == NULL || out == NULL || out_len == NULL)
		return -1;
	cap = *out_len;
	if (24 + 8 + len + 8 > cap)
		return -1;
	k = &ic->ic_bss->ni_ucastkey;
	if (k->wk_cipher == NULL || k->wk_cipher->ic_header == 0)
		return -1;

	/* the frame as the air would carry it, before the crypto header exists */
	p = out;
	p[0] = 0x08;
	p[1] = 0x02;			/* from DS */
	p[2] = 0; p[3] = 0;		/* duration */
	memcpy(p + 4, ic->ic_myaddr, 6);		/* A1 = station */
	memcpy(p + 10, ic->ic_bss->ni_macaddr, 6);	/* A2 = AP */
	memcpy(p + 16, ic->ic_bss->ni_bssid, 6);	/* A3 = BSSID */
	p[22] = (uint8_t)(seq << 4);
	p[23] = (uint8_t)(seq >> 4);
	p[24] = 0xaa; p[25] = 0xaa; p[26] = 0x03;	/* LLC/SNAP */
	p[27] = 0; p[28] = 0; p[29] = 0;
	p[30] = 0x08; p[31] = 0x00;			/* ethertype: IP */
	memcpy(p + 32, payload, len);
	*out_len = 32 + len;

	p[1] |= IEEE80211_FC1_WEP;	/* the receive path runs the crypto */
	m = frame_mbuf(out, *out_len);
	if (m == NULL)
		return -1;
	/* the crypto header goes after the 802.11 header: make the room and
	 * move the header back to the front (the stack's own sequence) */
	M_PREPEND(m, k->wk_cipher->ic_header, M_NOWAIT);
	if (m == NULL || m->m_len < 24 + (int)k->wk_cipher->ic_header) {
		if (m != NULL)
			m_freem(m);
		return -1;
	}
	memmove(mtod(m, uint8_t *), mtod(m, uint8_t *) + k->wk_cipher->ic_header,
	    24);
	if (!k->wk_cipher->ic_encap(k, m, 0)) {
		m_freem(m);
		return -1;
	}
	*out_len = (size_t)m->m_pkthdr.len;
	if (*out_len > cap) {
		m_freem(m);
		return -1;
	}
	m_copydata(m, 0, (int)*out_len, out);
	m_freem(m);
	return 0;
}

/* the named decap failure counter the MIC case asserts on */
uint32_t
n8_peek_ccmpmic_count(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	return ic != NULL ? ic->ic_stats.is_rx_ccmpmic : 0;
}

/* Red-control back door: drop the stack back into the state end_scan leaves
 * behind (both scan flags cleared). The bridge's scan_start is what re-arms
 * them; this poke simulates that re-arm being absent so the second-sweep case
 * can prove it depends on the re-arm, not on luck. */
void
n8_poke_clear_active_scan(void)
{
	struct ieee80211com *ic = (struct ieee80211com *)wlan_bridge_ic();

	if (ic == NULL)
		return;
	ic->ic_flags &= ~(unsigned int)(IEEE80211_F_SCAN | IEEE80211_F_ASCAN);
}
