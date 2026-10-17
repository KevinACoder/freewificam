/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_bridge.c
 * @brief   The net80211-world half of the bridge: it owns the ieee80211com,
 *          drains the stack's management and data queues into the driver
 *          through the scalar seam, injects received frames into the stack, and
 *          drives the scan group in the order the chip and the stack require.
 *
 * @design two-worlds adapter, net80211 half (wlan_bridge.h states why the bridge
 *         comes in two halves). Everything on the net80211 side - scan state,
 *         association, node management, crypto policy - stays in the imported
 *         stack; this file only translates: the stack's asks become seam calls
 *         (ports/net80211/seam.h: scalars, byte arrays and one opaque driver
 *         handle, never a shared struct) and the driver's events become stack
 *         calls. The drain and the injection follow the tested substitute
 *         (tests/host/net80211-frame-test/vwifi.c:221-253 the mgtq drain with
 *         its node-reference release, :272-304 the RX injection with
 *         find_rxnode/input/free_node), promoted from a model to the production
 *         path; the attach recipe follows the same suite's device attach
 *         (main.c:114-156), including the CCMP template registration that stands
 *         in for the import's compiled-out link set.
 *
 * The four obligations this file exists for, each with the reason it is not
 * optional:
 *
 *   1. Management TX. net80211 queues management frames on ic_mgtq and calls
 *      if_start; this port has no callouts, so the drain here is the only thing
 *      that moves those frames - without it auth/assoc/probe-request sit in the
 *      queue forever. The node reference the stack attached to the frame is
 *      released here, exactly as the imported drain helper does.
 *   2. Receive filter before any scan. A chip drops beacons from every BSS it
 *      does not already know until its receive filter is promiscuous, so the
 *      scan sets that bit before the sweep starts and clears it once the sweep
 *      ends (the order the Linux core also uses). Without it the scan table
 *      comes back empty - a fact this project measured on the board once.
 *   3. Scan dwell. net80211's software scan arms a channel and waits for the
 *      driver to report the channel done; that report is ieee80211_next_scan(),
 *      and with no callouts in the port the bridge is its caller.
 *   4. Data TX. Frames the stack hands to if_snd are encapsulated - and, when a
 *      key is installed, encrypted - by net80211's own encap/crypto path, then
 *      handed to the driver. One if_start runs both queues because this port has
 *      one transmit path.
 *   5. Association notification. A Linux-family driver learns that the station
 *      joined (or left) a BSS only through bss_info_changed; the bridge emits it
 *      on the state transition, carrying the BSSID and frequency the stack
 *      settled on. Without it the driver never switches to its associated
 *      behaviour while the stack already has.
 *
 * @date    08.10.2026
 */

#include <string.h>

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_proto.h>
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_ioctl.h>

#include <port/port.h>

#include "osal/wlan_osal.h"
#include "wlan_bridge.h"

/* the 2.4 GHz channel set this chip family covers (channels 1..13) */
#define WLAN_BRIDGE_CHAN_MIN	1
#define WLAN_BRIDGE_CHAN_MAX	13

/* How many distinct transmitters one sweep's coverage table holds.  A sweep of
 * this environment hears on the order of ten; the table exists to tell "the
 * radio heard one AP" from "the stack's table refused the rest", so it only has
 * to be comfortably larger than the former. */
#define WLAN_BRIDGE_BSS_SEEN_MAX	32

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

/*
 * One bridge per driver instance. This port carries a single adapter, so the
 * state is a singleton whose entries verify ownership rather than assume it.
 */
static struct {
	struct wlan_seam_drv *drv;
	const struct wlan_seam_drv_ops *ops;
	struct ieee80211com *ic;
	struct ifnet *ifp;
	const struct wlan_port_ifops *ifops;
	void *if_priv;
	int attached;
	int up;			/* the driver's start ran (up is idempotent) */
	int scanning;		/* a sweep is in flight (the filter is set) */
	/*
	 * Which route the sweep in flight took. A software sweep is driven from
	 * here: the dwell pump walks the channels and the walk running out
	 * closes the window. A hardware sweep is driven by the device - the
	 * request went down once, no channel walk runs here, and the window
	 * closes when the device says so (wlan_bridge_scan_hw_complete). The flag
	 * is what keeps the dwell pump from walking channels under a firmware
	 * sweep, which would tune the radio out from under it.
	 */
	int scan_hw;
	uint32_t tuned_freq;	/* the channel the driver was last told about */
	unsigned int scan_starts;
	unsigned int scan_completes;
	unsigned int filter_sets;
	unsigned int filter_clears;
	unsigned int mgt_tx;
	unsigned int data_tx;
	unsigned int rx_frames;
	unsigned int rx_mgmt;	/* diagnostic: mgmt frames seen in input */
	unsigned int rx_beacon;	/* diagnostic: beacons seen in input */
	unsigned int rx_beacon_tgt;	/* ... from the BSSID this join targets */
	unsigned int rx_probersp;	/* diagnostic: probe responses seen */
	unsigned int rx_fcs_bad;	/* diagnostic: frames the chip flagged */
	unsigned int oversize_drops;	/* frames refused past the mbuf data area */
	unsigned int rx_mgtdiscard;	/* stack: mgmt frames dropped in the state gate */
	unsigned int rx_wrongbss;	/* stack: frames from a BSSID we are not on */
	unsigned int rx_trace;		/* the per-frame admission trace's budget */
	/* The sweep's coverage, taken as it arrives: the distinct transmitters
	 * (addr2) the current sweep's beacons and probe responses came from, and
	 * how many did not fit the table.  The stack keys its scan table on addr2
	 * (ieee80211_add_scan, node.c:1271), so `rows` alone cannot separate "the
	 * radio heard one AP" from "the table refused the rest" - this pair can,
	 * and it is the one measurement that tells a radio/channel fault from a
	 * node-table one.  Reset when a sweep is armed. */
	uint8_t		bss_seen[WLAN_BRIDGE_BSS_SEEN_MAX][6];
	unsigned int	bss_seen_n;
	unsigned int	bss_seen_ovf;
	unsigned int	scan_nodes_max;	/* scan-table high-water mark this sweep */
	unsigned int	elem_walk_logged;	/* bounded element-walk reports */
	unsigned int scan_entered;	/* sweeps that reached S_SCAN */
	unsigned int scan_close;	/* how the last sweep ended */
	unsigned int tune_calls;	/* diagnostic: channel-program attempts */
	unsigned int tune_fails;	/* diagnostic: ... that came back nonzero */
	/* The data-plane discriminator family: a data frame's protection state
	 * on the way out and on the way in. "Left encrypted" vs "left in the
	 * clear" separates a key-selection fault (the frame goes out with the
	 * wrong key, or with none) from a radio/air fault, and the receive side
	 * answers the other half - did the peer's answer reach the stack at
	 * all, and did it arrive protected. */
	unsigned int data_tx_enc;	/* data frames sent with the WEP bit */
	unsigned int data_tx_plain;	/* data frames sent without it */
	unsigned int rx_data;		/* data frames received (type 2) */
	unsigned int rx_data_enc;	/* ... that arrived protected */
	unsigned int rx_data_hwdec;	/* ... the chip already decrypted */
	unsigned int rx_data_hwdec_prot;	/* ... protected AND chip-decrypted */
	unsigned int tx_noencap;	/* data frames dropped by encap/crypto */
	int data_tx_logged;	/* the one-shot first-frame dump ran */
	int rx_data_logged;	/* ... and the receive-side one */
	uint8_t bssid_last[6];	/* the join target the filter must match */
	int bssid_last_set;
	unsigned int tx_completes;
} bridge;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static struct mbuf *
bridge_mbuf_alloc(const uint8_t *frame, size_t len)
{
	struct mbuf *m;
	size_t space;

	if (len == 0)
		return NULL;
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	/*
	 * The frame is copied where this mbuf's data pointer points, not at the
	 * cluster head: a header mbuf's m_data sits MH_ALIGN bytes into the
	 * cluster, so its writable space is M_TRAILINGSPACE, not MCLBYTES.  A
	 * `len > MCLBYTES` guard (the first version of this helper) let a frame
	 * of 3841..4096 bytes be copied past the allocation - a 4020-byte frame
	 * overran its 4200-byte block by 172 bytes and landed in the next heap
	 * block, whose free-list links the allocator then walked as garbage (the
	 * rtw89 join fault: block_remove off a wild pointer).  A frame past a
	 * legal 802.11 MPDU's size is malformed input either way; drop it named
	 * and counted instead of scribbling on the heap.
	 */
	space = (size_t)M_TRAILINGSPACE(m);
	if (len > space) {
		m_freem(m);
		bridge.oversize_drops++;
		wlan_osal_log("wlan: frame of %u bytes exceeds the %u-byte mbuf "
		    "data area; dropped\n", (unsigned)len, (unsigned)space);
		return NULL;
	}
	memcpy(mtod(m, void *), frame, len);
	m->m_len = (int)len;
	m->m_pkthdr.len = (int)len;
	return m;
}

/*
 * Note one sweep frame's transmitter.  net80211's scan table is keyed on addr2
 * (ieee80211_add_scan), so the distinct-transmitter count is exactly the
 * coverage a sweep had the chance to record: a sweep that notes one transmitter
 * could never have produced more than one row, and one that notes many while
 * the table still holds a single node has a table or allocation fault instead.
 * Bounded and reset per sweep; overflow is counted, never silently dropped.
 */
static void
bridge_bss_seen_note(const uint8_t *frame)
{
	unsigned int i;

	for (i = 0; i < bridge.bss_seen_n; i++) {
		if (memcmp(bridge.bss_seen[i], &frame[10], 6) == 0)
			return;
	}
	if (bridge.bss_seen_n >= WLAN_BRIDGE_BSS_SEEN_MAX) {
		bridge.bss_seen_ovf++;
		return;
	}
	memcpy(bridge.bss_seen[bridge.bss_seen_n], &frame[10], 6);
	bridge.bss_seen_n++;
}

/* How many entries the stack's scan table holds right now.  Taken as a
 * high-water mark over a sweep so "the sweep inserted and the entries went
 * away" can be told from "the sweep never inserted anything" - the final
 * snapshot alone cannot.  The caller holds the serializer. */
static unsigned int
bridge_scan_nodes_count(void)
{
	struct ieee80211_node *ni;
	unsigned int n = 0;

	if (bridge.ic == NULL)
		return 0;
	TAILQ_FOREACH(ni, &bridge.ic->ic_scan.nt_node, ni_list)
		n++;
	return n;
}

/*
 * Walk a beacon's element stream the way the imported parser does and report
 * where it stops short.  The parser ends a frame with `is_rx_elem_toosmall`
 * when an element's length runs past the end of the buffer (input.c:2122), so
 * the offset it stops at is exactly how many bytes the delivered frame carries
 * beyond its own element stream: four is an FCS the receive path did not strip,
 * any other value is a length the receive path got wrong.  Bounded by the
 * caller; the imported tree is not touched.
 */
static void
bridge_elem_walk_report(const uint8_t *frame, size_t len)
{
	size_t off = 24;
	unsigned int last_id = 0, last_el = 0;

	while (off + 1 < len) {
		unsigned int elen = frame[off + 1];

		if (len - off < elen + 2) {
			wlan_osal_log("wlan: rx elemwalk len=%u stop=%u over=%u "
			    "last=(id=%u elen=%u)\n", (unsigned)len,
			    (unsigned)off, (unsigned)(len - off),
			    last_id, last_el);
			return;
		}
		last_id = frame[off];
		last_el = elen;
		off += elen + 2;
	}
	wlan_osal_log("wlan: rx elemwalk len=%u clean (off=%u)\n",
	    (unsigned)len, (unsigned)off);
}

/*
 * The negotiation face: one line per association-family frame, naming the
 * elements it carries and decoding the four that decide what the link can do -
 * Supported Rates, Extended Supported Rates, HT Capabilities (45), WMM (vendor
 * 221, OUI 00:50:f2) and RSN (48).  A capability mismatch is invisible in
 * every counter the stack keeps and in every derived flag the AP reports
 * (`iw station dump` prints `WMM/WME: no` and a last-used rate, not the
 * negotiated IEs); it is visible here, in the bytes the two sides exchanged.
 *
 * `dir` is "tx", "rx" or "scan"; the fixed-field length differs per subtype, so
 * the subtype decides where the element stream starts.
 */
