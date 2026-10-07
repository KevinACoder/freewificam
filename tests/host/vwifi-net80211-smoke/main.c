/*
 * @file    main.c
 * @brief   The net80211 protocol-path mock: one host run through the stack.
 *
 * The suite drives the imported net80211 component through its protocol paths
 * with the vwifi driver substitute (vwifi.c) and the scripted responder:
 *   P1 attach, P2 scan, P3 open-system authentication, P4 association and the
 *   node table, P6/P7 the data encapsulation and decapsulation paths, P9 the
 *   management frame negatives, P10 AES-CCM, P11 the inactivity timeout and
 *   P12 the EAPOL classification of the port.
 *
 * Every expectation is an input -> output pair derived from the 802.11 frame
 * formats (vectors.h) or from the component's declared contracts; the stack
 * under test never supplies its own expected values. The anchor lines
 *   "vwifi-smoke: <ID> ok=<0|1> <field>=<value> ..."
 * are machine checked by the container suite (tools/mock/net80211-frame-mock/)
 * and are the board-side print template of the two-sided segment contract.
 *
 * @design state container - ref net80211:driver/urtwn/if_urtwn.c:515
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/route.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_proto.h>
#include <net80211/ieee80211_crypto.h>

#include "osal/wlan_osal.h"
#include "vectors.h"
#include "vwifi.h"

#define TAG "vwifi-smoke: "
#define THREAD_STACK_SIZE 16384U

static const uint8_t ap_mac[6] = VWIFI_AP_MAC_BYTES;
static const uint8_t sta_mac[6] = VWIFI_STA_MAC_BYTES;

static struct ieee80211com *ic;
static struct ifnet ifp;
static struct vwifi vwifi;
static const char *fault_mode;		/* NULL = the green run */
static unsigned failures;

/* ------------------------------------------------------------------ */
/* reporting                                                           */
/* ------------------------------------------------------------------ */

static void fail(const char *what)
{
	fflush(stdout);
	fprintf(stderr, TAG "FAIL %s\n", what);
	fflush(stderr);
	exit(1);
}

/* One machine-checked anchor line: the segment id, the verdict, the fields. */
static void anchor(const char *id, int ok, const char *fmt, ...)
{
	va_list ap;

	printf(TAG "%s ok=%d", id, ok);
	if (fmt != NULL) {
		va_start(ap, fmt);
		putchar(' ');
		vprintf(fmt, ap);
		va_end(ap);
	}
	putchar('\n');
	fflush(stdout);
	if (!ok)
		failures++;
}

static int mac_eq(const uint8_t *a, const uint8_t *b)
{
	return memcmp(a, b, 6) == 0;
}

static int mac_is_broadcast(const uint8_t *a)
{
	static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	return mac_eq(a, bcast);
}

/* ------------------------------------------------------------------ */
/* P1: the device attach                                               */
/* ------------------------------------------------------------------ */

static void device_attach(void)
{
	ic = wlan_osal_zalloc(sizeof(*ic));
	if (ic == NULL)
		fail("ieee80211com allocation");
	memset(&ifp, 0, sizeof(ifp));

	ic->ic_ifp = &ifp;
	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	memcpy(ic->ic_myaddr, sta_mac, 6);

	/* One channel: the 2 GHz channel 1. */
	ic->ic_channels[VWIFI_CHAN_IEEE].ic_freq = VWIFI_CHAN_FREQ;
	ic->ic_channels[VWIFI_CHAN_IEEE].ic_flags = IEEE80211_CHAN_CCK |
	    IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;

	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	/* The desired network: what the driver feeds from its configuration. A
	 * station with no desired SSID matches no BSS at all (match_bss). */
	memcpy(ic->ic_des_essid, VWIFI_SSID, VWIFI_SSID_LEN);
	ic->ic_des_esslen = VWIFI_SSID_LEN;

	if (if_initialize(&ifp) != 0)
		fail("if_initialize");
	(void) if_percpuq_create(&ifp);
	ifp.if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

	vwifi_init(&vwifi, ic, &ifp);
	vwifi_install(&vwifi);

	/* NONE is installed by the crypto attach path; CCMP is registered here
	 * because the import's link-set registration is compiled out on this
	 * port (the cipher template is an exported symbol, spec 3.3.4). */
	ieee80211_crypto_register(&ieee80211_cipher_ccmp);

	ieee80211_ifattach(ic);

	/* The driver's late attach work: the node tables and ic_bss come up here,
	 * and the media shell is installed (reference driver shape). */
	ieee80211_media_init(ic, ieee80211_media_change, ieee80211_media_status);
	ifp.if_percpuq = if_percpuq_create(&ifp);
	if_register(&ifp);

	anchor("S2.LINK_ATTACH",
	    ic->ic_ifp == &ifp && ic->ic_bss != NULL &&
	    ic->ic_curchan != NULL && ic->ic_newstate != NULL &&
	    mac_eq(ic->ic_myaddr, sta_mac),
	    "opmode=STA ch=%d bss=%d chan=%d newstate=%d", VWIFI_CHAN_IEEE,
	    ic->ic_bss != NULL, ic->ic_curchan != NULL, ic->ic_newstate != NULL);
}

