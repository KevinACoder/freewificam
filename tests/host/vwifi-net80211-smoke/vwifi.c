/*
 * @file    vwifi.c
 * @brief   The virtual driver: TX capture, RX injection, scripted AP responder.
 *
 * The three boundaries of the driver substitute (spec 3.3.2):
 *   - TX capture: the stack queues management frames on ic_mgtq and calls
 *     if_start_lock(ifp); the port's if_start_lock runs ifp->if_start inside
 *     the serializer, so the capture routine below is the queue drain point.
 *     The node reference the stack attached to the frame (M_SETCTX) is released
 *     here, exactly as the imported drain helper does.
 *   - RX injection: ieee80211_input() consumes the mbuf; the caller owns the
 *     node reference returned by ieee80211_find_rxnode() and releases it after
 *     the call. Frames carry no CRC (M_HASFCS stays clear).
 *   - Scan dwell: the responder answers a probe request with a probe response;
 *     the harness itself calls ieee80211_next_scan() when the channel dwell is
 *     over (the driver's callout obligation, spec 3.3.2).
 *
 * @design state container - ref net80211:driver/urtwn/if_urtwn.c:515
 * @design adapter - ref net80211:driver/urtwn/if_urtwn.c:2541
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

#include <port/port.h>

#include "osal/wlan_osal.h"
#include "vwifi.h"

static const uint8_t vwifi_ap_mac[6] = VWIFI_AP_MAC_BYTES;
static const uint8_t vwifi_sta_mac[6] = VWIFI_STA_MAC_BYTES;

/* ESS plus short preamble: the capability the responder advertises. */
#define VWIFI_CAPINFO \
	(IEEE80211_CAPINFO_ESS | IEEE80211_CAPINFO_SHORT_PREAMBLE)

/* The rates the responder advertises: the 11b/g core set, a subset of the
 * standard rateset the stack negotiates against. */
static const uint8_t vwifi_rates[] = {
	0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
};

/* ------------------------------------------------------------------ */
/* frame builders                                                      */
/* ------------------------------------------------------------------ */

static uint8_t *put16(uint8_t *p, uint16_t v)
{
	*p++ = (uint8_t) (v & 0xff);
	*p++ = (uint8_t) (v >> 8);
	return p;
}

static uint8_t *put_mac(uint8_t *p, const uint8_t mac[6])
{
	memcpy(p, mac, 6);
	return p + 6;
}

static uint8_t *put_ie(uint8_t *p, uint8_t id, const uint8_t *body, size_t len)
{
	*p++ = id;
	*p++ = (uint8_t) len;
	memcpy(p, body, len);
	return p + len;
}

/* The 24-byte management header: version 0, type management, given subtype. */
static uint8_t *put_mgmt_hdr(uint8_t *p, uint8_t fc0, const uint8_t *dst)
{
	*p++ = fc0;
	*p++ = 0x00;			/* no flags for management frames */
	p = put16(p, 0);		/* duration */
	p = put_mac(p, dst);		/* addr1 = destination */
	p = put_mac(p, vwifi_ap_mac);	/* addr2 = source = BSSID */
	p = put_mac(p, vwifi_ap_mac);	/* addr3 = BSSID */
	p = put16(p, 0);		/* sequence control */
	return p;
}

size_t vwifi_build_probe_resp(uint8_t *buf)
{
	uint8_t *p = buf;
	const uint8_t chan = VWIFI_CHAN_IEEE;

	p = put_mgmt_hdr(p, IEEE80211_FC0_SUBTYPE_PROBE_RESP, vwifi_sta_mac);
	memset(p, 0, 8);		/* timestamp */
	p += 8;
	p = put16(p, VWIFI_BINTVAL);	/* beacon interval */
	p = put16(p, VWIFI_CAPINFO);	/* capability information */
	p = put_ie(p, IEEE80211_ELEMID_SSID, (const uint8_t *) VWIFI_SSID,
	    VWIFI_SSID_LEN);
	p = put_ie(p, IEEE80211_ELEMID_RATES, vwifi_rates, sizeof(vwifi_rates));
	p = put_ie(p, IEEE80211_ELEMID_DSPARMS, &chan, 1);
	return (size_t) (p - buf);
}