static void
bridge_ie_report(const char *dir, const uint8_t *frame, size_t len)
{
	unsigned int sub = (unsigned)(frame[0] & 0xf0);
	size_t off;
	unsigned int n = 0, ssid = 0, rates = 0, xrates = 0, rsn = 0;
	unsigned int htcap = 0, htinfo = 0, wme = 0;
	uint32_t mcs0 = 0;
	uint16_t hcap = 0;
	int status = -1;

	if (len < 24)
		return;
	switch (sub) {
	case 0x00:		/* association request */
		off = 24 + 4;
		break;
	case 0x20:		/* reassociation request */
		off = 24 + 10;
		break;
	case 0x10:		/* association response */
	case 0x30:		/* reassociation response */
		off = 24 + 6;
		status = len >= 28 ? (int)(frame[26] | (frame[27] << 8)) : -1;
		break;
	case 0xb0:		/* authentication */
		off = 24 + 6;
		status = len >= 30 ? (int)(frame[28] | (frame[29] << 8)) : -1;
		break;
	case 0x80:		/* beacon */
	case 0x50:		/* probe response */
		off = 24 + 12;
		break;
	default:
		off = 24;
		break;
	}
	if (off > len)
		return;
	while (off + 1 < len) {
		unsigned int id = frame[off];
		unsigned int elen = frame[off + 1];
		const uint8_t *el = &frame[off + 2];

		if (len - off < elen + 2u)
			break;
		n++;
		switch (id) {
		case 0:
			ssid = elen;
			break;
		case 1:
			rates = elen;
			break;
		case 50:
			xrates = elen;
			break;
		case 48:
			rsn = elen;
			break;
		case 45:
			htcap = 1;
			if (elen >= 2)
				hcap = (uint16_t)(el[0] | (el[1] << 8));
			/* HT Capabilities: info(2) then A-MPDU parameters(1),
			 * so the Supported MCS Set starts at el[3] */
			if (elen >= 7)
				mcs0 = (uint32_t)el[3] |
				    ((uint32_t)el[4] << 8) |
				    ((uint32_t)el[5] << 16) |
				    ((uint32_t)el[6] << 24);
			break;
		case 61:
			htinfo = 1;
			break;
		case 221:
			if (elen >= 4 && el[0] == 0x00 && el[1] == 0x50 &&
			    el[2] == 0xf2 && (el[3] == 0 || el[3] == 2))
				wme = 1;
			break;
		default:
			break;
		}
		off += elen + 2u;
	}
	wlan_osal_log("wlan: ie dir=%s sub=%02x len=%u status=%d n=%u "
	    "ssid=%u rates=%u xrates=%u rsn=%u htcap=%u hcap=0x%04x "
	    "mcs0=0x%08x htinfo=%u wme=%u\n", dir, sub, (unsigned)len, status,
	    n, ssid, rates, xrates, rsn, htcap, (unsigned)hcap,
	    (unsigned)mcs0, htinfo, wme);
}

static void
bridge_oerror(struct ieee80211com *ic)
{

	if (ic->ic_ifp != NULL)
		if_statinc(ic->ic_ifp, if_oerrors);
}

/* ------------------------------------------------------------------ */
/* transmit: the single drain of both queues (obligations 1 and 4)      */
/* ------------------------------------------------------------------ */

/*
 * The management queue drain. The stack queues with IFQ_ENQUEUE and attaches
 * the destination node in M_CTX; that reference is released once the frame has
 * left, which is what the imported drain helper does and what this loop
 * repeats.
 */
static void
bridge_drain_mgtq(struct ieee80211com *ic)
{
	struct mbuf *m;

	for (;;) {
		struct ieee80211_node *ni;
		int rc;

		IF_DEQUEUE(&ic->ic_mgtq, m);
		if (m == NULL)
			break;
		if ((mtod(m, const uint8_t *)[0] & 0xf0) != 0x40) {
			const uint8_t *fc = mtod(m, const uint8_t *);

			/* every association-family frame we send, decoded:
			 * this is the transmitting half of the negotiation
			 * record (the receiving half logs in wlan_bridge_input) */
			if ((fc[0] & 0xf0) == 0x00 || (fc[0] & 0xf0) == 0x20 ||
			    (fc[0] & 0xf0) == 0xb0)
				bridge_ie_report("tx", fc, (size_t)m->m_len);
			/* the first association-family frame and the first
			 * probe request go out as one hex line each: the air
			 * audit's raw material (a frame that the chip never
			 * answers is judged from its bytes, not from the
			 * counter that handed it over).  The association
			 * request gets its own one-shot dump: an AP that
			 * answers the auth and then deauthenticates instead
			 * of associating is judged from those bytes against
			 * the working line's. */
			static int dumped_join;
			static int dumped_assoc;

			if ((fc[0] & 0xf0) == 0x00 && !dumped_assoc) {
				unsigned int k;

				dumped_assoc = 1;
				wlan_osal_log("wlan: mgt tx assoc-req-bytes "
				    "len=%u:", (unsigned)m->m_len);
				for (k = 0; k < (unsigned)m->m_len && k < 96;
				    k++)
					wlan_osal_log("%02x%s", fc[k],
					    (k % 16) == 15u || k + 1u ==
					    (unsigned)m->m_len ? "\n" : " ");
			}
			if (!dumped_join) {
				unsigned int k;

				wlan_osal_log("wlan: mgt tx first-join-bytes "
				    "len=%u:", (unsigned)m->m_len);
				for (k = 0; k < (unsigned)m->m_len && k < 64;
				    k++)
					wlan_osal_log("%02x%s", fc[k],
					    (k % 16) == 15u || k + 1u ==
					    (unsigned)m->m_len ? "\n" : " ");
				dumped_join = 1;
			}
			wlan_osal_log("wlan: mgt tx fc=%02x len=%u a1=%02x:%02x:"
			    "%02x:%02x:%02x:%02x a3=%02x:%02x:%02x:%02x:%02x:%02x\n",
			    fc[0], (unsigned)m->m_len, fc[4], fc[5], fc[6], fc[7],
			    fc[8], fc[9], fc[16], fc[17], fc[18], fc[19], fc[20],
			    fc[21]);
		} else {
			const uint8_t *fc = mtod(m, const uint8_t *);
			static int dumped_probe2;

			if (!dumped_probe2) {
				unsigned int k;

				dumped_probe2 = 1;
				wlan_osal_log("wlan: mgt tx first-probe-bytes "
				    "len=%u:", (unsigned)m->m_len);
				for (k = 0; k < (unsigned)m->m_len && k < 96;
				    k++)
					wlan_osal_log("%02x%s", fc[k],
					    (k % 16) == 15u || k + 1u ==
					    (unsigned)m->m_len ? "\n" : " ");
			}
		}
		rc = bridge.ops->tx(bridge.drv, mtod(m, const uint8_t *),
		    (size_t)m->m_len, 1);
		if (rc < 0)		/* the seam answers the frame's own length
					 * on success and a negative error
					 * otherwise (wlan_seam_adapter.c
					 * seam_tx), so only a negative return
					 * is an output error */
			bridge_oerror(ic);
		else if (ic->ic_ifp != NULL)
			if_statinc(ic->ic_ifp, if_opackets);
		bridge.mgt_tx++;
		ni = M_GETCTX(m, struct ieee80211_node *);
		M_SETCTX(m, NULL);
		if (ni != NULL)
			ieee80211_free_node(ni);
		m_freem(m);
	}
}

/*
 * The data queue drain: net80211's own encapsulation runs here, and then -
 * when the frame came out marked protected - the crypto header and trailer are
 * added by ieee80211_crypto_encap. That second call is the driver's own step in
 * the imported design: the stack's encap leaves the packet marked (sets the WEP
 * bit) and documents that the crypto header "is added when the driver calls
 * back to ieee80211_crypto_encap to do crypto encapsulation"
 * (ieee80211_output.c:445). The Linux-family driver on the far side of this
 * seam does not do it - mac80211 does - so the bridge owes it here.
 */
static void
bridge_drain_if_snd(struct ieee80211com *ic)
{
	struct ifnet *ifp = ic->ic_ifp;
	struct mbuf *m;

	for (;;) {
		struct mbuf *enc;
		struct ieee80211_node *ni;
		struct ieee80211_frame *wh;

		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		ni = ic->ic_bss;
		if (ni == NULL) {
			m_freem(m);
			if_statinc(ifp, if_oerrors);
			continue;
		}
		enc = ieee80211_encap(ic, m, ni);
		if (enc == NULL) {
			bridge.tx_noencap++;
			if_statinc(ifp, if_oerrors);	/* encap consumed it */
			continue;
		}
		wh = mtod(enc, struct ieee80211_frame *);
		if ((wh->i_fc[1] & IEEE80211_FC1_WEP) != 0) {
			/*
			 * The call seals in place and returns the key (NULL on
			 * failure). It can in principle re-head the mbuf - the
			 * imported function carries the known "we modify m"
			 * caveat - but the frame just came out of encap, which
			 * reserved the crypto header room for exactly this call
			 * (output.c:440-445), so the header is prepended in
			 * place. The tested carrier uses the call the same way
			 * (frame-test main.c:630-641).
			 */
			if (ieee80211_crypto_encap(ic, ni, enc) == NULL) {
				bridge.tx_noencap++;
				m_freem(enc);
				if_statinc(ifp, if_oerrors);
				continue;
			}
			bridge.data_tx_enc++;
		} else {
			bridge.data_tx_plain++;
		}
		/* the one-shot first-data-frame dump: the key id the frame
		 * carries is what the peer indexes its decrypt with, so a
		 * group-addressed frame sealed with the pairwise key (key id 0)
		 * reads straight off this line. The id sits in the top two bits
		 * of the CCMP header's fourth byte (the caller passes it
		 * pre-shifted, ieee80211_crypto.c:585), and that byte starts
		 * where the 802.11 header ends (ccmp_encap writes it in place
		 * at ieee80211_hdrspace, ieee80211_crypto_ccmp.c:145-160). */
		if (!bridge.data_tx_logged) {
			const uint8_t *ccmp;

			bridge.data_tx_logged = 1;
			wh = mtod(enc, struct ieee80211_frame *);
			ccmp = mtod(enc, const uint8_t *) +
			    ieee80211_hdrspace(ic, mtod(enc, const void *));
			wlan_osal_log("wlan: data tx first: dst=%02x:%02x:%02x:%02x:"
			    "%02x:%02x prot=%d keyid=%d len=%u\n",
			    wh->i_addr1[0], wh->i_addr1[1], wh->i_addr1[2],
			    wh->i_addr1[3], wh->i_addr1[4], wh->i_addr1[5],
			    (wh->i_fc[1] & IEEE80211_FC1_WEP) != 0,
			    (wh->i_fc[1] & IEEE80211_FC1_WEP) != 0 ?
			    (int)((ccmp[3] >> 6) & 0x3) : -1,
			    (unsigned)enc->m_pkthdr.len);
		}
		if (bridge.ops->tx(bridge.drv, mtod(enc, const uint8_t *),
		    (size_t)enc->m_pkthdr.len, 0) < 0)
			if_statinc(ifp, if_oerrors);	/* same return contract as
							 * the management drain: a
							 * negative answer is the
							 * error */
		else
			if_statinc(ifp, if_opackets);
		bridge.data_tx++;
		m_freem(enc);
	}
}

static void
bridge_start(struct ifnet *ifp)
{
	struct ieee80211com *ic = (struct ieee80211com *)ifp->if_softc;

	if (!bridge.attached || ic == NULL || bridge.ops == NULL)
		return;
	/* management first: auth/assoc must not queue behind data */
	bridge_drain_mgtq(ic);
	bridge_drain_if_snd(ic);
}

/* ------------------------------------------------------------------ */
/* the state transition the presentation layer watches                 */
/* ------------------------------------------------------------------ */

/*
 * net80211 drives its state machine through ic_newstate. The presentation layer
 * needs to know when the interface reaches RUN (DHCP starts) and when it leaves
 * it; the wrapper below is the port's call point for port.h's wlan_port_ifops
 * (that header has no setter, so the bridge owns the wiring). The stack's own
 * handler is kept and called first: this only observes, it never replaces the
 * state machine.
 *
 * Two duties ride on the same transition:
 *
 *   - Channel. Every state transition is a use of the radio: the sweep's probe
 *     request for the newly armed channel and the join's auth request on the
 *     AP's channel both leave inside the stack's handler. So the radio is
 *     pointed at ic_curchan BEFORE the handler runs. (The sweep arms its first
 *     channel inside begin_scan, which is reached from the handler, so the
 *     per-channel armings arrive here as their own transitions - the nested
 *     new_state(S_SCAN) call - which is what makes the order work.)
 *   - Association. A Linux-family driver learns about the join only through
 *     bss_info_changed carrying BSS_CHANGED_ASSOC and BSS_CHANGED_BSSID, and
 *     that is exactly what this family of drivers reads back (rtl8xxxu
 *     core.c:3211 reads bssid and assoc). Without the call the driver keeps
 *     believing it is unassociated while the stack has already joined.
 */