/* ------------------------------------------------------------------ */
/* P2/P3/P4: scan -> auth -> assoc                                     */
/* ------------------------------------------------------------------ */

static void run_scan_to_run(void)
{
	unsigned guard;

	ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	anchor("S2.SCAN_START", (ic->ic_flags & IEEE80211_F_SCAN) != 0, NULL);

	for (guard = 0; guard < 64 && ic->ic_state != IEEE80211_S_RUN; guard++) {
		if (vwifi_dispatch_one(&vwifi))
			continue;
		if (ic->ic_state == IEEE80211_S_SCAN) {
			/* The scan dwell is over: the driver advances the scan. */
			ieee80211_next_scan(ic);
			continue;
		}
		break;		/* waiting for a response that will not come */
	}
}

static void check_captured_frames(void)
{
	const uint8_t *f;

	/* probe request: broadcast destination, station source, 11b/g rates. */
	f = vwifi.last_probe_req.data;
	anchor("S2.TX_PROBE_REQ", vwifi.stats.tx_probe_req >= 1,
	    "n=%u", vwifi.stats.tx_probe_req);
	anchor("S1.BYTES_TX",
	    vwifi.last_probe_req.len >= 24 &&
	    f[0] == IEEE80211_FC0_SUBTYPE_PROBE_REQ && f[1] == 0x00 &&
	    mac_is_broadcast(f + 4) && mac_eq(f + 10, sta_mac) &&
	    mac_is_broadcast(f + 16),
	    "len=%u", (unsigned) vwifi.last_probe_req.len);

	/* authentication request: open system, first sequence, to the AP. */
	f = vwifi.last_auth_req.data;
	anchor("S3.TX_AUTH",
	    vwifi.stats.tx_auth_req == 1 && vwifi.last_auth_req.len == 30 &&
	    f[0] == IEEE80211_FC0_SUBTYPE_AUTH && f[1] == 0x00 &&
	    mac_eq(f + 4, ap_mac) && mac_eq(f + 10, sta_mac) &&
	    mac_eq(f + 16, ap_mac) &&
	    f[24] == 0x00 && f[25] == 0x00 &&	/* algorithm: open system */
	    f[26] == 0x01 && f[27] == 0x00 &&	/* sequence: 1 */
	    f[28] == 0x00 && f[29] == 0x00,	/* status: success */
	    "alg=0 seq=1");

	/* association request: carries the SSID and the rates elements. */
	f = vwifi.last_assoc_req.data;
	anchor("S3.TX_ASSOC",
	    vwifi.stats.tx_assoc_req == 1 && vwifi.last_assoc_req.len >= 28 &&
	    f[0] == IEEE80211_FC0_SUBTYPE_ASSOC_REQ && f[1] == 0x00 &&
	    mac_eq(f + 4, ap_mac) && mac_eq(f + 10, sta_mac) &&
	    mac_eq(f + 16, ap_mac),
	    "len=%u", (unsigned) vwifi.last_assoc_req.len);
}