size_t vwifi_build_auth_resp(uint8_t *buf)
{
	uint8_t *p = buf;

	p = put_mgmt_hdr(p, IEEE80211_FC0_SUBTYPE_AUTH, vwifi_sta_mac);
	p = put16(p, 0);		/* algorithm: open system */
	p = put16(p, 2);		/* sequence: second frame of the exchange */
	p = put16(p, 0);		/* status: success */
	return (size_t) (p - buf);
}

size_t vwifi_build_assoc_resp(uint8_t *buf)
{
	uint8_t *p = buf;

	p = put_mgmt_hdr(p, IEEE80211_FC0_SUBTYPE_ASSOC_RESP, vwifi_sta_mac);
	p = put16(p, VWIFI_CAPINFO);	/* capability information */
	p = put16(p, 0);		/* status: success */
	p = put16(p, VWIFI_AID);	/* association id */
	p = put_ie(p, IEEE80211_ELEMID_RATES, vwifi_rates, sizeof(vwifi_rates));
	return (size_t) (p - buf);
}

/* ------------------------------------------------------------------ */
/* capture                                                             */
/* ------------------------------------------------------------------ */

static void frame_store(struct vwifi_frame *dst, const uint8_t *data, size_t len)
{
	if (len > VWIFI_FRAME_MAX)
		len = VWIFI_FRAME_MAX;
	memcpy(dst->data, data, len);
	dst->len = len;
}

static void vwifi_record(struct vwifi *sc, const struct mbuf *m)
{
	struct vwifi_frame fr;
	unsigned next;
	uint8_t fc0, type, subtype;

	fr.len = (size_t) m->m_len;
	if (fr.len > VWIFI_FRAME_MAX)
		fr.len = VWIFI_FRAME_MAX;
	m_copydata(m, 0, (int) fr.len, fr.data);

	next = (sc->fifo_tail + 1) % VWIFI_FIFO_DEPTH;
	if (next != sc->fifo_head) {
		sc->fifo[sc->fifo_tail] = fr;
		sc->fifo_tail = next;
	}

	fc0 = fr.data[0];
	type = fc0 & IEEE80211_FC0_TYPE_MASK;
	subtype = fc0 & IEEE80211_FC0_SUBTYPE_MASK;
	if (type == IEEE80211_FC0_TYPE_MGT) {
		switch (subtype) {
		case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
			sc->stats.tx_probe_req++;
			sc->last_probe_req = fr;
			break;
		case IEEE80211_FC0_SUBTYPE_AUTH:
			sc->stats.tx_auth_req++;
			sc->last_auth_req = fr;
			break;
		case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
			sc->stats.tx_assoc_req++;
			sc->last_assoc_req = fr;
			break;
		default:
			sc->stats.tx_other++;
			break;
		}
	} else if (type == IEEE80211_FC0_TYPE_DATA) {
		sc->stats.tx_data++;
		sc->last_data_tx = fr;
	} else {
		sc->stats.tx_other++;
	}
}

static void vwifi_start(struct ifnet *ifp)
{
	struct vwifi *sc = ifp->if_softc;
	struct ieee80211com *ic = sc->ic;
	struct ieee80211_node *ni;
	struct mbuf *m;

	for (;;) {
		IF_DEQUEUE(&ic->ic_mgtq, m);
		if (m == NULL)
			break;
		vwifi_record(sc, m);

		ni = M_GETCTX(m, struct ieee80211_node *);
		M_SETCTX(m, NULL);
		if (ni != NULL)
			ieee80211_free_node(ni);
		m_freem(m);
	}
}

/* ------------------------------------------------------------------ */
/* RX injection and the scripted responder                             */
/* ------------------------------------------------------------------ */

void vwifi_inject(struct vwifi *sc, const uint8_t *frame, size_t len)
{
	struct ieee80211com *ic = sc->ic;
	struct ieee80211_node *ni;
	struct mbuf *m;

	if (len > VWIFI_FRAME_MAX) {
		sc->stats.inject_fail++;
		return;
	}
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		sc->stats.inject_fail++;
		return;
	}
	memcpy(mtod(m, void *), frame, len);
	m->m_len = (int) len;
	m->m_pkthdr.len = (int) len;

	wlan_port_serializer_lock();
	ni = ieee80211_find_rxnode(ic, (const struct ieee80211_frame_min *) frame);
	ieee80211_input(ic, m, ni, VWIFI_RSSI, 0);
	ieee80211_free_node(ni);
	wlan_port_serializer_unlock();
}