static int (*bridge_orig_newstate)(struct ieee80211com *,
    enum ieee80211_state, int);

static int
bridge_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	enum ieee80211_state ostate = ic->ic_state;
	int rc;

	/*
	 * Every association-relevant transition, stamped by the log sink's own
	 * boot clock.  A join that ends up back in SCAN has one cause per step
	 * and the stack names it here: net80211's own management-frame timeout
	 * (ieee80211.c:823, request+5 s) and the supplicant's recovery both
	 * drive new_state, and the scan arm that follows is this bridge's own.
	 * SCAN -> SCAN (one per dwell) is skipped: it carries no association
	 * news and would flood the console.  This is the line that answers
	 * "what moved the stack out of AUTH, and when".
	 */
	if (nstate >= IEEE80211_S_AUTH || ostate >= IEEE80211_S_AUTH)
		wlan_osal_log("wlan: state %d -> %d arg=%d flags=0x%x cur=%d "
		    "scanning=%d tuned=%u\n", (int)ostate, (int)nstate, arg,
		    (unsigned)ic->ic_flags,
		    ic->ic_curchan != NULL ?
		    (int)(ic->ic_curchan - ic->ic_channels) : -1,
		    bridge.scanning, bridge.tuned_freq);

	(void)wlan_bridge_tune_current();
	/*
	 * Pre-association BSSID programming: entering AUTH uses the radio, and
	 * the chip's receive filter compares incoming frames against its BSSID
	 * register - without the join's target in it before the auth request
	 * leaves, the AP's answers are dropped in hardware (measured on the
	 * board: requests out, nothing back, the timeout loop with it). The
	 * stack has already settled ic_bss onto the target when sta_join
	 * re-entered this transition.
	 */
	if (nstate == IEEE80211_S_AUTH || nstate == IEEE80211_S_ASSOC) {
		struct ieee80211_node *bss = ic->ic_bss;

		if (bridge.ops != NULL && bridge.ops->bssid != NULL &&
		    bss != NULL) {
			memcpy(bridge.bssid_last, bss->ni_bssid, 6);
			bridge.bssid_last_set = 1;
			/* the AP's own timing travels with the target: the
			 * interface fills it while it prepares the connection,
			 * and a driver reads it before it takes a station */
			(void)bridge.ops->bssid(bridge.drv, bss->ni_bssid,
			    (uint16_t)bss->ni_intval,
			    bss->ni_dtim_period != 0 ?
			    (uint8_t)bss->ni_dtim_period : 1);
		}
	} else if (ostate >= IEEE80211_S_AUTH && nstate < IEEE80211_S_AUTH &&
	    bridge.ops != NULL && bridge.ops->bssid != NULL) {
		/*
		 * The connection attempt is over without having reached RUN (a
		 * failed authentication, an aborted join): the target comes off
		 * the driver's view, the way mac80211 destroys the AP's station
		 * when a connection fails.  A target that did reach RUN is wound
		 * down by the leave notification below instead.  No NULL BSSID
		 * reaches the registers: the drivers' handlers dereference the
		 * pointer, so the seam only clears its own view (the rule the
		 * seam's leave path documents).
		 */
		bridge.bssid_last_set = 0;
		(void)bridge.ops->bssid(bridge.drv, NULL, 0, 0);
	}
	rc = bridge_orig_newstate(ic, nstate, arg);

	if (nstate == IEEE80211_S_RUN && ostate != IEEE80211_S_RUN) {
		if (bridge.ops != NULL && bridge.ops->bss_info != NULL) {
			struct ieee80211_node *bss = ic->ic_bss;

			(void)bridge.ops->bss_info(bridge.drv, 1,
			    bss != NULL ? bss->ni_bssid : ic->ic_myaddr,
			    bss != NULL && bss->ni_chan != NULL ?
			    bss->ni_chan->ic_freq : 0,
			    /* ni_associd carries the 0xc000 marking; a
			     * mac80211 driver wants the raw association id */
			    bss != NULL ? (uint16_t)(bss->ni_associd & 0x3fff) : 0,
			    bss != NULL ? bss->ni_rates.rs_rates : NULL,
			    bss != NULL ? bss->ni_rates.rs_nrates : 0);
		}
		if (bridge.ifops != NULL && bridge.ifops->up != NULL)
			(void)bridge.ifops->up(bridge.if_priv);
	} else if (nstate != IEEE80211_S_RUN && ostate == IEEE80211_S_RUN) {
		if (bridge.ops != NULL && bridge.ops->bss_info != NULL)
			(void)bridge.ops->bss_info(bridge.drv, 0, NULL, 0, 0,
			    NULL, 0);
		if (bridge.ifops != NULL && bridge.ifops->down != NULL)
			(void)bridge.ifops->down(bridge.if_priv);
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* attach / detach                                                     */
/* ------------------------------------------------------------------ */

int
wlan_bridge_attach(struct wlan_seam_drv *drv,
    const struct wlan_seam_drv_ops *ops, const uint8_t mac[6])
{
	struct ieee80211com *ic;
	struct ifnet *ifp;
	int ch;

	/*
	 * The port's own init: the serializer, the sleep slots and the tick
	 * rate. It belongs here rather than in each image's start-up because
	 * every line attaches through this call, and it is idempotent
	 * (wlan_osal_init's RUN_ONCE) for the suites that call it themselves.
	 * The board's first sweep found what its absence costs: the serializer
	 * was a no-op (mutex_enter refused an uninitialised lock - the port's
	 * own alarm), so the sweep and the driver's receive thread ran
	 * unserialised, which single-threaded lines had never exposed.
	 */
	wlan_osal_init();

	if (bridge.attached) {
		wlan_osal_log("wlan: a second attach refused (single adapter)\n");
		return -EBUSY;
	}
	if (drv == NULL || ops == NULL || mac == NULL)
		return -EINVAL;

	ic = wlan_osal_zalloc(sizeof(*ic));
	if (ic == NULL)
		return -ENOMEM;
	ifp = wlan_osal_zalloc(sizeof(*ifp));
	if (ifp == NULL) {
		wlan_osal_free(ic);
		return -ENOMEM;
	}

	/* the attach recipe of the tested substitute (frame-test main.c:114-156) */
	ic->ic_ifp = ifp;
	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	memcpy(ic->ic_myaddr, mac, IEEE80211_ADDR_LEN);

	/* the one-channel diagnostic build points here: the channel the
	 * join targets is the only one invented, so no sweep and no join
	 * ever switches the radio - the discriminator between "transmit
	 * wedges after channel switching" and everything else */
#ifdef WLAN_BRIDGE_SINGLE_CHANNEL
	for (ch = WLAN_BRIDGE_CHAN_MIN; ch <= 1; ch++) {
#else
	for (ch = WLAN_BRIDGE_CHAN_MIN; ch <= WLAN_BRIDGE_CHAN_MAX; ch++) {
#endif
		struct ieee80211_channel *c = &ic->ic_channels[ch];

		c->ic_freq = 2407 + ch * 5;
		c->ic_flags = IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
		setbit(ic->ic_chan_avail, ch);
	}
	ic->ic_curchan = &ic->ic_channels[1];

	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	if (if_initialize(ifp) != 0) {
		wlan_osal_free(ifp);
		wlan_osal_free(ic);
		return -EIO;
	}
	(void)if_percpuq_create(ifp);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_softc = ic;
	ifp->if_start = bridge_start;

	/*
	 * The cipher templates: the import's link-set registration is compiled
	 * out on this port, so the templates the stack will actually use are
	 * registered explicitly (CCMP is the WPA2 cipher this target uses; NONE
	 * is installed by the crypto attach path itself).
	 */
	ieee80211_crypto_register(&ieee80211_cipher_ccmp);

	ieee80211_ifattach(ic);
	ieee80211_media_init(ic, ieee80211_media_change,
	    ieee80211_media_status);
	ifp->if_percpuq = if_percpuq_create(ifp);
	if_register(ifp);

	/* the observation wrapper is installed after ifattach (which installs
	 * the stack's own handler) */
	bridge_orig_newstate = ic->ic_newstate;
	ic->ic_newstate = bridge_newstate;

	/*
	 * The driver starts before its interface is added, which is the order
	 * the interface itself uses: ieee80211_do_open starts the hardware, and
	 * the interfaces are added afterwards, so a driver's add_interface may
	 * assume the chip is already running.  rtw89 does - its add_interface
	 * programs the interface's port and reads the host flow-control pages
	 * back - and refuses the interface when the start has not run (measured
	 * on the board: "add_interface refused (-14)").  bridge.up records that
	 * the start has run, so wlan_bridge_up() below does not start it twice.
	 */
	if (ops->start != NULL) {
		if (ops->start(drv) != 0) {
			wlan_osal_log("wlan: the driver refused to start\n");
			ic->ic_newstate = bridge_orig_newstate;
			ieee80211_ifdetach(ic);
			wlan_osal_free(ifp);
			wlan_osal_free(ic);
			return -EIO;
		}
		bridge.up = 1;
	}

	/* the driver side learns about the interface (its own vif/priv area) */
	if (ops->add_interface != NULL &&
	    ops->add_interface(drv, mac, WLAN_SEAM_IFTYPE_STATION) != 0) {
		wlan_osal_log("wlan: the driver refused the interface\n");
		if (bridge.up && ops->stop != NULL) {
			(void)ops->stop(drv);
			bridge.up = 0;
		}
		ic->ic_newstate = bridge_orig_newstate;
		ieee80211_ifdetach(ic);
		wlan_osal_free(ifp);
		wlan_osal_free(ic);
		return -EIO;
	}

	bridge.drv = drv;
	bridge.ops = ops;
	bridge.ic = ic;
	bridge.ifp = ifp;
	bridge.attached = 1;
	wlan_osal_log("wlan: bridge attached %02x:%02x:%02x:%02x:%02x:%02x\n",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return 0;
}

void
wlan_bridge_detach(void)
{

	if (!bridge.attached)
		return;
	if (bridge.ops->stop != NULL)
		(void)bridge.ops->stop(bridge.drv);
	ieee80211_ifdetach(bridge.ic);
	wlan_osal_free(bridge.ifp);
	wlan_osal_free(bridge.ic);
	bridge.drv = NULL;
	bridge.ops = NULL;
	bridge.ic = NULL;
	bridge.ifp = NULL;
	bridge.attached = 0;
	bridge.scanning = 0;
	wlan_osal_log("wlan: bridge detached\n");
}

/* ------------------------------------------------------------------ */
/* receive                                                             */
/* ------------------------------------------------------------------ */

/*
 * The interface's channel for a receive frequency, or NULL when the driver
 * reported no frequency or one this port's channel map does not carry (a 5 GHz
 * report on a 2.4 GHz-only map, say).  Only channels the port invented at attach
 * are eligible, so a stray report can never point the parser at a channel the
 * map does not describe.
 */
static struct ieee80211_channel *
bridge_chan_for_freq(struct ieee80211com *ic, uint32_t freq)
{
	unsigned int ch;

	if (freq == 0)
		return NULL;
	for (ch = WLAN_BRIDGE_CHAN_MIN; ch <= WLAN_BRIDGE_CHAN_MAX; ch++) {
		struct ieee80211_channel *c = &ic->ic_channels[ch];

		if (c->ic_flags != 0 && c->ic_freq == freq)
			return c;
	}
	return NULL;
}

/*
 * Receive: the driver's frames reach the stack. The frame arrives without FCS;
 * the bridge copies it into the stack's own buffer, so the caller keeps its
 * buffer. The node reference find_rxnode returns is released after input, as
 * the imported path requires.
 */
int
wlan_bridge_input(const uint8_t *frame, size_t len, int rssi, int fcs_bad,
    int hw_dec, uint32_t rx_freq)
{
	struct ieee80211com *ic = bridge.ic;
	struct ieee80211_node *ni;
	struct ieee80211_channel *rx_chan;
	struct ieee80211_channel *saved_chan;
	struct mbuf *m;

	if (!bridge.attached || ic == NULL || frame == NULL)
		return -ENODEV;
	m = bridge_mbuf_alloc(frame, len);
	if (m == NULL) {
		wlan_osal_log("wlan: rx without a buffer; frame dropped\n");
		return -ENOMEM;
	}
	bridge.rx_frames++;
	if (fcs_bad)
		bridge.rx_fcs_bad++;
	/* the receive half of the data-plane discriminator: a data frame that
	 * reached the bridge at all, and whether it came in protected. The
	 * pair separates "the peer never answered" from "the answer arrived
	 * and the stack dropped it" - and, with the crypto counters the stats
	 * snapshot carries, from "the answer arrived and did not decrypt". */
	if (len >= 24 && (frame[0] & 0x0c) == 0x08) {	/* data */
		bridge.rx_data++;
		if ((frame[1] & IEEE80211_FC1_WEP) != 0) {
			bridge.rx_data_enc++;
			/* protected and already decrypted by the chip: this is
			 * the frame a software-crypto stack decrypts a second
			 * time, so the count is the fingerprint itself */
			if (hw_dec)
				bridge.rx_data_hwdec_prot++;
		}
		if (hw_dec)
			bridge.rx_data_hwdec++;
		/* the one-shot first-data-frame dump: which addresses the frame
		 * carries (the receiver address decides the key lookup, the
		 * third address is the real destination), whether it arrived
		 * protected, and whether the chip already decrypted it - the
		 * last one is the fingerprint of a frame the stack will try to
		 * decrypt a second time */
		if (!bridge.rx_data_logged) {
			bridge.rx_data_logged = 1;
			wlan_osal_log("wlan: data rx first: fc=%02x%02x a1=%02x:%02x:"
			    "%02x:%02x:%02x:%02x a2=%02x:%02x:%02x:%02x:%02x:%02x "
			    "a3=%02x:%02x:%02x:%02x:%02x:%02x prot=%d hwdec=%d "
			    "fcs=%d len=%u\n", frame[0], frame[1],
			    frame[4], frame[5], frame[6], frame[7], frame[8],
			    frame[9], frame[10], frame[11], frame[12], frame[13],
			    frame[14], frame[15], frame[16], frame[17], frame[18],
			    frame[19], frame[20], frame[21],
			    (frame[1] & IEEE80211_FC1_WEP) != 0, hw_dec ? 1 : 0,
			    fcs_bad ? 1 : 0, (unsigned)len);
		}
	}
	if (len >= 24 && (frame[0] & 0x0c) == 0x00) {	/* management */
		uint8_t fc0 = frame[0];
		uint8_t sub = (uint8_t)(fc0 & 0xf0);

		/*
		 * The discriminator family, counted (lines would flood at a
		 * beacon's period): beacons split by "from the BSSID this
		 * join targets", probe responses apart, and the FCS-flagged
		 * noise above. A join wait with target beacons flowing says
		 * the radio sits on the AP's channel AND the chip's address
		 * filter passes the AP's frames; a wait with only flagged
		 * noise says the opposite; an either-way absence of probe
		 * responses during sweeps says the probes stopped being
		 * answered.
		 */
		if (sub == 0x80) {
			bridge.rx_beacon++;
			if (bridge.bssid_last_set &&
			    memcmp(&frame[16], bridge.bssid_last, 6) == 0)
				bridge.rx_beacon_tgt++;
			bridge_bss_seen_note(frame);
			if (bridge.elem_walk_logged < 3) {
				bridge.elem_walk_logged++;
				bridge_elem_walk_report(frame, len);
				/* what the AP advertises, decoded - the other
				 * end of a capability mismatch */
				bridge_ie_report("scan", frame, len);
			}
		} else if (sub == 0x50) {
			bridge.rx_probersp++;
			bridge_bss_seen_note(frame);
			if (bridge.rx_probersp < 4)
				bridge_ie_report("scan", frame, len);
		}
		/* The association-exchange family always logs (rare, and the
		 * evidence a join judgment stands on), now with the FCS
		 * verdict and, for the first few, the raw bytes: a frame
		 * that arrives garbled splits "corrupted before the driver"
		 * (fcs set) from "corrupted between the driver and here"
		 * (fcs clear but the bytes are not an answer's shape);
		 * other frames are sampled so the lines stay readable. */
		if ((fc0 & 0x0c) == 0x00 && (sub == 0xb0 /* auth */ ||
		    sub == 0x00 || sub == 0x10 /* assoc req/resp */)) {
			static unsigned int dumped;

			/* the receiving half of the negotiation record: the
			 * AP's answer with every element it carries */
			bridge_ie_report("rx", frame, len);
			wlan_osal_log("wlan: rx mgt fc=%02x fcs=%d retry=%u sc=%04x "
			    "a1=%02x:%02x:%02x:%02x:%02x:%02x "
			    "a2=%02x:%02x:%02x:%02x:%02x:%02x len=%u\n", fc0,
			    fcs_bad ? 1 : 0, (frame[1] & 0x08) != 0,
			    (unsigned)(frame[22] | (frame[23] << 8)),
			    frame[4], frame[5], frame[6],
			    frame[7], frame[8], frame[9], frame[10], frame[11],
			    frame[12], frame[13], frame[14], frame[15],
			    (unsigned)len);
			if (dumped < 8) {
				unsigned int k;

				dumped++;
				wlan_osal_log("wlan: rx join-bytes:");
				for (k = 0; k < len && k < 64; k++)
					wlan_osal_log(" %02x", frame[k]);
				wlan_osal_log("\n");
			}
		} else if (sub != 0x80 && bridge.rx_mgmt < 64) {
			bridge.rx_mgmt++;
			wlan_osal_log("wlan: rx mgt fc=%02x a1=%02x:%02x:%02x:"
			    "%02x:%02x:%02x a3=%02x:%02x:%02x:%02x:%02x:%02x "
			    "len=%u%s\n", fc0, frame[4],
			    frame[5], frame[6], frame[7], frame[8], frame[9],
			    frame[16], frame[17], frame[18], frame[19],
			    frame[20], frame[21],
			    (unsigned)len, fcs_bad ? " fcs!" : "");
		}
		/*
		 * A deauthenticate/disassociate from the AP is the one frame that
		 * ends a join attempt from the AP's side; its reason code lives in
		 * the first two bytes after the header.  An AP that answers the
		 * auth and then deauthenticates the station (the rtw89 line
		 * 2026-10-10: auth answered, assoc request sent, DEAUTH ~600 ms
		 * later with the stack still in ASSOC) is judged from this code -
		 * 2 = "previous auth invalid", 6/7 = class-2/3 frame from an
		 * unauthenticated station, 15 = 4-way timeout.
		 */
		if ((fc0 & 0x0c) == 0x00 && (sub == 0xc0 /* deauth */ ||
		    sub == 0xa0 /* disassoc */)) {
			static unsigned int dumped_deauth;

			wlan_osal_log("wlan: rx mgt %s reason=%u a1=%02x:%02x:%02x:"
			    "%02x:%02x:%02x a2=%02x:%02x:%02x:%02x:%02x:%02x "
			    "len=%u\n", sub == 0xc0 ? "deauth" : "disassoc",
			    (unsigned)(len >= 26 ? (frame[24] | (frame[25] << 8))
						 : 0xffffu),
			    frame[4], frame[5], frame[6], frame[7], frame[8],
			    frame[9], frame[10], frame[11], frame[12], frame[13],
			    frame[14], frame[15], (unsigned)len);
			if (dumped_deauth < 2) {
				unsigned int k;

				dumped_deauth++;
				wlan_osal_log("wlan: rx deauth-bytes:");
				for (k = 0; k < len && k < 48; k++)
					wlan_osal_log(" %02x", frame[k]);
				wlan_osal_log("\n");
			}
		}
	}

	/*
	 * The frame's own receive channel, pointed at for the duration of input.
	 * The imported parser reads the receive channel off the interface's
	 * *current* channel (ieee80211_input.c:2118) and silently discards a
	 * beacon whose DS-parameter channel differs from it (:2213) - correct only
	 * while the driver's delivery is synchronous with the dwell.  This port's
	 * receive path runs on its own thread, so a frame delivered across a dwell
	 * would be judged against the wrong channel and dropped; the frame carries
	 * the channel it arrived on, and input runs with the current channel
	 * pointed at it.  The serializer keeps a concurrent ieee80211_next_scan()
	 * from interleaving, and the previous value is put back so the walk's own
	 * bookkeeping is untouched.
	 */
	wlan_port_serializer_lock();
	rx_chan = bridge_chan_for_freq(ic, rx_freq);
	saved_chan = ic->ic_curchan;
	if (rx_chan != NULL)
		ic->ic_curchan = rx_chan;
	/*
	 * The admission trace: the stack's own drop reasons, taken as deltas
	 * around one input call, on the first few beacons/probe responses.  An
	 * empty scan table with `mgtdiscard` climbing means the frame arrived with
	 * no scan window up; with `chanmismatch` climbing it means the frame was
	 * judged against the wrong channel; with `nodealloc` climbing the table
	 * itself refused the entry.  Bounded to a handful of lines so the console
	 * stays readable.
	 */
	if (len >= 24 && (frame[0] & 0x0c) == 0x00 &&
	    (frame[0] & 0xf0) == 0x80 && bridge.rx_trace < 6) {
		struct ieee80211_stats *st = &ic->ic_stats;
		u_int32_t mgt0 = st->is_rx_mgtdiscard;
		u_int32_t wb0 = st->is_rx_wrongbss;
		u_int32_t bc0 = st->is_rx_badchan;
		u_int32_t cm0 = st->is_rx_chanmismatch;
		u_int32_t na0 = st->is_rx_nodealloc;
		u_int32_t fscan = (ic->ic_flags & IEEE80211_F_SCAN) != 0;
		u_int32_t asc = (ic->ic_flags & IEEE80211_F_ASCAN) != 0;
		int cur = rx_chan != NULL ?
		    (int)(rx_chan - ic->ic_channels) : -1;
		int was = saved_chan != NULL ?
		    (int)(saved_chan - ic->ic_channels) : -1;

		bridge.rx_trace++;
		wlan_osal_log("wlan: rxtrace fc=%02x len=%u rx_freq=%u "
		    "chan=%d cur=%d state=%d fscan=%u asc=%u scanning=%u "
		    "ic=%p\n", frame[0], (unsigned)len, rx_freq, cur, was,
		    (int)ic->ic_state, fscan, asc, bridge.scanning,
		    (void *)ic);
		ni = ieee80211_find_rxnode(ic,
		    (const struct ieee80211_frame_min *)m->m_data);
		ieee80211_input(ic, m, ni, rssi, 0);
		ieee80211_free_node(ni);
		wlan_osal_log("wlan: rxtrace  -> mgtdiscard=+%u wrongbss=+%u "
		    "badchan=+%u chmis=+%u nodealloc=+%u\n",
		    st->is_rx_mgtdiscard - mgt0, st->is_rx_wrongbss - wb0,
		    st->is_rx_badchan - bc0, st->is_rx_chanmismatch - cm0,
		    st->is_rx_nodealloc - na0);
	} else {
		ni = ieee80211_find_rxnode(ic,
		    (const struct ieee80211_frame_min *)m->m_data);
		ieee80211_input(ic, m, ni, rssi, 0);
		ieee80211_free_node(ni);
	}
	ic->ic_curchan = saved_chan;
	bridge.rx_mgtdiscard = ic->ic_stats.is_rx_mgtdiscard;
	bridge.rx_wrongbss = ic->ic_stats.is_rx_wrongbss;
	if (bridge.scanning) {
		unsigned int n = bridge_scan_nodes_count();

		if (n > bridge.scan_nodes_max)
			bridge.scan_nodes_max = n;
	}
	wlan_port_serializer_unlock();
	return 0;
}

void
wlan_bridge_tx_complete(void)
{

	bridge.tx_completes++;
}

/* ------------------------------------------------------------------ */
/* the scan group (obligations 2 and 3)                                */
/* ------------------------------------------------------------------ */

/* The hardware-scan request carries a bounded channel list; the cap matches the
 * substitute's own (components/linuxkpi/linuxkpi_80211.c, whose request object
 * holds the same bound). */
#define WLAN_BRIDGE_SCAN_FREQ_MAX	64

/*
 * The stack's active channels, as the centre frequencies a hardware scan wants.
 * `ic_chan_active` is the mask the walk itself consults
 * (ieee80211_next_scan, ieee80211_node.c:328-360: `isset(ic_chan_scan, ...)`
 * over the same table), so this is the walk's own channel set in another
 * spelling rather than a second opinion about which channels exist.
 */
static size_t
bridge_scan_active_freqs(uint32_t *freqs, size_t max)
{
	size_t n = 0;
	int i;

	for (i = 0; i <= IEEE80211_CHAN_MAX && n < max; i++) {
		if (!isset(bridge.ic->ic_chan_active, i))
			continue;
		if (bridge.ic->ic_channels[i].ic_freq == 0)
			continue;
		freqs[n++] = bridge.ic->ic_channels[i].ic_freq;
	}
	return n;
}

/*
 * Start a sweep. The filter goes first (obligation 2): a chip that is not
 * promiscuous answers no unknown beacon, so a sweep started before the bit is
 * programmed records nothing.
 */
int
wlan_bridge_scan_start(void)
{

	if (!bridge.attached)
		return -ENODEV;
	/* one sweep at a time: the radio has one channel walk, and a second
	 * arm over a live one would corrupt the filter/walk bookkeeping. The
	 * two sweep drivers in a WPA image (a case and the supplicant) coexist
	 * by the caller retrying this refusal, not by nesting. */
	if (bridge.scanning)
		return -EBUSY;
	if (bridge.ops->filter == NULL ||
	    bridge.ops->filter(bridge.drv, WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC,
	    WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC) != 0) {
		wlan_osal_log("wlan: receive filter refused; a scan would see "
		    "nothing\n");
		return -EIO;
	}
	bridge.filter_sets++;
	/*
	 * Which route this sweep takes: the driver answers for itself, because
	 * the two are not interchangeable. A driver with a software scan takes
	 * it and the bridge drives the walk below. A driver whose ops table has
	 * only a hardware scan (the AC 7260's mvm path, which has no
	 * sw_scan_start at all) gets the request form instead: the channel set
	 * goes down once and the device owns the dwell, so no walk runs here and
	 * the window closes from wlan_bridge_scan_hw_complete.
	 *
	 * The set handed down is the stack's own active channel list - the same
	 * one the walk would have visited - so the firmware sweeps what a
	 * software sweep would have.
	 */
	bridge.scan_hw = bridge.ops->has_hw_scan != NULL &&
	    bridge.ops->has_hw_scan(bridge.drv) != 0;
	if (bridge.scan_hw) {
		uint32_t freqs[WLAN_BRIDGE_SCAN_FREQ_MAX];
		size_t n_freqs = bridge_scan_active_freqs(freqs,
		    WLAN_BRIDGE_SCAN_FREQ_MAX);
		int hw_rc;

		hw_rc = n_freqs == 0 || bridge.ops->hw_scan_start == NULL ?
		    -1 : bridge.ops->hw_scan_start(bridge.drv, freqs, n_freqs);
		if (hw_rc == 1 && bridge.ops->sw_scan_start != NULL) {
			/*
			 * mac80211's own fallback (scan.c:896-906): a driver
			 * answers 1 for "this request cannot be taken as a
			 * hardware scan", and the interface then runs the
			 * software route instead of failing the sweep.  The
			 * Realtek vendors' drivers register both routes and
			 * answer 1 while their firmware has no scan offload,
			 * which is a live configuration on this board - treating
			 * the answer as an error killed their sweeps outright.
			 */
			bridge.scan_hw = 0;
		} else if (hw_rc != 0) {
			wlan_osal_log("wlan: hw scan refused (freqs=%u)\n",
			    (unsigned)n_freqs);
			bridge.scan_hw = 0;
			(void)bridge.ops->filter(bridge.drv,
			    WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC, 0);
			bridge.filter_clears++;
			return -EIO;
		}
	}
	if (!bridge.scan_hw && bridge.ops->sw_scan_start != NULL &&
	    bridge.ops->sw_scan_start(bridge.drv) != 0) {
		(void)bridge.ops->filter(bridge.drv,
		    WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC, 0);
		bridge.filter_clears++;
		return -EIO;
	}
	/*
	 * The bookkeeping flag goes up only once the transition below has
	 * finished.  The dwell pump gates on it, and the transition can take
	 * longer than one dwell period (it arms the first channel and runs that
	 * channel's probe through the whole transmit chain).  A flag set before
	 * the transition lets the pump dwell on a stack that has not entered its
	 * scan state yet: the dwell's entry check then reads "the stack left the
	 * scan" and closes the sweep - the window is destroyed before it opens,
	 * which is the intermittent `chans=0`/`entered=0` sweep measured on both
	 * driver lines.
	 */
	bridge.scan_starts++;
	bridge.scan_close = WLAN_BRIDGE_SCAN_CLOSE_NONE;
	bridge.bss_seen_n = 0;
	bridge.bss_seen_ovf = 0;
	bridge.scan_nodes_max = bridge_scan_nodes_count();
	/*
	 * Every arm, not just the first: a sweep armed while a join is in
	 * flight is what drags the stack out of AUTH (the transition below
	 * forces S_SCAN), and the join's own answer then lands in the wrong
	 * state.  With the arm and the state transition both stamped, "the
	 * supplicant re-scanned before the answer" is a line, not a theory.
	 */
	wlan_osal_log("wlan: scan arm #%u state=%d flags=0x%x scanning=%d\n",
	    bridge.scan_starts, (int)bridge.ic->ic_state,
	    (unsigned)bridge.ic->ic_flags, bridge.scanning);
	wlan_port_serializer_lock();
	/*
	 * Re-arm the channel list: in MANUAL roaming the stack stays in its
	 * SCAN state after a sweep ends (end_scan returns without joining), so
	 * the next scan_start is a SCAN -> SCAN transition - which only probes
	 * the current channel unless someone has refilled ic_chan_scan. The
	 * mask is consumed channel by channel during the walk, so every sweep
	 * needs its own copy (the predecessor's bridge did the same).
	 */
	memcpy(bridge.ic->ic_chan_scan, bridge.ic->ic_chan_active,
	    sizeof(bridge.ic->ic_chan_scan));
	/*
	 * Re-arm the scan flags with the same pair begin_scan() would set:
	 * end_scan() cleared both at the last sweep's close, and a SCAN ->
	 * SCAN re-entry emits no probe unless F_ASCAN is back (the re-entry
	 * case gates probe_curchan on it), while F_SCAN is what the stack's
	 * own rx-side helpers consult for "am I scanning". Without this every
	 * sweep after the first is a silent passive walk over a stale table
	 * (measured on the board: mgt frames frozen while the supplicant's
	 * scans kept "completing").
	 */
	bridge.ic->ic_flags |= IEEE80211_F_SCAN | IEEE80211_F_ASCAN;
	ieee80211_new_state(bridge.ic, IEEE80211_S_SCAN, -1);
	/*
	 * A sweep is only real if the stack ended up in its scan state.  A
	 * transition that returns without it (the state machine refused, or the
	 * walk ran out before the first arm) leaves the dwell's first entry
	 * check to close the sweep immediately - the case then reads an empty
	 * table with nothing naming the cause.  Recorded here, and reported by
	 * the case's own line, so a zero-length window is never silent.
	 */
	if (bridge.ic->ic_state == IEEE80211_S_SCAN) {
		bridge.scan_entered++;
	} else {
		bridge.scan_close = WLAN_BRIDGE_SCAN_CLOSE_NOENTRY;
		wlan_osal_log("wlan: scan start did not enter S_SCAN (state=%d "
		    "flags=0x%x) - the sweep has no window\n",
		    (int)bridge.ic->ic_state, (unsigned)bridge.ic->ic_flags);
	}
	/* the window is open (or the sweep is a named no-entry one): from here the
	 * dwell pump owns it */
	bridge.scanning = 1;
	wlan_port_serializer_unlock();
	return 0;
}

/*
 * The dwell ends: hand the stack to the next channel. ieee80211_next_scan()
 * returns 1 when it armed another channel and 0 when the sweep ended (it calls
 * end_scan itself); on the end the receive filter goes back and the driver is
 * told the scan completed.
 *
 * No tuning happens here: arming a channel runs the state transition (and with
 * it the probe request) synchronously inside ieee80211_next_scan, so the tuning
 * must have happened before that - the transition wrapper below owns it. The
 * arm-time tune this function used to do was after the probe had already left,
 * which on a real chip means every probe went out on the previous channel.
 */
/*
 * Close a sweep: the filter goes back, the driver is told the scan completed, and
 * the reason is recorded so a case that found nothing can name it.  Both closers
 * (the stack leaving S_SCAN on its own, and the walk running out of channels) go
 * through here - they used to be two copies of the same four calls.
 */
static void
bridge_scan_close(unsigned int reason)
{
	bridge.scanning = 0;
	bridge.scan_completes++;
	bridge.scan_close = reason;
	if (bridge.scan_hw) {
		/*
		 * A hardware sweep: the device owns it. When the close came from
		 * the device (WLAN_BRIDGE_SCAN_CLOSE_HW) it has already stopped
		 * and released its own request; any other close means the stack
		 * left the scan on its own, so the device has to be told to stop
		 * or it keeps dwelling on a radio nobody is collecting from.
		 */
		if (reason != WLAN_BRIDGE_SCAN_CLOSE_HW &&
		    bridge.ops->hw_scan_cancel != NULL)
			bridge.ops->hw_scan_cancel(bridge.drv);
		bridge.scan_hw = 0;
	} else if (bridge.ops->sw_scan_complete != NULL) {
		bridge.ops->sw_scan_complete(bridge.drv);
	}
	(void)bridge.ops->filter(bridge.drv,
	    WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC, 0);
	bridge.filter_clears++;
	/*
	 * Every close, not just the first.  During a join that oscillates the
	 * sweep is closed and re-armed once per supplicant retry, and the
	 * filter going back to narrow between the arms is exactly the window
	 * an answer can be lost in - so each close is a line, paired with its
	 * arm above.
	 */
	wlan_osal_log("wlan: sweep close #%u reason=%s entered=%u state=%d "
	    "flags=0x%x starts=%u\n", bridge.scan_completes,
	    reason == WLAN_BRIDGE_SCAN_CLOSE_WALK ? "walk-end" :
	    reason == WLAN_BRIDGE_SCAN_CLOSE_STATE ? "stack-left-scan" :
	    reason == WLAN_BRIDGE_SCAN_CLOSE_NOENTRY ? "no-entry" :
	    reason == WLAN_BRIDGE_SCAN_CLOSE_HW ? "hw-done" : "other",
	    bridge.scan_entered,
	    bridge.ic != NULL ? (int)bridge.ic->ic_state : -1,
	    bridge.ic != NULL ? (unsigned)bridge.ic->ic_flags : 0u,
	    bridge.scan_starts);
}

/*
 * The device reported its hardware sweep finished (the substitute forwards the
 * driver's ieee80211_scan_completed here). This is the hw route's end-of-sweep
 * signal: nothing on this side walks a channel list for a firmware sweep, so
 * without it the window would stay open until the stack happened to leave the
 * scan state. A completion with no sweep armed is not an error - the device may
 * report a sweep this side already closed - so it is ignored.
 *
 * The stack's own end-of-scan runs here too. On the walk route the sweep ends
 * inside ieee80211_next_scan, whose last step is ieee80211_end_scan - that is
 * where the interface clears its scan flags and, through
 * ieee80211_notify_scan_done, tells the port the results are ready. The device
 * route bypasses the walk entirely, so without this call the port half of that
 * contract never fires: a consumer that scans through the interface (the
 * supplicant's scan request) waits for its own abort timer instead of the
 * completion - measured on the board as a ~60 s scan cycle that made the join
 * miss its window. Manual roaming (the mode every supplicant-driven connect
 * runs in) makes end_scan stop after the notification, so the candidate choice
 * stays with the consumer.
 */
void
wlan_bridge_scan_hw_complete(void)
{

	if (!bridge.attached || !bridge.scanning)
		return;
	bridge_scan_close(WLAN_BRIDGE_SCAN_CLOSE_HW);
	wlan_port_serializer_lock();
	if (bridge.ic != NULL && bridge.ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_end_scan(bridge.ic);
	wlan_port_serializer_unlock();
}

int
wlan_bridge_scan_dwell(void)
{
	int more;

	if (!bridge.attached || !bridge.scanning)
		return -EINVAL;
	/*
	 * A hardware sweep has no walk to dwell: the device is tuning and
	 * listening on its own, and calling next_scan here would tune the radio
	 * out from under it (the request's channel list is the device's, not
	 * this walk's). What still matters is the state check: if the stack left
	 * its scan world on its own, the window has to close and the device has
	 * to be told to stop, which is the same close the software path does.
	 */
	if (bridge.scan_hw) {
		wlan_port_serializer_lock();
		if (bridge.ic->ic_state != IEEE80211_S_SCAN) {
			wlan_port_serializer_unlock();
			bridge_scan_close(WLAN_BRIDGE_SCAN_CLOSE_STATE);
			return 0;
		}
		wlan_port_serializer_unlock();
		return 1;
	}
	/*
	 * The state decision and the walk are one critical section.
	 *
	 * The stack may have left its scan world on its own (the supplicant cancels
	 * the sweep and joins: ieee80211_cancel_scan runs inside sta_join's
	 * transition, and sta_join runs under this same lock).  Dwelling then would
	 * drag it back into SCAN through next_scan's own transition - measured on
	 * the board as a join that oscillated SCAN/AUTH and had every auth response
	 * discarded ("bad state"), because the stack was no longer in AUTH when the
	 * answer arrived.
	 *
	 * Reading that state *outside* the lock leaves a window: a join can enter
	 * AUTH between the read and the walk, and the walk then drags a joining
	 * stack back to SCAN - the join's request goes out on a channel the pump
	 * chose and the AP's answer lands in the wrong state.  Both the decision
	 * and the walk happen under the lock here.
	 */
	wlan_port_serializer_lock();
	if (bridge.ic->ic_state != IEEE80211_S_SCAN) {
		wlan_port_serializer_unlock();
		bridge_scan_close(WLAN_BRIDGE_SCAN_CLOSE_STATE);
		return 0;
	}
	/*
	 * next_scan re-enters the stack's SCAN state (tuning the radio to the next
	 * channel); bridge_start takes no lock itself (the input path already runs
	 * it locked), so the probe transmit this arms cannot recurse into this
	 * lock.
	 */
	more = ieee80211_next_scan(bridge.ic);
	wlan_port_serializer_unlock();
	if (more == 0)
		bridge_scan_close(WLAN_BRIDGE_SCAN_CLOSE_WALK);
	return more;
}

/*
 * Diagnostic: hold the receive filter's promiscuous bit without arming a sweep.
 * A join whose answer only arrives while the bit is up is a chip receive-filter
 * fault - the frame reached the chip and the chip dropped it - while one that
 * never arrives either way is not.  Test-only: no production path calls this,
 * and the scan group owns the bit everywhere else.
 */
int
wlan_bridge_filter_promisc(int on)
{

	if (!bridge.attached || bridge.ops->filter == NULL)
		return -ENODEV;
	if (on)
		bridge.filter_sets++;
	else
		bridge.filter_clears++;
	return bridge.ops->filter(bridge.drv,
	    WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC,
	    on ? WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC : 0);
}

/*
 * Keep the driver's radio on the channel the stack currently sits on. Called
 * from the state transition wrapper before the stack's own handler runs, so the
 * order is always configure-then-use: the sweep's probe request for a channel
 * leaves after the driver was told about that channel, and the join's auth
 * request leaves after the driver moved to the AP's channel (ieee80211_sta_join
 * retargets ic_curchan without touching the driver). The stack owns the
 * decision; the bridge carries it to the driver exactly once per change.
 */
int
wlan_bridge_tune_current(void)
{
	struct ieee80211_channel *chan;
	int rc;

	if (!bridge.attached || bridge.ic == NULL)
		return -ENODEV;
	chan = bridge.ic->ic_curchan;
	if (chan == NULL || bridge.ops->config == NULL)
		return -EINVAL;
	if (chan->ic_freq == bridge.tuned_freq)
		return 0;
	bridge.tune_calls++;
	rc = bridge.ops->config(bridge.drv, WLAN_SEAM_CONFIG_CHANNEL,
	    chan->ic_freq);
	if (rc == 0) {
		bridge.tuned_freq = chan->ic_freq;
	} else {
		/* logged, not swallowed: a refused retune leaves the radio on
		 * the old channel while the stack believes otherwise - every
		 * frame of the exchange that follows leaves on the wrong
		 * channel, and nothing else would say so */
		bridge.tune_fails++;
		wlan_osal_log("wlan: tune %u MHz FAILED (%d); the radio stays "
		    "on %u\n", chan->ic_freq, rc, bridge.tuned_freq);
	}
	return rc;
}

void
wlan_bridge_scan_stats(unsigned int *starts, unsigned int *completes,
    unsigned int *filter_sets, unsigned int *filter_clears)
{

	if (starts != NULL)
		*starts = bridge.scan_starts;
	if (completes != NULL)
		*completes = bridge.scan_completes;
	if (filter_sets != NULL)
		*filter_sets = bridge.filter_sets;
	if (filter_clears != NULL)
		*filter_clears = bridge.filter_clears;
}


/*
 * Is a sweep in flight? The radio's dwell pump asks this: while the stack is
 * sweeping, someone must hand it ieee80211_next_scan() at the dwell period,
 * and the bridge already knows whether one is armed.
 */
int
wlan_bridge_scan_active(void)
{
	/* the stack's own state is the authority: a sweep the stack has left
	 * (a cancelled scan on the way into a join) must not be dwelt */
	return bridge.attached && bridge.scanning && bridge.ic != NULL &&
	    bridge.ic->ic_state == IEEE80211_S_SCAN;
}

/*
 * Does the sweep's bookkeeping still stand (whatever the stack's state)?
 * The dwell pump asks this, NOT scan_active(): a join can flip the stack
 * out of S_SCAN between the pump's check and its dwell call, and a pump
 * that then only idles leaves `scanning` set forever - every later
 * scan_start refuses with -EBUSY and no code path clears it (the dwell's
 * own entry check is the only runtime clearer, and it must be reached to
 * run). The pump therefore dwells while the bookkeeping stands and lets
 * the dwell's entry check close a sweep the stack already left.
 */
int
wlan_bridge_scan_pending(void)
{

	return bridge.attached && bridge.scanning;
}

/*
 * Diagnostic: the imported input path's "auth response in the wrong state"
 * counter. It separates "the answer never reached the stack" from "the answer
 * arrived and was discarded a layer above the radio" in one number.
 */
uint32_t
wlan_bridge_stat_bad_auth(void)
{

	return bridge.attached && bridge.ic != NULL ?
	    bridge.ic->ic_stats.is_rx_bad_auth : 0;
}

/*
 * The 1 Hz stack tick. net80211 runs its management-frame timer and node
 * inactivity timers here: a management frame (the auth request of a join, for
 * one) carries a seconds-countdown, and its expiry is what restarts the scan
 * when a response was lost - the retry the standard's state machine depends
 * on. This port has no callout thread, so someone must call it with the same
 * discipline the imported kernel's if_timer had; the radio pump does, once a
 * second, for every state (the predecessor's worker called it on its own 1 Hz
 * timer from this same position).
 */
void
wlan_bridge_watchdog(void)
{

	if (!bridge.attached || bridge.ic == NULL)
		return;
	wlan_port_serializer_lock();
	ieee80211_watchdog(bridge.ic);
	wlan_port_serializer_unlock();
}
/*
 * Set the interface's desired SSID: what a joiner configures before a sweep,
 * and what the stack's own match then requires a candidate to carry. The
 * scalar face of ic_des_essid/ic_des_esslen (the two fields stay in this
 * world - callers pass bytes, not the com).
 */
int
wlan_bridge_set_ssid(const uint8_t *ssid, size_t len)
{

	if (!bridge.attached || bridge.ic == NULL)
		return -ENODEV;
	if (ssid == NULL || len == 0 || len > IEEE80211_NWID_LEN)
		return -EINVAL;
	memcpy(bridge.ic->ic_des_essid, ssid, len);
	bridge.ic->ic_des_esslen = (u_int8_t)len;
	return 0;
}

/* ------------------------------------------------------------------ */
/* the supplicant-driven connect path                                  */
/* ------------------------------------------------------------------ */

void
wlan_bridge_set_manual(int on)
{

	if (!bridge.attached || bridge.ic == NULL)
		return;
	bridge.ic->ic_roaming = on ? IEEE80211_ROAMING_MANUAL :
	    IEEE80211_ROAMING_AUTO;
}

void
wlan_bridge_set_wpa(int on)
{

	if (!bridge.attached || bridge.ic == NULL)
		return;
	if (on)
		bridge.ic->ic_flags |= IEEE80211_F_WPA1 | IEEE80211_F_WPA2;
	else
		bridge.ic->ic_flags &= ~(unsigned int)
		    (IEEE80211_F_WPA1 | IEEE80211_F_WPA2);
}

/*
 * The HT Capabilities element this port declares, appended to the
 * supplicant's element block in the association request (and the probe
 * request, which appends the same block).  The imported stack has no 11n
 * state of its own, so the capability is stated here rather than derived.
 *
 * One spatial stream, MCS 0-7, 20 MHz only, short GI for 20 MHz, and no
 * aggregation: the seam configures the radio at 20 MHz
 * (NL80211_CHAN_WIDTH_20) and the receive path has no block-ack session, so
 * advertising either would invite frames this port cannot take.
 *
 * The second diagnostic build switch, WLAN_NO_HT, removes the element
 * entirely: the station then associates as a plain 802.11g peer.  It exists
 * so a cross-stack measurement can be taken at a matched link fingerprint
 * (legacy 54, 20 MHz, no HT, no WMM) instead of comparing an HT station to a
 * legacy one; the switch also clears IEEE80211_F_WME in the join below.
 */
#ifndef WLAN_SEAM_BW40
#define WLAN_SEAM_BW40 0
#endif
#ifndef WLAN_NO_HT
#define WLAN_NO_HT 0
#endif
#if !WLAN_NO_HT
static const uint8_t bridge_htcap_ie[28] = {
	45, 26,				/* element id, length */
	0x20 | (WLAN_SEAM_BW40 ? 0x02 : 0x00), 0x00,
					/* HT cap: SGI-20; bit 1 is
					 * Supported Channel Width, which the
					 * WLAN_BW40 build switch sets so the
					 * association asks for 20/40 MHz */
	0x00,				/* A-MPDU: 8 KiB, no min spacing */
	0xff, 0x00, 0x00, 0x00,		/* MCS 0-7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,			/* rx_highest */
	0x01,				/* tx_params: TX set defined */
	0x00, 0x00, 0x00,		/* reserved */
	0x00, 0x00,			/* HT extended capabilities */
	0x00, 0x00, 0x00, 0x00,		/* TX beamforming */
	0x00				/* ASEL */
};
#define BRIDGE_HTCAP_IE_LEN sizeof(bridge_htcap_ie)
#else
/*
 * The element is not compiled in at all, so the append site below has nothing
 * to copy; a zero-length macro keeps that site branch-free.
 */
#define bridge_htcap_ie NULL
#define BRIDGE_HTCAP_IE_LEN 0
#endif

/*
 * The join the supplicant decided on. Everything here is the stack's own
 * documented policy surface (the prior carrier's board-proven associate
 * shape): which candidate, which security state, and then sta_join - the
 * stack's state machine runs auth/assoc from there exactly as it does on the
 * automatic path.
 *
 * Two details are load-bearing:
 *   - ni_authmode is set on ic_bss BEFORE sta_join, because copy_bss()
 *     propagates it into the new bss node inside the join; 802.1X makes RUN
 *     skip the auto-authorize, so the port stays closed until the pairwise
 *     key lands (the 4-way handshake is what opens it).
 *   - the RSN IE rides in ic_opt_ie, which the assoc-request builder appends
 *     verbatim - this is the byte string the AP's 4-way will check itself
 *     against, so it must be the supplicant's own IE, not a rebuilt one.
 */
int
wlan_bridge_join(const uint8_t *ssid, size_t len, const uint8_t *bssid,
    const uint8_t *wpa_ie, size_t ie_len)
{
	struct ieee80211com *ic = bridge.ic;
	struct ieee80211_node *ni = NULL;
	uint8_t *ie_copy = NULL;
	size_t ie_copy_len = 0;
	size_t ht_len = BRIDGE_HTCAP_IE_LEN;
	int ret = 0;

	if (!bridge.attached || ic == NULL)
		return -ENODEV;
	if (ssid == NULL || len == 0 || len > IEEE80211_NWID_LEN)
		return -EINVAL;
	if (ie_len != 0 && (wpa_ie == NULL || ie_len > 256))
		return -EINVAL;
	/*
	 * One allocation carries the supplicant's element block and, unless the
	 * no-HT arm asked for a legacy association, the HT Capabilities element
	 * this port declares (see bridge_htcap_ie).  It is built before the
	 * serializer is taken, and it is built as one block rather than grown
	 * afterwards: the board's heap is busy enough at join time that a second,
	 * larger allocation is not free.
	 */
	if (ie_len + ht_len != 0) {
		ie_copy = wlan_osal_alloc(ie_len + ht_len);
		if (ie_copy == NULL)
			return -ENOMEM;
		if (ie_len != 0)
			memcpy(ie_copy, wpa_ie, ie_len);
		if (ht_len != 0)
			memcpy(ie_copy + ie_len, bridge_htcap_ie, ht_len);
	}
	ie_copy_len = ie_len + ht_len;

	wlan_port_serializer_lock();
	if (bssid != NULL)
		ni = ieee80211_find_node(&ic->ic_scan, bssid);
	if (ni == NULL) {
		struct ieee80211_node *n;

		TAILQ_FOREACH(n, &ic->ic_scan.nt_node, ni_list) {
			if (n->ni_esslen == (int)len &&
			    memcmp(n->ni_essid, ssid, len) == 0) {
				ni = ieee80211_ref_node(n);
				break;
			}
		}
	}
	if (ni == NULL) {
		wlan_port_serializer_unlock();
		wlan_osal_free(ie_copy);
		wlan_osal_log("wlan: join: no scan candidate for the ssid\n");
		return -ENOENT;
	}

	ic->ic_des_esslen = (int)len;
	memset(ic->ic_des_essid, 0, sizeof(ic->ic_des_essid));
	memcpy(ic->ic_des_essid, ssid, len);
	ic->ic_flags &= ~(unsigned int)(IEEE80211_F_WPA1 | IEEE80211_F_WPA2 |
	    IEEE80211_F_DROPUNENC);
	if (ie_len != 0) {
		/* the element id says which generation the supplicant chose */
		ic->ic_flags |= (wpa_ie[0] == 48 /* WLAN_EID_RSN */ ?
		    IEEE80211_F_WPA2 : IEEE80211_F_WPA1);
		ic->ic_flags |= IEEE80211_F_PRIVACY;
	} else if ((ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) != 0) {
		ic->ic_flags |= IEEE80211_F_PRIVACY;
	} else {
		ic->ic_flags &= ~(unsigned int)IEEE80211_F_PRIVACY;
	}
#if WLAN_NO_HT
	/*
	 * The no-HT arm removes both advertisements together, so the station
	 * enters the matched-link baseline as a plain 802.11g peer (no HT, no
	 * WMM).  The flag is cleared rather than left alone because the join
	 * path can run again in one boot (a reconnect after the first
	 * association would otherwise inherit the earlier value).
	 */
	ic->ic_flags &= ~(unsigned int)IEEE80211_F_WME;
#else
	/*
	 * WMM travels with HT: hostapd clears a station's HT flag when the
	 * association request carries no valid WMM element, so an HT-only
	 * request leaves the AP transmitting legacy frames (measured: the AP's
	 * response grew to ten elements and its MCS set to 0xffff, while every
	 * delivered data frame stayed legacy 20 MHz).  The element itself is
	 * net80211's own - the association builder appends it when F_WME is set
	 * and the AP's beacon carried one.
	 */
	ic->ic_flags |= IEEE80211_F_WME;
#endif

	if (ic->ic_opt_ie != NULL) {
		wlan_osal_free(ic->ic_opt_ie);
	}
	ic->ic_opt_ie = ie_copy;
	ic->ic_opt_ie_len = (u_int16_t)ie_copy_len;
	wlan_osal_log("wlan: join opt ie=%u len=%u\n", (unsigned)ie_len,
	    (unsigned)ic->ic_opt_ie_len);
	wlan_osal_log("wlan: join opt ie=%u len=%u\n", (unsigned)ie_len,
	    (unsigned)ic->ic_opt_ie_len);

	if (ni->ni_chan != NULL)
		ic->ic_curchan = ni->ni_chan;
	if (ic->ic_bss != NULL) {
		ic->ic_bss->ni_authmode = ie_len != 0 ?
		    IEEE80211_AUTH_8021X : IEEE80211_AUTH_OPEN;
	}

	/*
	 * sta_join stays inside the serializer: its new_state(S_AUTH) retunes
	 * the radio and queues the auth request, and the dwell pump's
	 * next_scan (which re-enters S_SCAN and retunes to the sweep's next
	 * channel) runs under the same lock - without this, the two can
	 * interleave and the auth request leaves on a channel the pump chose.
	 */
	if (ieee80211_sta_join(ic, ni)) {
		wlan_osal_log("wlan: join %.*s bssid=%02x:%02x:%02x:%02x:%02x:%02x "
		    "%s\n", (int)len, (const char *)ssid, ni->ni_bssid[0],
		    ni->ni_bssid[1], ni->ni_bssid[2], ni->ni_bssid[3],
		    ni->ni_bssid[4], ni->ni_bssid[5],
		    ie_len != 0 ? "rsn" : "open");
	} else {
		ret = -EIO;
	}
	wlan_port_serializer_unlock();
	/* sta_join consumed the reference on success (ic_bss holds it); a
	 * failure leaves it to us */
	if (ret != 0)
		ieee80211_free_node(ni);
	return ret;
}

void
wlan_bridge_disconnect(int reason)
{

	if (!bridge.attached || bridge.ic == NULL)
		return;
	wlan_port_serializer_lock();
	(void)ieee80211_new_state(bridge.ic, IEEE80211_S_INIT, reason);
	wlan_port_serializer_unlock();
}

/*
 * Pull one scan-table row. The iterator runs inside the serializer; the IE
 * buffer is file-scope because the caller consumes the assembled bytes after
 * the lock is released (a stack-frame buffer would be read as garbage - a
 * failure this line measured once).
 */
static struct {
	uint8_t iebuf[320];
} bridge_fetch;

struct bridge_fetch_ctx {
	int idx;
	int found;
	struct wlan_bridge_scan_res *out;
};

static void
bridge_fetch_node(void *arg, struct ieee80211_node *ni)
{
	struct bridge_fetch_ctx *fc = arg;
	struct wlan_bridge_scan_res *o = fc->out;
	uint8_t *ie = bridge_fetch.iebuf;

	/* the iterator walks the whole table: once the wanted row is copied,
	 * later nodes must not overwrite it */
	if (fc->found)
		return;
	if (fc->idx != 0) {
		fc->idx--;
		return;
	}
	fc->found = 1;
	memset(o, 0, sizeof(*o));
	memcpy(o->bssid, ni->ni_bssid, 6);
	o->ssid_len = (size_t)ni->ni_esslen;
	if (o->ssid_len > 32)
		o->ssid_len = 32;
	memcpy(o->ssid, ni->ni_essid, o->ssid_len);
	o->freq = ni->ni_chan != NULL ? ni->ni_chan->ic_freq : 0;
	o->beacon_int = (uint16_t)ni->ni_intval;
	o->caps = (uint16_t)ni->ni_capinfo;
	o->level = (int)ni->ni_rssi;
	if (ni->ni_rates.rs_nrates != 0) {
		size_t rn = ni->ni_rates.rs_nrates > 16 ?
		    16 : (size_t)ni->ni_rates.rs_nrates;

		ie[0] = 1;	/* rates element id */
		ie[1] = (uint8_t)rn;
		memcpy(ie + 2, ni->ni_rates.rs_rates, rn);
		ie += 2 + rn;
	}
	if (ni->ni_wpa_ie != NULL) {
		size_t n = 2 + ni->ni_wpa_ie[1];

		if ((size_t)(ie - bridge_fetch.iebuf) + n >
		    sizeof(bridge_fetch.iebuf)) {
			n = sizeof(bridge_fetch.iebuf) -
			    (size_t)(ie - bridge_fetch.iebuf);
		}
		memcpy(ie, ni->ni_wpa_ie, n);
		ie += n;
	}
	o->ies = bridge_fetch.iebuf;
	o->ies_len = (size_t)(ie - bridge_fetch.iebuf);
}

int
wlan_bridge_scan_fetch(int idx, struct wlan_bridge_scan_res *out)
{
	struct bridge_fetch_ctx fc;

	if (!bridge.attached || bridge.ic == NULL || out == NULL || idx < 0)
		return -EINVAL;
	fc.idx = idx;
	fc.found = 0;
	fc.out = out;
	wlan_port_serializer_lock();
	ieee80211_iterate_nodes(&bridge.ic->ic_scan, bridge_fetch_node, &fc);
	wlan_port_serializer_unlock();
	return fc.found ? 0 : -ENOENT;
}

/*
 * The two legs of one key install. The stack side mirrors what the crypto
 * layer's own callers do (newkey attaches the cipher template and allocates
 * the slot, setkey validates and records); the driver side is the seam's
 * CAM write. A pairwise success also authorizes the node: the 802.1X gate
 * from the join is opened exactly here, which is why an aborted handshake
 * leaves the port closed.
 */
int
wlan_bridge_key_install(int pairwise, int key_idx, int set_tx, int cipher,
    const uint8_t *key, size_t key_len)
{
	struct ieee80211com *ic = bridge.ic;
	struct ieee80211_key *wk;
	uint8_t zero_mac[IEEE80211_ADDR_LEN] = { 0 };
	int stack_cipher;
	int rc = -1;

	if (!bridge.attached || ic == NULL || ic->ic_bss == NULL)
		return -ENODEV;
	if (key == NULL || key_len == 0 || key_len > IEEE80211_KEYBUF_SIZE)
		return -EINVAL;

	switch (cipher) {
	case WLAN_SEAM_CIPHER_CCMP:
		stack_cipher = IEEE80211_CIPHER_AES_CCM;
		break;
	default:
		wlan_osal_log("wlan: unsupported key cipher %d\n", cipher);
		return -EINVAL;
	}

	if (!pairwise && (key_idx < 0 || key_idx >= IEEE80211_WEP_NKID))
		return -EINVAL;

	wlan_port_serializer_lock();
	ieee80211_key_update_begin(ic);
	wk = pairwise ? &ic->ic_bss->ni_ucastkey : &ic->ic_nw_keys[key_idx];
	if (ieee80211_crypto_newkey(ic, stack_cipher,
	    IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV |
	    (pairwise ? 0 : IEEE80211_KEY_GROUP), wk)) {
		wk->wk_keylen = (u_int8_t)key_len;
		wk->wk_keyrsc = 0;
		wk->wk_keytsc = 0;
		memset(wk->wk_key, 0, sizeof(wk->wk_key));
		memcpy(wk->wk_key, key, key_len);
		if (ieee80211_crypto_setkey(ic, wk, pairwise ?
		    ic->ic_bss->ni_macaddr : zero_mac)) {
			rc = 0;
			if (!pairwise && set_tx)
				ic->ic_def_txkey = (ieee80211_keyix)key_idx;
			if (pairwise)
				ieee80211_node_authorize(ic->ic_bss);
		}
	}
	ieee80211_key_update_end(ic);
	wlan_port_serializer_unlock();

	if (rc != 0) {
		wlan_osal_log("wlan: key %s idx %d: the stack refused it\n",
		    pairwise ? "pair" : "group", key_idx);
		return rc;
	}

	/*
	 * The driver's key write belongs to keys the hardware is going to use.
	 * This port never advertises a hardware cipher, so the stack's key comes
	 * back software-crypto, and net80211's own model keeps such a key out of
	 * the driver (FreeBSD calls the driver's key hook only for hardware
	 * keys). Handing it over anyway turns the chip's receive decryption on:
	 * a protected frame then arrives already decrypted and this stack
	 * decrypts the plaintext a second time, fails the CCMP MIC and drops it -
	 * every protected frame from the peer, the DHCP offers included, dies
	 * there while the management path keeps working. Measured on the board:
	 * the count of chip-decrypted receive frames and the MIC-failure count
	 * rose in lockstep (32/32, 71/71, 89/89, 113/113) with no lease.
	 */
	if ((wk->wk_flags & IEEE80211_KEY_SWCRYPT) != 0) {
		wlan_osal_log("wlan: key %s idx %d len %u set_tx %d: stack-side "
		    "only (software crypto; the chip is not asked to decrypt)\n",
		    pairwise ? "pair" : "group", key_idx, (unsigned)key_len,
		    set_tx);
		return 0;
	}

	/* the driver's CAM write: same seam the clear path uses */
	rc = wlan_bridge_key_set(pairwise ? 1 : 0, key_idx,
	    WLAN_SEAM_CIPHER_CCMP, key, key_len);
	wlan_osal_log("wlan: key %s idx %d len %u set_tx %d -> %d\n",
	    pairwise ? "pair" : "group", key_idx, (unsigned)key_len, set_tx, rc);
	return rc;
}

/*
 * Walk the scan table, handing each candidate to the callback as scalars
 * (bytes and numbers only - the caller may live in the other world). Returns
 * the number of candidates seen.
 */
unsigned int
wlan_bridge_scan_walk(void (*cb)(const uint8_t bssid[6], const uint8_t *ssid,
    size_t ssid_len, uint32_t freq, int rssi, void *arg), void *arg)
{
	struct ieee80211com *ic = bridge.ic;
	struct ieee80211_node *ni;
	unsigned int n = 0;

	if (ic == NULL || cb == NULL)
		return 0;
	TAILQ_FOREACH(ni, &ic->ic_scan.nt_node, ni_list) {
		cb(ni->ni_bssid, ni->ni_essid, ni->ni_esslen,
		    ni->ni_chan != NULL ? ni->ni_chan->ic_freq : 0,
		    (int)ni->ni_rssi, arg);
		n++;
	}
	return n;
}

/* the console's own callback: one line per candidate */
static void
bridge_print_bss(const uint8_t bssid[6], const uint8_t *ssid, size_t ssid_len,
    uint32_t freq, int rssi, void *arg)
{
	char essid[IEEE80211_NWID_LEN + 1];
	size_t n = ssid_len;

	(void)arg;
	if (n > IEEE80211_NWID_LEN)
		n = IEEE80211_NWID_LEN;
	memcpy(essid, ssid, n);
	essid[n] = '\0';
	wlan_osal_log("wlan: bss %02x:%02x:%02x:%02x:%02x:%02x ssid=%s "
	    "freq=%u rssi=%d\n", bssid[0], bssid[1], bssid[2], bssid[3],
	    bssid[4], bssid[5], essid, freq, rssi);
}

/* the current state value (the imported numbering), for the console */
int
wlan_bridge_state(void)
{

	return bridge.attached && bridge.ic != NULL ?
	    (int)bridge.ic->ic_state : -1;
}

void
wlan_bridge_stats(struct wlan_bridge_counters *out)
{

	if (out == NULL)
		return;
	out->mgt_tx = bridge.mgt_tx;
	out->data_tx = bridge.data_tx;
	out->rx_frames = bridge.rx_frames;
	out->tx_completes = bridge.tx_completes;
	out->scan_starts = bridge.scan_starts;
	out->scan_completes = bridge.scan_completes;
	out->filter_sets = bridge.filter_sets;
	out->filter_clears = bridge.filter_clears;
	out->rx_beacon = bridge.rx_beacon;
	out->rx_beacon_tgt = bridge.rx_beacon_tgt;
	out->rx_probersp = bridge.rx_probersp;
	out->rx_fcs_bad = bridge.rx_fcs_bad;
	out->oversize_drops = bridge.oversize_drops;
	/* the stack's own discard reasons, read from the stack's counters */
	if (bridge.ic != NULL) {
		out->rx_chanmismatch = bridge.ic->ic_stats.is_rx_chanmismatch;
		out->rx_badchan = bridge.ic->ic_stats.is_rx_badchan;
		out->rx_badbintval = bridge.ic->ic_stats.is_rx_badbintval;
		out->rx_mgtdiscard = bridge.ic->ic_stats.is_rx_mgtdiscard;
		out->rx_wrongbss = bridge.ic->ic_stats.is_rx_wrongbss;
		out->rx_nodealloc = bridge.ic->ic_stats.is_rx_nodealloc;
		out->rx_elem_unknown = bridge.ic->ic_stats.is_rx_elem_unknown;
		out->rx_elem_missing = bridge.ic->ic_stats.is_rx_elem_missing;
		out->rx_elem_toobig = bridge.ic->ic_stats.is_rx_elem_toobig;
		out->rx_elem_toosmall = bridge.ic->ic_stats.is_rx_elem_toosmall;
	}
	out->scan_bss_seen = bridge.bss_seen_n;
	out->scan_bss_seen_ovf = bridge.bss_seen_ovf;
	out->scan_nodes_max = bridge.scan_nodes_max;
	out->scan_nodes_now = bridge_scan_nodes_count();
	out->scan_entered = bridge.scan_entered;
	out->scan_close = bridge.scan_close;
	out->tune_calls = bridge.tune_calls;
	out->tune_fails = bridge.tune_fails;
	out->data_tx_enc = bridge.data_tx_enc;
	out->data_tx_plain = bridge.data_tx_plain;
	out->tx_noencap = bridge.tx_noencap;
	out->rx_data = bridge.rx_data;
	out->rx_data_enc = bridge.rx_data_enc;
	out->rx_data_hwdec = bridge.rx_data_hwdec;
	out->rx_data_hwdec_prot = bridge.rx_data_hwdec_prot;
	/* the stack's own verdicts, read straight off the imported statistics
	 * (the names are the import's: ieee80211_ioctl.h) - they are the only
	 * place a decryption failure becomes visible at all */
	if (bridge.ic != NULL) {
		out->rx_wepfail = bridge.ic->ic_stats.is_rx_wepfail;
		out->rx_ccmpformat = bridge.ic->ic_stats.is_rx_ccmpformat;
		out->rx_ccmpreplay = bridge.ic->ic_stats.is_rx_ccmpreplay;
		out->rx_ccmpmic = bridge.ic->ic_stats.is_rx_ccmpmic;
		out->tx_nodefkey = bridge.ic->ic_stats.is_tx_nodefkey;
		if (bridge.ic->ic_ifp != NULL)
			out->tx_errors = (unsigned int)
			    IF_STATGET(bridge.ic->ic_ifp, if_oerrors);
	}
	out->tuned_freq = bridge.tuned_freq;
	out->attached = bridge.attached;
}

/* ------------------------------------------------------------------ */
/* data path and lifecycle for the presentation layer                  */
/* ------------------------------------------------------------------ */

/*
 * The key path. net80211's crypto policy layer decides which key goes where and
 * the driver programs it; the bridge is the wire between them (the crypto
 * layer's caller is the supplicant bridge of the authentication round, which is
 * why this entry exists now and is exercised by the bridge tests). The cipher
 * travels in the seam's own numbering.
 */
int
wlan_bridge_key_set(int pairwise, int key_idx, int cipher, const uint8_t *key,
    size_t key_len)
{

	if (!bridge.attached || bridge.ops->set_key == NULL)
		return -ENODEV;
	if (key == NULL && cipher != WLAN_SEAM_CIPHER_NONE)
		return -EINVAL;
	return bridge.ops->set_key(bridge.drv, pairwise, key_idx, cipher, key,
	    key_len);
}

int
wlan_bridge_key_clear(int pairwise, int key_idx)
{

	/*
	 * Nothing to clear on the driver side: a software-crypto key never
	 * reaches the chip's key table (see wlan_bridge_key_install), and the
	 * driver's own clear path re-enables its receive decryption engine on
	 * the way past - the same trap, just without a key to decrypt with.
	 */
	if (!bridge.attached || bridge.ops->clear_key == NULL)
		return -ENODEV;
	(void)pairwise;
	(void)key_idx;
	return 0;
}

int
wlan_bridge_tx_raw(const uint8_t *frame, size_t len)
{
	struct ifnet *ifp;
	struct mbuf *m;
	int qerr;

	if (!bridge.attached || frame == NULL)
		return -ENODEV;
	ifp = bridge.ic->ic_ifp;
	if (ifp == NULL)
		return -ENODEV;
	/*
	 * A frame from the presentation layer (already an ethernet frame) enters
	 * the stack through the interface's send queue: net80211's if_start then
	 * encapsulates and encrypts it on the way out. Queueing rather than
	 * handing it to the driver directly keeps one transmit path.
	 */
	m = bridge_mbuf_alloc(frame, len);
	if (m == NULL)
		return -ENOMEM;
	IFQ_ENQUEUE(&ifp->if_snd, m, qerr);
	if (qerr != 0) {
		m_freem(m);
		return -ENOBUFS;
	}
	bridge_start(ifp);
	return (int)len;
}

int
wlan_bridge_up(void)
{

	if (!bridge.attached || bridge.ops->start == NULL)
		return -ENODEV;
	/* Idempotent: the driver's start submits fresh URBs and re-inits its
	 * anchors, so a second call would stack another live set - and more than
	 * one piece of bring-up logic asks for up (the probe path and the
	 * supplicant's ensure_up). The first caller starts it, the rest ride. */
	if (bridge.up)
		return 0;
	if (bridge.ops->start(bridge.drv) != 0)
		return -EIO;
	bridge.up = 1;
	return 0;
}

void
wlan_bridge_down(void)
{

	if (!bridge.attached || !bridge.up || bridge.ops->stop == NULL)
		return;
	(void)bridge.ops->stop(bridge.drv);
	bridge.up = 0;
}

int
wlan_bridge_hwaddr(uint8_t addr[6])
{

	if (!bridge.attached)
		return -ENODEV;
	memcpy(addr, bridge.ic->ic_myaddr, IEEE80211_ADDR_LEN);
	return 0;
}

void *
wlan_bridge_ic(void)
{

	return bridge.attached ? bridge.ic : NULL;
}

void
wlan_bridge_set_ifops(const struct wlan_port_ifops *ops, void *if_priv)
{

	bridge.ifops = ops;
	bridge.if_priv = if_priv;
}

/* ------------------------------------------------------------------ */
/* port.h entry points the bridge implements                           */
/* ------------------------------------------------------------------ */

/*
 * The port's control and diagnostics surface (port/port.h). Each entry names
 * its consumer here rather than staying an unimplemented declaration: the board
 * console and the presentation layer are those consumers, and an entry whose
 * dependency is not up answers a refusal rather than nothing.
 */

int
wlan_port_get_hwaddr(uint8_t addr[6])
{

	return wlan_bridge_hwaddr(addr);
}

int
wlan_port_up(void)
{

	if (!bridge.attached)
		return -1;
	/*
	 * The firmware the driver loads in probe is the check this port can make
	 * from here: the registry is the loader and the blob registers itself at
	 * start-up, so an unresolved name means the image cannot work.  Which
	 * name that is belongs to the image (WLAN_BRIDGE_FIRMWARE_NAME), and it
	 * is named in the log so a board log says which blob was missing.
	 */
	{
		struct wlan_firmware fw;

		if (wlan_port_firmware_get(WLAN_BRIDGE_FIRMWARE_NAME, &fw) != 0)
			wlan_osal_log("wlan: firmware blob %s is not registered\n",
			    WLAN_BRIDGE_FIRMWARE_NAME);
	}
	return wlan_bridge_up();
}

void
wlan_port_deinit(void)
{

	wlan_bridge_down();
	wlan_bridge_detach();
}

int
wlan_port_select(const char *name)
{

	if (name == NULL)
		return -1;
	return strcmp(name, WLAN_BRIDGE_ADAPTER_NAME) == 0 ? 0 : -1;
}

void *
wlan_port_get_ic(void)
{

	return wlan_bridge_ic();
}

int
wlan_port_scan(const uint8_t *ssid, size_t len)
{

	(void)ssid;
	(void)len;	/* the SSID filter arrives with the supplicant round */
	return wlan_bridge_scan_start();
}

int
wlan_port_xmit(const uint8_t *frame, size_t len)
{

	return wlan_bridge_tx_raw(frame, len);
}

void
wlan_port_status_dump(void)
{
	struct wlan_bridge_counters c;

	wlan_bridge_stats(&c);
	wlan_osal_log("wlan: attached=%d mgt_tx=%u data_tx=%u rx=%u tx_done=%u "
	    "scan=%u/%u filter=%u/%u\n", c.attached, c.mgt_tx, c.data_tx,
	    c.rx_frames, c.tx_completes, c.scan_starts, c.scan_completes,
	    c.filter_sets, c.filter_clears);
	wlan_osal_log("wlan: data tx enc=%u plain=%u noenc=%u err=%u | "
	    "rx data=%u enc=%u hwdec=%u/%u | crypto wepfail=%u ccmpfmt=%u "
	    "replay=%u mic=%u nodefkey=%u\n", c.data_tx_enc, c.data_tx_plain,
	    c.tx_noencap, c.tx_errors, c.rx_data, c.rx_data_enc,
	    c.rx_data_hwdec_prot, c.rx_data_hwdec, c.rx_wepfail,
	    c.rx_ccmpformat, c.rx_ccmpreplay,
	    c.rx_ccmpmic, c.tx_nodefkey);
}

void
wlan_port_scan_dump(void)
{

	/*
	 * The scan table is the stack's node table; the walk below is the
	 * net80211-world half of it (this file holds the ic), and the board
	 * console prints through the same callback the cases use.
	 */
	if (!bridge.attached) {
		wlan_osal_log("wlan: no scan table (not attached)\n");
		return;
	}
	wlan_bridge_scan_walk(bridge_print_bss, NULL);
}

int
wlan_port_firmware_get(const char *name, struct wlan_firmware *fw)
{

	if (name == NULL || fw == NULL)
		return -1;
	/*
	 * The firmware registry this project compiles in is the loader. It is a
	 * linuxkpi component, so it cannot be called from this world directly;
	 * the driver half resolves the name and hands back borrowed bytes
	 * through the seam's firmware hook.
	 */
	return wlan_bridge_fw_lookup(name, &fw->data, &fw->size);
}

void
wlan_port_adapter_register(const struct wlan_port_adapter *adapter)
{

	(void)adapter;	/* this port's one adapter is the bridge itself */
}