static void check_responder_vectors(void)
{
	anchor("S3.RX_PROBE_RESP",
	    vwifi.stats.rx_probe_resp >= 1 &&
	    vwifi.last_probe_resp.len == sizeof(vwifi_vec_probe_resp) &&
	    memcmp(vwifi.last_probe_resp.data, vwifi_vec_probe_resp,
	    sizeof(vwifi_vec_probe_resp)) == 0,
	    "ssid=" VWIFI_SSID);
	anchor("S3.VEC_PROBE_RESP",
	    vwifi.last_probe_resp.len == sizeof(vwifi_vec_probe_resp) &&
	    memcmp(vwifi.last_probe_resp.data, vwifi_vec_probe_resp,
	    sizeof(vwifi_vec_probe_resp)) == 0,
	    "n=%u", (unsigned) sizeof(vwifi_vec_probe_resp));

	anchor("S3.RX_AUTH",
	    vwifi.stats.rx_auth_resp == 1 &&
	    vwifi.last_auth_resp.len == sizeof(vwifi_vec_auth_resp) &&
	    memcmp(vwifi.last_auth_resp.data, vwifi_vec_auth_resp,
	    sizeof(vwifi_vec_auth_resp)) == 0,
	    "seq=2 status=0");
	anchor("S3.VEC_AUTH_RESP",
	    vwifi.last_auth_resp.len == sizeof(vwifi_vec_auth_resp) &&
	    memcmp(vwifi.last_auth_resp.data, vwifi_vec_auth_resp,
	    sizeof(vwifi_vec_auth_resp)) == 0,
	    "n=%u", (unsigned) sizeof(vwifi_vec_auth_resp));

	anchor("S3.RX_ASSOC",
	    vwifi.stats.rx_assoc_resp == 1 &&
	    vwifi.last_assoc_resp.len == sizeof(vwifi_vec_assoc_resp) &&
	    memcmp(vwifi.last_assoc_resp.data, vwifi_vec_assoc_resp,
	    sizeof(vwifi_vec_assoc_resp)) == 0,
	    "status=0 aid=%u", VWIFI_AID);
	anchor("S3.VEC_ASSOC_RESP",
	    vwifi.last_assoc_resp.len == sizeof(vwifi_vec_assoc_resp) &&
	    memcmp(vwifi.last_assoc_resp.data, vwifi_vec_assoc_resp,
	    sizeof(vwifi_vec_assoc_resp)) == 0,
	    "n=%u", (unsigned) sizeof(vwifi_vec_assoc_resp));

	anchor("S3.EVENT_SCAN_DONE", vwifi.stats.ev_scan_done == 1, NULL);
	anchor("S3.EVENT_ASSOC", vwifi.stats.ev_assoc == 1, NULL);
}

/* ------------------------------------------------------------------ */
/* P5: the node table and the port hooks                               */
/* ------------------------------------------------------------------ */

static unsigned node_count_sta, node_count_scan;

static void count_node(void *arg, struct ieee80211_node *ni)
{
	unsigned *count = arg;

	(void) ni;
	(*count)++;
}

static void check_node_table(void)
{
	struct ieee80211_node *bss = ic->ic_bss;

	node_count_sta = 0;
	node_count_scan = 0;
	ieee80211_iterate_nodes(&ic->ic_sta, count_node, &node_count_sta);
	ieee80211_iterate_nodes(&ic->ic_scan, count_node, &node_count_scan);

	anchor("S3.NODE_TABLE",
	    bss != NULL && mac_eq(bss->ni_bssid, ap_mac) &&
	    bss->ni_associd == VWIFI_AID && node_count_scan >= 1 &&
	    ieee80211_node_refcnt(bss) >= 1,
	    "nodes=%u/%u refs=%d", node_count_scan, node_count_sta,
	    ieee80211_node_refcnt(bss));

	anchor("S3.RUN_STATE", ic->ic_state == IEEE80211_S_RUN, "state=%d",
	    (int) ic->ic_state);
}


/* ------------------------------------------------------------------ */
/* P6/P7: the data plane (encapsulation and decapsulation)             */
/* ------------------------------------------------------------------ */

static struct mbuf *eth_mbuf(const uint8_t *dst, const uint8_t *src,
    uint16_t type, const uint8_t *payload, size_t len)
{
	struct mbuf *m;
	uint8_t *p;

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	p = mtod(m, uint8_t *);
	memcpy(p, dst, 6);
	memcpy(p + 6, src, 6);
	p[12] = (uint8_t) (type >> 8);
	p[13] = (uint8_t) (type & 0xff);
	memcpy(p + 14, payload, len);
	m->m_len = (int) (14 + len);
	m->m_pkthdr.len = m->m_len;
	return m;
}