int vwifi_dispatch_one(struct vwifi *sc)
{
	struct vwifi_frame fr;
	uint8_t resp[VWIFI_FRAME_MAX];
	size_t rlen;

	if (sc->fifo_head == sc->fifo_tail)
		return 0;
	fr = sc->fifo[sc->fifo_head];
	sc->fifo_head = (sc->fifo_head + 1) % VWIFI_FIFO_DEPTH;

	switch (fr.data[0] & IEEE80211_FC0_SUBTYPE_MASK) {
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		rlen = vwifi_build_probe_resp(resp);
		sc->stats.rx_probe_resp++;
		frame_store(&sc->last_probe_resp, resp, rlen);
		vwifi_inject(sc, resp, rlen);
		return 1;
	case IEEE80211_FC0_SUBTYPE_AUTH:
		rlen = vwifi_build_auth_resp(resp);
		sc->stats.rx_auth_resp++;
		frame_store(&sc->last_auth_resp, resp, rlen);
		vwifi_inject(sc, resp, rlen);
		return 1;
	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
		rlen = vwifi_build_assoc_resp(resp);
		sc->stats.rx_assoc_resp++;
		frame_store(&sc->last_assoc_resp, resp, rlen);
		vwifi_inject(sc, resp, rlen);
		return 1;
	default:
		sc->stats.script_unknown++;
		return 1;
	}
}

int vwifi_mirror_data(const uint8_t *in, size_t len, uint8_t *out)
{
	uint8_t fc1;

	if (len < 24 || (in[1] & IEEE80211_FC1_DIR_MASK) != IEEE80211_FC1_DIR_TODS)
		return -1;
	memcpy(out, in, len);
	fc1 = in[1];
	out[1] = (uint8_t) ((fc1 & ~IEEE80211_FC1_DIR_MASK) |
	    IEEE80211_FC1_DIR_FROMDS);
	/* STA->AP (addr1 BSSID, addr2 SA, addr3 DA) -> AP->STA */
	memcpy(out + 4, in + 16, 6);	/* addr1 = DA */
	memcpy(out + 10, in + 4, 6);	/* addr2 = BSSID */
	memcpy(out + 16, in + 10, 6);	/* addr3 = SA */
	return 0;
}

/* ------------------------------------------------------------------ */
/* the presentation hooks of the port                                  */
/* ------------------------------------------------------------------ */

static void vwifi_on_data(const uint8_t *frame, size_t len, void *arg)
{
	struct vwifi *sc = arg;

	sc->stats.data_rx_frames++;
	frame_store(&sc->last_data_rx, frame, len);
}

static void vwifi_on_eapol(const uint8_t *src, const uint8_t *buf, size_t len,
    void *arg)
{
	struct vwifi *sc = arg;

	sc->stats.eapol_rx_frames++;
	memcpy(sc->eapol_src, src, 6);
	frame_store(&sc->last_eapol_rx, buf, len);
}

static void vwifi_on_event(enum wlan_port_event event, const uint8_t *addr,
    void *arg)
{
	struct vwifi *sc = arg;

	(void) addr;
	switch (event) {
	case WLAN_PORT_SCAN_DONE:
		sc->stats.ev_scan_done++;
		break;
	case WLAN_PORT_ASSOC:
		sc->stats.ev_assoc++;
		break;
	case WLAN_PORT_DISASSOC:
		sc->stats.ev_disassoc++;
		break;
	}
}

void vwifi_install(struct vwifi *sc)
{
	sc->ifp->if_softc = sc;
	sc->ifp->if_start = vwifi_start;
	wlan_port_set_data_rx(vwifi_on_data, sc);
	wlan_port_set_eapol_rx(vwifi_on_eapol, sc);
	wlan_port_set_event_handler(vwifi_on_event, sc);
}

void vwifi_init(struct vwifi *sc, struct ieee80211com *ic, struct ifnet *ifp)
{
	memset(sc, 0, sizeof(*sc));
	sc->ic = ic;
	sc->ifp = ifp;
}
