/* SPDX-License-Identifier: BSD-2-Clause */
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
	unsigned int rx_mgtdiscard;	/* stack: mgmt frames dropped in the state gate */
	unsigned int rx_wrongbss;	/* stack: frames from a BSSID we are not on */
	unsigned int rx_trace;		/* the per-frame admission trace's budget */
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

	if (len == 0 || len > MCLBYTES)
		return NULL;
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	memcpy(mtod(m, void *), frame, len);
	m->m_len = (int)len;
	m->m_pkthdr.len = (int)len;
	return m;
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
			/* the first association-family frame and the first
			 * probe request go out as one hex line each: the air
			 * audit's raw material (a frame that the chip never
			 * answers is judged from its bytes, not from the
			 * counter that handed it over) */
			static int dumped_join;

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
		if (rc != 0)
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
		    (size_t)enc->m_pkthdr.len, 0) != 0)
			if_statinc(ifp, if_oerrors);
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
			(void)bridge.ops->bssid(bridge.drv, bss->ni_bssid);
		}
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
		} else if (sub == 0x50) {
			bridge.rx_probersp++;
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

			wlan_osal_log("wlan: rx mgt fc=%02x fcs=%d a1=%02x:%02x:"
			    "%02x:%02x:%02x:%02x a2=%02x:%02x:%02x:%02x:%02x:%02x "
			    "len=%u\n", fc0, fcs_bad ? 1 : 0,
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
		    "chan=%d cur=%d state=%d fscan=%u asc=%u scanning=%u\n",
		    frame[0], (unsigned)len, rx_freq, cur, was,
		    (int)ic->ic_state, fscan, asc, bridge.scanning);
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
	if (bridge.ops->sw_scan_start != NULL &&
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
	static unsigned int logged;

	bridge.scanning = 0;
	bridge.scan_completes++;
	bridge.scan_close = reason;
	if (bridge.ops->sw_scan_complete != NULL)
		bridge.ops->sw_scan_complete(bridge.drv);
	(void)bridge.ops->filter(bridge.drv,
	    WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC, 0);
	bridge.filter_clears++;
	if (!logged) {
		logged = 1;
		wlan_osal_log("wlan: sweep closed reason=%s entered=%u state=%d "
		    "flags=0x%x starts=%u completes=%u\n",
		    reason == WLAN_BRIDGE_SCAN_CLOSE_WALK ? "walk-end" :
		    reason == WLAN_BRIDGE_SCAN_CLOSE_STATE ? "stack-left-scan" :
		    "other", bridge.scan_entered,
		    bridge.ic != NULL ? (int)bridge.ic->ic_state : -1,
		    bridge.ic != NULL ? (unsigned)bridge.ic->ic_flags : 0u,
		    bridge.scan_starts, bridge.scan_completes);
	}
}

int
wlan_bridge_scan_dwell(void)
{
	int more;

	if (!bridge.attached || !bridge.scanning)
		return -EINVAL;
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
	int ret = 0;

	if (!bridge.attached || ic == NULL)
		return -ENODEV;
	if (ssid == NULL || len == 0 || len > IEEE80211_NWID_LEN)
		return -EINVAL;
	if (ie_len != 0) {
		if (wpa_ie == NULL || ie_len > 256)
			return -EINVAL;
		ie_copy = wlan_osal_alloc(ie_len);
		if (ie_copy == NULL)
			return -ENOMEM;
		memcpy(ie_copy, wpa_ie, ie_len);
	}

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

	if (ic->ic_opt_ie != NULL) {
		wlan_osal_free(ic->ic_opt_ie);
	}
	ic->ic_opt_ie = ie_copy;
	ic->ic_opt_ie_len = (u_int16_t)ie_len;

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
	/* the stack's own discard reasons, read from the stack's counters */
	if (bridge.ic != NULL) {
		out->rx_chanmismatch = bridge.ic->ic_stats.is_rx_chanmismatch;
		out->rx_badchan = bridge.ic->ic_stats.is_rx_badchan;
		out->rx_badbintval = bridge.ic->ic_stats.is_rx_badbintval;
		out->rx_mgtdiscard = bridge.ic->ic_stats.is_rx_mgtdiscard;
		out->rx_wrongbss = bridge.ic->ic_stats.is_rx_wrongbss;
	}
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