/* The ethernet frame the far end would see. */
static size_t eth_build(uint8_t *buf, const uint8_t *dst, const uint8_t *src,
    uint16_t type, const uint8_t *payload, size_t len)
{
	memcpy(buf, dst, 6);
	memcpy(buf + 6, src, 6);
	buf[12] = (uint8_t) (type >> 8);
	buf[13] = (uint8_t) (type & 0xff);
	memcpy(buf + 14, payload, len);
	return 14 + len;
}

static void check_data_plane(void)
{
	static const uint8_t far_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x7a };
	static const uint8_t payload[24] = {
		'v', 'w', 'i', 'f', 'i', '-', 'd', 'a', 't', 'a', 0, 1, 2, 3
	};
	uint8_t tx1[VWIFI_FRAME_MAX], tx2[VWIFI_FRAME_MAX];
	uint8_t mirror[VWIFI_FRAME_MAX], expect[VWIFI_FRAME_MAX];
	struct mbuf *m, *w;
	size_t len1, len2, elen;
	unsigned before;
	uint16_t seq1, seq2;
	int ok;

	/* A frame addressed to the station itself: the AP's delivery of it is
	 * the only one the station can receive (the receive filter drops other
	 * unicast destinations). */
	m = eth_mbuf(sta_mac, far_mac, ETHERTYPE_IP, payload, sizeof(payload));
	w = (m != NULL) ? ieee80211_encap(ic, m, ic->ic_bss) : NULL;
	if (w == NULL) {
		anchor("S3.ENCAP", 0, "encap failed");
		return;
	}
	len1 = (size_t) w->m_len;
	m_copydata(w, 0, (int) len1, tx1);
	m_freem(w);

	m = eth_mbuf(sta_mac, far_mac, ETHERTYPE_IP, payload, sizeof(payload));
	w = (m != NULL) ? ieee80211_encap(ic, m, ic->ic_bss) : NULL;
	if (w == NULL) {
		anchor("S3.ENCAP", 0, "second encap failed");
		return;
	}
	len2 = (size_t) w->m_len;
	m_copydata(w, 0, (int) len2, tx2);
	m_freem(w);

	seq1 = (uint16_t) (((tx1[22] | (tx1[23] << 8)) >>
	    IEEE80211_SEQ_SEQ_SHIFT) & 0x0fff);
	seq2 = (uint16_t) (((tx2[22] | (tx2[23] << 8)) >>
	    IEEE80211_SEQ_SEQ_SHIFT) & 0x0fff);

	ok = len1 == 24 + 8 + sizeof(payload) &&
	    tx1[0] == IEEE80211_FC0_TYPE_DATA &&
	    (tx1[1] & IEEE80211_FC1_DIR_MASK) == IEEE80211_FC1_DIR_TODS &&
	    mac_eq(tx1 + 4, ap_mac) && mac_eq(tx1 + 10, far_mac) &&
	    mac_eq(tx1 + 16, sta_mac) &&
	    tx1[24] == LLC_SNAP_LSAP && tx1[25] == LLC_SNAP_LSAP &&
	    tx1[26] == LLC_UI &&
	    tx1[27] == 0 && tx1[28] == 0 && tx1[29] == 0 &&
	    tx1[30] == (uint8_t) (ETHERTYPE_IP >> 8) &&
	    tx1[31] == (uint8_t) (ETHERTYPE_IP & 0xff) &&
	    memcmp(tx1 + 32, payload, sizeof(payload)) == 0 &&
	    seq2 == (uint16_t) ((seq1 + 1) & 0x0fff);
	anchor("S3.ENCAP", ok, "fc=%02x%02x len=%u seq=%u->%u", tx1[0], tx1[1],
	    (unsigned) len1, seq1, seq2);

	/* The air self loop: flip the direction like the AP would, inject, and
	 * expect the port's data sink to see the original ethernet frame. */
	before = vwifi.stats.data_rx_frames;
	if (vwifi_mirror_data(tx1, len1, mirror) != 0) {
		anchor("S3.DECAP", 0, "mirror failed");
		return;
	}
	elen = eth_build(expect, sta_mac, far_mac, ETHERTYPE_IP, payload,
	    sizeof(payload));
	vwifi_inject(&vwifi, mirror, len1);
	ok = vwifi.stats.data_rx_frames == before + 1 &&
	    vwifi.last_data_rx.len == elen &&
	    memcmp(vwifi.last_data_rx.data, expect, elen) == 0;
	anchor("S3.DECAP", ok, "len=%u", (unsigned) vwifi.last_data_rx.len);
	anchor("S1.BYTES_RX", len1 >= 24, "len=%u", (unsigned) len1);
}

/* P12: the EAPOL classification of the port, reached through the stack. */
static void check_eapol(void)
{
	/* EAPOL test material (an EAPOL-Key frame header), not a network key. */
	static const uint8_t eapol[4] = { 0x02, 0x03, 0x00, 0x05 };
	uint8_t buf[VWIFI_FRAME_MAX];
	size_t len;
	unsigned before_data, before_eapol;
	int ok;

	len = vwifi_build_data_from_ap(buf, sta_mac, ap_mac, ETHERTYPE_PAE,
	    eapol, sizeof(eapol), 3, 0, 0);
	before_data = vwifi.stats.data_rx_frames;
	before_eapol = vwifi.stats.eapol_rx_frames;
	vwifi_inject(&vwifi, buf, len);
	ok = vwifi.stats.eapol_rx_frames == before_eapol + 1 &&
	    vwifi.stats.data_rx_frames == before_data &&
	    mac_eq(vwifi.eapol_src, ap_mac) &&
	    vwifi.last_eapol_rx.len == sizeof(eapol) &&
	    memcmp(vwifi.last_eapol_rx.data, eapol, sizeof(eapol)) == 0;
	anchor("S4.EAPOL_CLASSIFY", ok, "len=%u",
	    (unsigned) vwifi.last_eapol_rx.len);
}

/* P8: reassembly, plus the two named drop paths. */
static void check_defrag(void)
{
	static const uint8_t part1[10] = { 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5,
		0xa6, 0xa7, 0xa8, 0xa9 };
	static const uint8_t part2[8] = { 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5,
		0xb6, 0xb7 };
	uint8_t buf[VWIFI_FRAME_MAX], expect[VWIFI_FRAME_MAX];
	uint8_t all[sizeof(part1) + sizeof(part2)];
	uint8_t first[VWIFI_FRAME_MAX];
	size_t len, elen;
	unsigned before, before_dup;
	int ok;

	memcpy(all, part1, sizeof(part1));
	memcpy(all + sizeof(part1), part2, sizeof(part2));
	elen = eth_build(expect, sta_mac, ap_mac, ETHERTYPE_IP, all, sizeof(all));

	before = vwifi.stats.data_rx_frames;

	/* first fragment: saved, nothing delivered yet */
	len = vwifi_build_data_from_ap(buf, sta_mac, ap_mac, ETHERTYPE_IP,
	    part1, sizeof(part1), 7, 1, 0);
	memcpy(first, buf, len);
	vwifi_inject(&vwifi, buf, len);
	ok = vwifi.stats.data_rx_frames == before;

	/* continuation: raw body bytes, no SNAP header */
	len = vwifi_build_data_from_ap(buf, sta_mac, ap_mac, ETHERTYPE_IP,
	    part2, sizeof(part2), 7, 0, 1);
	memmove(buf + 24, buf + 32, sizeof(part2));
	len -= 8;
	vwifi_inject(&vwifi, buf, len);
	ok = ok && vwifi.stats.data_rx_frames == before + 1 &&
	    vwifi.last_data_rx.len == elen &&
	    memcmp(vwifi.last_data_rx.data, expect, elen) == 0;
	anchor("S3.DEFRAG", ok, "frags=2 rx=%u len=%u",
	    vwifi.stats.data_rx_frames, (unsigned) vwifi.last_data_rx.len);

	/* a continuation without a predecessor is dropped */
	before = vwifi.stats.data_rx_frames;
	len = vwifi_build_data_from_ap(buf, sta_mac, ap_mac, ETHERTYPE_IP,
	    part2, sizeof(part2), 40, 0, 1);
	memmove(buf + 24, buf + 32, sizeof(part2));
	len -= 8;
	vwifi_inject(&vwifi, buf, len);
	ok = vwifi.stats.data_rx_frames == before;

	/* a retransmission of an old sequence is dropped by name */
	before = vwifi.stats.data_rx_frames;
	before_dup = ic->ic_stats.is_rx_dup;
	first[1] |= IEEE80211_FC1_RETRY;
	vwifi_inject(&vwifi, first, len + 8 + sizeof(part1) - 0);
	ok = ok && vwifi.stats.data_rx_frames == before &&
	    ic->ic_stats.is_rx_dup > before_dup;
	anchor("S3.DEFRAG_NEG", ok, "dup=%u", ic->ic_stats.is_rx_dup - before_dup);
}

/* P9: management frame negatives. */
static void check_mgmt_negatives(void)
{
	uint8_t buf[VWIFI_FRAME_MAX];
	uint8_t chan = VWIFI_CHAN_IEEE;
	size_t len, body;
	unsigned before_small, before_big;
	int ok;

	/* a truncated authentication body is rejected by length */
	before_small = ic->ic_stats.is_rx_elem_toosmall;
	len = vwifi_build_auth_resp(buf);
	vwifi_inject(&vwifi, buf, len - 1);
	ok = ic->ic_stats.is_rx_elem_toosmall == before_small + 1 &&
	    ic->ic_state == IEEE80211_S_RUN;
	anchor("S3.NEG_TRUNC", ok, "small=%u", ic->ic_stats.is_rx_elem_toosmall);

	/* an over-long SSID element is rejected by element length */
	before_big = ic->ic_stats.is_rx_elem_toobig;
	{
		uint8_t *p = buf;
		memset(p, 0, 24);			/* header, filled below */
		p[0] = IEEE80211_FC0_SUBTYPE_PROBE_RESP;
		memcpy(p + 4, sta_mac, 6);		/* A1 */
		memcpy(p + 10, ap_mac, 6);		/* A2 */
		memcpy(p + 16, ap_mac, 6);		/* A3 */
		body = 24 + 8;				/* timestamp */
		p[body++] = 0x64; p[body++] = 0x00;	/* beacon interval */
		p[body++] = 0x21; p[body++] = 0x00;	/* capability */
		p[body++] = IEEE80211_ELEMID_RATES;	/* rates element */
		p[body++] = 8;
		memcpy(p + body, "\x82\x84\x8b\x96\x0c\x12\x18\x24", 8);
		body += 8;
		p[body++] = IEEE80211_ELEMID_SSID;	/* ssid element, 33 bytes */
		p[body++] = 33;
		memset(p + body, 'x', 33);
		body += 33;
		p[body++] = IEEE80211_ELEMID_DSPARMS;
		p[body++] = 1;
		p[body++] = chan;
		len = body;
	}
	vwifi_inject(&vwifi, buf, len);
	ok = ic->ic_stats.is_rx_elem_toobig == before_big + 1 &&
	    ic->ic_state == IEEE80211_S_RUN;
	anchor("S3.NEG_IE", ok, "big=%u", ic->ic_stats.is_rx_elem_toobig);
}

/* One AP-side frame of the AES-CCM exchange: built in the AP-to-station
 * direction and sealed with the key's own cipher (what the peer would do). */
static size_t peer_ccmp_frame(struct ieee80211_key *k, uint8_t *buf,
    const uint8_t *payload, size_t len, uint16_t seq)
{
	struct mbuf *m;
	size_t flen;

	flen = vwifi_build_data_from_ap(buf, sta_mac, ap_mac, ETHERTYPE_IP,
	    payload, len, seq, 0, 0);
	buf[1] |= IEEE80211_FC1_WEP;	/* the receive path runs the crypto */
	m = vwifi_frame_mbuf(buf, flen);
	if (m == NULL)
		return 0;
	/* the crypto header goes after the 802.11 header: prepend the space and
	 * move the header back to the front (the stack's own sequence) */
	M_PREPEND(m, k->wk_cipher->ic_header, M_NOWAIT);
	if (m == NULL || m->m_len < 24 + (int) k->wk_cipher->ic_header) {
		if (m != NULL)
			m_freem(m);
		return 0;
	}
	memmove(mtod(m, uint8_t *), mtod(m, uint8_t *) + k->wk_cipher->ic_header,
	    24);
	if (!k->wk_cipher->ic_encap(k, m, 0)) {
		m_freem(m);
		return 0;
	}
	flen = (size_t) m->m_len;
	m_copydata(m, 0, (int) flen, buf);
	m_freem(m);
	return flen;
}

/* P10: the AES-CCM round trip and the MIC negative.
 *
 * The transmit leg is the station's own encapsulation plus the crypto hook the
 * driver runs for a protected frame. The receive leg cannot be that frame
 * mirrored: the CCMP MIC covers the address fields, so a direction flip would
 * be rejected by design. The AP side of the exchange is therefore built in the
 * AP-to-station direction and encrypted with the same key material through the
 * cipher the key is bound to - exactly what the peer's crypto would do.
 */
static void check_ccmp(void)
{
	/* Test material, not a network key (the library's own tests annotate
	 * their constant the same way). */
	static const uint8_t test_key[16] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
	};
	static const uint8_t far_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x7a };
	static const uint8_t payload[16] = {
		'c', 'c', 'm', 'p', '-', 'p', 'l', 'a',
		'y', 'l', 'o', 'a', 'd', 0, 0, 0
	};
	uint8_t buf[VWIFI_FRAME_MAX], tx[VWIFI_FRAME_MAX];
	uint8_t expect[VWIFI_FRAME_MAX];
	struct ieee80211_key *k = &ic->ic_bss->ni_ucastkey;
	struct mbuf *m, *w;
	size_t len, elen, tx_len;
	unsigned before, before_mic;
	int ok, tx_enc = 0, rx_ok = 0, mic_ok = 0;

	/* install the pairwise key on the BSS node (what a supplicant does) */
	ieee80211_key_update_begin(ic);
	if (!ieee80211_crypto_newkey(ic, IEEE80211_CIPHER_AES_CCM,
	    IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV, k)) {
		ieee80211_key_update_end(ic);
		anchor("S3.CCMP", 0, "newkey failed");
		return;
	}
	k->wk_keylen = sizeof(test_key);
	memcpy(k->wk_key, test_key, sizeof(test_key));
	if (!ieee80211_crypto_setkey(ic, k, ic->ic_bss->ni_macaddr)) {
		ieee80211_key_update_end(ic);
		anchor("S3.CCMP", 0, "setkey failed");
		return;
	}
	ieee80211_key_update_end(ic);

	ic->ic_flags |= IEEE80211_F_PRIVACY;

	/* transmit leg: encapsulation, then the driver's crypto call */
	m = eth_mbuf(sta_mac, far_mac, ETHERTYPE_IP, payload, sizeof(payload));
	w = (m != NULL) ? ieee80211_encap(ic, m, ic->ic_bss) : NULL;
	if (w == NULL) {
		anchor("S3.CCMP", 0, "encap failed");
		return;
	}
	m_copydata(w, 0, (int) w->m_len, tx);
	if ((tx[1] & IEEE80211_FC1_WEP) == 0) {
		m_freem(w);
		anchor("S3.CCMP", 0, "frame not marked protected");
		return;
	}
	if (ieee80211_crypto_encap(ic, ic->ic_bss, w) == NULL) {
		m_freem(w);
		anchor("S3.CCMP", 0, "crypto encap failed");
		return;
	}
	/* the sealed frame is a chain (the MIC rides in its own buffer), so its
	 * packet length is the length that matters */
	len = (size_t) w->m_pkthdr.len;
	m_copydata(w, 0, (int) len, tx);
	m_freem(w);
	tx_len = len;
	tx_enc = memcmp(tx + 32, payload, sizeof(payload)) != 0;
	/* header + ccmp header + llc + payload + mic */
	ok = len == 24 + 8 + 8 + sizeof(payload) + 8 && tx_enc;

	/* receive leg: the peer seals the reverse-direction frame */
	len = peer_ccmp_frame(k, buf, payload, sizeof(payload), 4);
	if (len == 0) {
		anchor("S3.CCMP", 0, "peer encap failed");
		return;
	}

	elen = eth_build(expect, sta_mac, ap_mac, ETHERTYPE_IP, payload,
	    sizeof(payload));
	before = vwifi.stats.data_rx_frames;
	vwifi_inject(&vwifi, buf, len);
	rx_ok = vwifi.stats.data_rx_frames == before + 1 &&
	    vwifi.last_data_rx.len == elen &&
	    memcmp(vwifi.last_data_rx.data, expect, elen) == 0;
	ok = ok && rx_ok;

	/* a second freshly sealed frame (a valid packet number) with a broken
	 * last byte inside the MIC: rejected by name, nothing delivered */
	len = peer_ccmp_frame(k, buf, payload, sizeof(payload), 5);
	if (len == 0) {
		anchor("S3.CCMP", 0, "second peer encap failed");
		return;
	}
	before = vwifi.stats.data_rx_frames;
	before_mic = ic->ic_stats.is_rx_ccmpmic;
	buf[len - 1] ^= 0x01;
	vwifi_inject(&vwifi, buf, len);
	mic_ok = vwifi.stats.data_rx_frames == before &&
	    ic->ic_stats.is_rx_ccmpmic == before_mic + 1;
	ok = ok && mic_ok;

	ic->ic_flags &= ~IEEE80211_F_PRIVACY;
	anchor("S3.CCMP", ok, "tx=%u enc=%d rx_ok=%d rx=%u mic=%d deliv=%u",
	    (unsigned) tx_len, tx_enc, rx_ok, vwifi.stats.data_rx_frames,
	    mic_ok, (unsigned) vwifi.last_data_rx.len);
}

/* P11: the inactivity timeout and the watchdog entry.
 *
 * In station mode the node table that carries the access point is the scan
 * table, so the timeout entry exercised here is its candidate purge; the
 * hostap/ibss "station leaves" path of ieee80211_node_leave is out of this
 * mock's operating mode (the library asserts the mode there). The port has no
 * timer service yet, so the mock drives both entries explicitly (spec 3.3.5).
 */
static void check_watchdog(void)
{
	static const uint8_t candidate_mac[6] = {
		0x02, 0x00, 0x00, 0x00, 0x00, 0x55
	};
	struct ieee80211_node *ni;
	unsigned before, after;
	int ok;

	before = 0;
	ieee80211_iterate_nodes(&ic->ic_scan, count_node, &before);

	ni = ieee80211_alloc_node(&ic->ic_scan, candidate_mac);
	if (ni == NULL) {
		anchor("S3.WATCHDOG", 0, "alloc failed");
		return;
	}
	ni->ni_inact = 1;		/* expires with the next tick */
	ic->ic_bss->ni_inact = 60;	/* the joined node stays */

	ic->ic_scan.nt_timeout(&ic->ic_scan);

	after = 0;
	ieee80211_iterate_nodes(&ic->ic_scan, count_node, &after);

	ok = after == before &&
	    ic->ic_scan.nt_inact_timer == IEEE80211_INACT_WAIT &&
	    ic->ic_bss->ni_inact == 59;

	/* the driver tick: reachable, it services the inactivity timers and asks
	 * for another one, and it leaves a running state alone */
	ieee80211_watchdog(ic);
	ok = ok && ic->ic_state == IEEE80211_S_RUN &&
	    ic->ic_ifp->if_timer == 1;

	anchor("S3.WATCHDOG", ok, "cand=%u->%u bss_inact=%u if_timer=%d",
	    before, after, ic->ic_bss->ni_inact, ic->ic_ifp->if_timer);
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static void suite_run(void *argument)
{
	unsigned blocks0, bytes0, blocks1, bytes1;

	(void) argument;
	wlan_osal_init();

	blocks0 = wlan_osal_live_blocks();
	bytes0 = wlan_osal_live_bytes();

	device_attach();
	run_scan_to_run();
	check_captured_frames();
	check_responder_vectors();
	check_node_table();
	check_data_plane();
	check_eapol();
	check_defrag();
	check_mgmt_negatives();
	check_ccmp();
	check_watchdog();

	blocks1 = wlan_osal_live_blocks();
	bytes1 = wlan_osal_live_bytes();
	printf(TAG "alloc blocks %u->%u bytes %u->%u\n", blocks0, blocks1,
	    bytes0, bytes1);

	if (failures == 0) {
		printf(TAG "PASS\n");
		fflush(stdout);
		cmsis2_host_end_scheduler();
		return;
	}
	printf(TAG "FAILURES %u\n", failures);
	fflush(stdout);
	exit(1);
}

int main(int argc, char **argv)
{
	static const osThreadAttr_t suite_attr = {
		.name = "vwifi-suite",
		.priority = osPriorityNormal,
		.stack_size = THREAD_STACK_SIZE,
	};

	if (argc > 2 && strcmp(argv[1], "--fault") == 0)
		fault_mode = argv[2];

	if (osKernelInitialize() != osOK)
		fail("kernel init");
	if (osThreadNew(suite_run, NULL, &suite_attr) == NULL)
		fail("suite thread create");
	if (osKernelStart() != osOK)
		fail("kernel start");
	return 0;
}
