/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * lkpi_bridge.c - the net80211 side of the mac80211 mapping (S9/rtw88-
 * adapt W3; the prior project's M4a shape ported onto this tree's S8-1
 * compat world).
 *
 * The bridge makes the ic-based vendored net80211 (pre-vap NetBSD stack)
 * drive the verbatim Linux rtw88 through the plain-C vtable from
 * linuxkpi_net80211.h: the net80211 state machine re-enters S_SCAN
 * through the wrapped ic_newstate, the per-channel driver work runs on
 * the single lkpi workqueue thread, the scan dwell is a timer that only
 * flags the worker, and RX frames cross back into net80211 as pool
 * mbufs.
 *
 * W3 scope: scan + beacon/probe-response RX.  Data-plane TX, set_key and
 * anything association-shaped stay unwired (W4/W5).
 *
 * Dual-world note (FR-4): this TU owns the net80211 types and reaches
 * the mac80211 side ONLY through linuxkpi_net80211.h's C ABI and the
 * lkpi OSAL primitives (workqueue/timer/mutex) - no mac80211.h types
 * are visible here, so the two struct universes never share one.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* the net80211 world's prelude and headers come first */
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net80211/ieee80211_netbsd.h>
#include <net80211/ieee80211_var.h>

/* the mac80211-side OSAL shapes + the driver vtable ABI */
#include "linuxkpi_core.h"
#include "linuxkpi_net80211.h"
/* the wedge report the up-timeout path calls (declared in the usb face) */
#include "linuxkpi_usb.h"

/* the bridge's shell-facing API (consumed by port/adapters/rtw88) */
#include "net80211_bridge.h"

/* deferred-work bits, guarded by sc->mtx */
#define	LKPI_NF_NEWSTATE	0x1u
#define	LKPI_NF_SCANNEXT	0x2u
#define	LKPI_NF_WATCHDOG	0x4u

struct lkpi_n80211_softc {
	struct ieee80211com	ic;
	struct ifnet		ifp;
	void			*hw;		/* opaque mac80211 cookie */
	struct lkpi_n80211_drv	drv;

	struct mutex		mtx;		/* flags/nstate/narg guard */
	unsigned int		flags;
	enum ieee80211_state	nstate;
	int			narg;

	int	(*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);

	struct work_struct	work;
	struct work_struct	up_work;
	struct timer_list	dwell_to;
	struct timer_list	wd_to;

	bool			started;
	bool			scanning;
	bool			attached;	/* attach() completed */

	/* RX intake counters (W3 forensics: "table empty" must be
	 * distinguishable from "callbacks never arrived") */
	unsigned int		rx_frames;	/* crossed with a sane length */
	unsigned int		rx_notstarted;
	unsigned int		rx_short;
	unsigned int		rx_dropped;	/* mbuf alloc failures */
	unsigned int		rx_input;	/* handed to ieee80211_input */
	/* mgmt subtype counters (W4 forensics: the vendored discard
	 * paths are all silent - IEEE80211_DEBUG is compiled out - so
	 * "auth resp reached the bridge or not" is the only visible
	 * split between a state-machine bug and an air-side miss) */
	unsigned int		rx_beacon;
	unsigned int		rx_probe_resp;
	unsigned int		rx_auth;
	unsigned int		rx_assoc_resp;
	unsigned int		rx_deauth;
	unsigned int		rx_disassoc;
	/* TX forensics cap: hex the first 8 non-scan mgmt frames (auth
	 * body truth without an air sniffer) */
	unsigned int		tx_hex_n;
};

static struct lkpi_n80211_softc lkpi_n80211_softc;

static int	lkpi_n80211_up(void);
static int	lkpi_n80211_up_seq(void);

/* ------------------------------------------------------------------ */
/* driver-op trampolines                                               */
/* ------------------------------------------------------------------ */

/* Tune to the channel net80211 picked (ic_curchan).  rtw88 config reads
 * the chandef the mac80211 side builds from the same numbers. */
static void
lkpi_n80211_set_channel(struct lkpi_n80211_softc *sc)
{
	struct ieee80211com *ic = &sc->ic;
	int chan = ieee80211_chan2ieee(ic, ic->ic_curchan);

	if (chan < 1 || chan > 14)
		return;
	(void)sc->drv.config(2407 + 5 * chan, chan);
}

/* Move one ic_mgtq frame across to the driver.  Runs on the workqueue
 * thread; the IF_* dequeue already held the serializer (the compat if.h
 * macros lock it around every op). */
static void
lkpi_n80211_tx_mgmt(struct lkpi_n80211_softc *sc, struct mbuf *m)
{
	struct mbuf *n;
	size_t len, nlen;
	uint8_t buf[512];
	uint8_t *out = buf;

	for (n = m, len = 0; n != NULL; n = n->m_next)
		len += n->m_len;
	if (len > sizeof(buf)) {
		m_freem(m);
		if_statinc(&sc->ifp, if_oerrors);
		return;
	}
	for (n = m; n != NULL; n = n->m_next) {
		nlen = n->m_len;
		memcpy(out, mtod(n, void *), nlen);
		out += nlen;
	}
	m_freem(m);

	/* W4 forensics: auth/assoc frames only (scan floods this path
	 * with probe requests).  fc + addresses expose a malformed
	 * builder before the frame is blamed on the air side.
	 * ether_sprintf returns ONE shared static buffer, so the three
	 * addresses in a single printf all show the last value - the
	 * byte-wise hex below is the trustworthy content record. */
	if (sc->ic.ic_state != IEEE80211_S_SCAN) {
		const uint8_t *f = buf;
		size_t i, hl = len < 32 ? len : 32;

		printf("rtw88u: tx mgmt fc=%02x%02x len %zu\n", f[1], f[0],
		    len);
		if (sc->tx_hex_n++ < 8) {
			printf("rtw88u: tx hex:");
			for (i = 0; i < hl; i++)
				printf(" %02x", f[i]);
			printf("\n");
		}
	}

	(void)sc->drv.tx_mgmt(buf, len);
	if_statinc(&sc->ifp, if_opackets);
}

static void
lkpi_n80211_drain_mgtq(struct lkpi_n80211_softc *sc)
{
	struct mbuf *m;

	for (;;) {
		IF_DEQUEUE(&sc->ic.ic_mgtq, m);
		if (m == NULL)
			return;
		lkpi_n80211_tx_mgmt(sc, m);
	}
}

/* ------------------------------------------------------------------ */
/* newstate wrapper (the prior project's board-proven shape)           */
/* ------------------------------------------------------------------ */

/* Runs on the workqueue thread; ic_state still holds the previous state.
 * The vtable ops sleep (USB control traffic) - exactly why the transition
 * was deferred here. */
static void
lkpi_n80211_newstate_cb(struct lkpi_n80211_softc *sc,
    enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = &sc->ic;
	enum ieee80211_state ostate = ic->ic_state;

	switch (nstate) {
	case IEEE80211_S_SCAN:
		/* one channel per pass; the dwell timer hops */
		lkpi_n80211_set_channel(sc);
		if (ostate != IEEE80211_S_SCAN && !sc->scanning) {
			static const uint8_t
			    zero_bss[IEEE80211_ADDR_LEN] = { 0 };

			/* auth/assoc timed out back to scan: drop the
			 * BSS context the firmware still holds */
			(void)sc->drv.bss_info_changed(zero_bss, 0, 0, 0);
			sc->scanning = true;
			sc->drv.sw_scan_start(ic->ic_myaddr);
		}
		/* net80211 bookkeeping: active scans emit probe requests
		 * into ic_mgtq right here */
		(void)sc->sc_newstate(ic, nstate, arg);
		lkpi_n80211_drain_mgtq(sc);
		lkpi_mod_timer(&sc->dwell_to, jiffies + HZ / 5);
		break;

	case IEEE80211_S_INIT:
		if (sc->scanning) {
			sc->scanning = false;
			sc->drv.sw_scan_complete();
		}
		(void)sc->sc_newstate(ic, nstate, arg);
		break;

	case IEEE80211_S_AUTH:
		/* leave the scan world FIRST: rtw88 keeps RF calibration
		 * and the periodic dynamic mechanism parked behind
		 * RTW_FLAG_SCANNING, and only sw_scan_complete clears it
		 * (start/complete stay a pair) */
		if (sc->scanning) {
			sc->scanning = false;
			sc->drv.sw_scan_complete();
		}
		printf("rtw88u: auth seq 1 (scan closed)\n");
		/* retune to the joined BSS's channel BEFORE the auth frame
		 * goes out (the driver's config op is what actually
		 * programs the RF; sta_join already moved ic_curchan) */
		lkpi_n80211_set_channel(sc);
		printf("rtw88u: auth seq 2 (channel set)\n");
		/* hand the AP's bssid to the firmware (PORT_SET_BSSID +
		 * op-chan bookkeeping) before anything goes on air */
		if (ic->ic_bss != NULL)
			(void)sc->drv.bss_info_changed(ic->ic_bss->ni_bssid,
			    0, 0, ic->ic_bss->ni_intval != 0 ?
			    ic->ic_bss->ni_intval : 100);
		printf("rtw88u: auth seq 3 (bss info)\n");
		(void)sc->sc_newstate(ic, nstate, arg);
		printf("rtw88u: auth seq 4 (auth frame out)\n");
		/* upstream mac80211 calls drv.mgd_prepare_tx before every
		 * auth/assoc burst - rtw88 puts the phy calibration there
		 * (rtw_chip_prepare_tx); without it the RF leaves the
		 * scan world uncalibrated and the AP never answers */
		sc->drv.mgd_prepare_tx();
		printf("rtw88u: auth seq 5 (prepare tx)\n");
		lkpi_n80211_drain_mgtq(sc);
		break;

	case IEEE80211_S_ASSOC:
		/* assoc req rides the same mgtq/drain; channel stays on
		 * the joined BSS */
		(void)sc->sc_newstate(ic, nstate, arg);
		sc->drv.mgd_prepare_tx();
		lkpi_n80211_drain_mgtq(sc);
		break;

	case IEEE80211_S_RUN:
		/* association complete: flip the port to MGD_LINKED and
		 * write the AID (net80211 set ni_associd off the assoc
		 * resp); rtw88 hangs its rsvd-page/default-port setup on
		 * BSS_CHANGED_ASSOC */
		if (ic->ic_bss != NULL)
			(void)sc->drv.bss_info_changed(ic->ic_bss->ni_bssid,
			    1, IEEE80211_AID(ic->ic_bss->ni_associd),
			    ic->ic_bss->ni_intval != 0 ?
			    ic->ic_bss->ni_intval : 100);
		(void)sc->sc_newstate(ic, nstate, arg);
		break;

	default:
		/* anything else the state machine drives: forwards to
		 * net80211 untouched */
		(void)sc->sc_newstate(ic, nstate, arg);
		break;
	}
}

static int
lkpi_n80211_newstate(struct ieee80211com *ic, enum ieee80211_state nstate,
    int arg)
{
	struct lkpi_n80211_softc *sc = ic->ic_ifp->if_softc;

	/* Stop the dwell timer when leaving scan.  The first INIT->SCAN
	 * transition re-enters here synchronously (begin_scan ->
	 * next_scan -> newstate(S_SCAN)) before the first dwell has even
	 * started; stopping unconditionally used to cancel that dwell
	 * (prior-line trap). */
	if (nstate != IEEE80211_S_SCAN)
		lkpi_del_timer(&sc->dwell_to);

	lkpi_mutex_lock(&sc->mtx);
	sc->nstate = nstate;
	sc->narg = arg;
	sc->flags |= LKPI_NF_NEWSTATE;
	lkpi_mutex_unlock(&sc->mtx);

	ieee80211_queue_work(sc->hw, &sc->work);
	return 0;
}

/* timer callbacks run in timer context: flag + queue, never sleep */
static void
lkpi_n80211_dwell(struct timer_list *t)
{
	struct lkpi_n80211_softc *sc =
	    container_of(t, struct lkpi_n80211_softc, dwell_to);

	lkpi_mutex_lock(&sc->mtx);
	sc->flags |= LKPI_NF_SCANNEXT;
	lkpi_mutex_unlock(&sc->mtx);
	ieee80211_queue_work(sc->hw, &sc->work);
}

static void
lkpi_n80211_wd_tick(struct timer_list *t)
{
	struct lkpi_n80211_softc *sc =
	    container_of(t, struct lkpi_n80211_softc, wd_to);

	lkpi_mutex_lock(&sc->mtx);
	sc->flags |= LKPI_NF_WATCHDOG;
	lkpi_mutex_unlock(&sc->mtx);
	ieee80211_queue_work(sc->hw, &sc->work);
}

static void
lkpi_n80211_worker(struct work_struct *w)
{
	struct lkpi_n80211_softc *sc =
	    container_of(w, struct lkpi_n80211_softc, work);
	struct ieee80211com *ic = &sc->ic;
	enum ieee80211_state nstate;
	unsigned int flags;
	int narg;

	for (;;) {
		lkpi_mutex_lock(&sc->mtx);
		flags = sc->flags;
		nstate = sc->nstate;
		narg = sc->narg;
		sc->flags = 0;
		lkpi_mutex_unlock(&sc->mtx);
		if ((flags & (LKPI_NF_NEWSTATE | LKPI_NF_SCANNEXT |
		    LKPI_NF_WATCHDOG)) == 0)
			return;

		if ((flags & LKPI_NF_SCANNEXT) != 0 &&
		    (flags & LKPI_NF_NEWSTATE) == 0) {
			/* short hold, no bus inside: next_scan picks the
			 * channel and re-enters newstate (which defers
			 * the driver work back onto this thread) */
			net80211_serializer_lock();
			if (ic->ic_state == IEEE80211_S_SCAN)
				(void)ieee80211_next_scan(ic);
			net80211_serializer_unlock();
		}

		if ((flags & LKPI_NF_NEWSTATE) != 0)
			lkpi_n80211_newstate_cb(sc, nstate, narg);

		if ((flags & LKPI_NF_WATCHDOG) != 0) {
			net80211_serializer_lock();
			ieee80211_watchdog(ic);
			net80211_serializer_unlock();
			lkpi_mod_timer(&sc->wd_to, jiffies + HZ);
		}
	}
}

/* ------------------------------------------------------------------ */
/* ifnet entry points (the wd timer drives ieee80211_watchdog; the     */
/* if_timer face is never serviced here)                               */
/* ------------------------------------------------------------------ */

static void
lkpi_n80211_if_start(struct ifnet *ifp)
{
	struct lkpi_n80211_softc *sc = ifp->if_softc;

	lkpi_n80211_drain_mgtq(sc);
}

static int
lkpi_n80211_if_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
	/* no ifconfig on this image; the wlanrtw shell is the entry */
	(void)ifp;
	(void)cmd;
	(void)data;
	return ENOTTY;
}

static int
lkpi_n80211_if_init(struct ifnet *ifp)
{
	(void)ifp;
	return lkpi_n80211_up();
}

/* ------------------------------------------------------------------ */
/* RX: one air frame in -> pool mbuf -> find_rxnode + input            */
/* ------------------------------------------------------------------ */

void
lkpi_n80211_rx_frame(const void *data, size_t len, int signal_dbm)
{
	struct lkpi_n80211_softc *sc = &lkpi_n80211_softc;
	struct ieee80211com *ic = &sc->ic;
	struct ieee80211_node *ni;
	struct mbuf *m;
	int rssi;

	if (!sc->started) {
		sc->rx_notstarted++;
		return;
	}
	if (len < sizeof(struct ieee80211_frame_min)) {
		sc->rx_short++;
		return;
	}
	sc->rx_frames++;
	/* 802.11 fc: bits 2-3 type (0 = mgmt), bits 4-7 subtype */
	{
		const uint8_t *p = data;
		uint16_t fc = (uint16_t)(p[0] | (p[1] << 8));

		if ((fc & 0x0c) == 0) {
			switch ((fc >> 4) & 0xf) {
			case 0x8:	sc->rx_beacon++;	break;
			case 0x5:	sc->rx_probe_resp++;	break;
			case 0xb:	sc->rx_auth++;		break;
			case 0x1:	sc->rx_assoc_resp++;	break;
			case 0xc:	sc->rx_deauth++;	break;
			case 0xa:	sc->rx_disassoc++;	break;
			}
		}
	}
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL || len > MCLBYTES) {
		if (m != NULL)
			m_freem(m);
		sc->rx_dropped++;
		return;
	}
	/* no MCLGET needed: the pool mbuf owns one 4 KiB cluster outright */
	memcpy(mtod(m, void *), data, len);
	m->m_len = (int)len;
	m->m_pkthdr.len = (int)len;
	m->m_pkthdr.rcvif = &sc->ifp;
	/* rtw88 delivers the FCS; net80211 input strips it on M_HASFCS */
	m->m_flags |= M_HASFCS;

	rssi = signal_dbm + 110;	/* dBm -> net80211 monotonic units */
	net80211_serializer_lock();
	ni = ieee80211_find_rxnode(ic,
	    mtod(m, struct ieee80211_frame_min *));
	(void)ieee80211_input(ic, m, ni, rssi, 0);	/* consumes m */
	ieee80211_free_node(ni);
	net80211_serializer_unlock();
	sc->rx_input++;
}

/* ------------------------------------------------------------------ */
/* bring-up chain (deferred to the wq thread: ops->start's power-on    */
/* chain is too deep for a shell-task stack - prior-line M4 lesson)    */
/* ------------------------------------------------------------------ */

static struct completion lkpi_n80211_up_comp;
static int lkpi_n80211_up_ret;
static bool lkpi_n80211_up_busy;

static void
lkpi_n80211_up_work(struct work_struct *w)
{
	(void)w;
	lkpi_n80211_up_ret = lkpi_n80211_up_seq();
	lkpi_n80211_up_busy = false;
	lkpi_complete(&lkpi_n80211_up_comp);
}

static int
lkpi_n80211_up(void)
{
	int i;

	/* the shell can beat the USB probe: attach (and with it the
	 * INIT_WORK of up_work) lands at ieee80211_register_hw, deep in
	 * the 50s enumeration window - queueing the work before that
	 * made it run with a NULL func and the one-shot busy flag wedged
	 * every later call (board 2026-10-04, "up -> -110" + scan -16) */
	for (i = 0; i < 200 && !lkpi_n80211_softc.attached; i++)
		msleep(100);
	if (!lkpi_n80211_softc.attached)
		return -EAGAIN;
	if (lkpi_n80211_up_busy)
		return -EBUSY;
	lkpi_n80211_up_busy = true;
	lkpi_completion_init(&lkpi_n80211_up_comp);
	ieee80211_queue_work(lkpi_n80211_softc.hw,
	    &lkpi_n80211_softc.up_work);
	/* bounded wait: a wedge inside ops->start must not take the shell
	 * with it; busy stays set on timeout so a second enter cannot
	 * stack on the wedged one */
	if (lkpi_wait_for_completion_timeout(&lkpi_n80211_up_comp,
	    60 * HZ) == 0) {
		printf("lkpi: start timeout (60s), worker inside ops->start; "
		    "dump with usbkpi dump\n");
		usbkpi_wedge_report();
		return -ETIMEDOUT;
	}
	return lkpi_n80211_up_ret;
}

static int
lkpi_n80211_up_seq(void)
{
	struct lkpi_n80211_softc *sc = &lkpi_n80211_softc;
	int ret;

	if (sc->started)
		return 0;

	/* power on + MAC/BB/RF init; sleeps in USB control ops while
	 * holding nothing above the driver */
	ret = sc->drv.start();
	if (ret != 0)
		return ret;
	ret = sc->drv.add_interface(sc->ic.ic_myaddr);
	if (ret != 0) {
		sc->drv.stop();
		return ret;
	}
	sc->started = true;

	sc->ifp.if_flags |= IFF_RUNNING;
	lkpi_mod_timer(&sc->wd_to, jiffies + HZ);
	printf("rtw88u: interface up (STA)\n");

	/* enter scan the net80211 way: the wrapper defers the driver work */
	(void)ieee80211_new_state(&sc->ic, IEEE80211_S_SCAN, -1);
	return 0;
}

/* ------------------------------------------------------------------ */
/* shell-facing API (net80211_bridge.h)                                */
/* ------------------------------------------------------------------ */

int
net80211_bridge_up(void)
{
	return lkpi_n80211_up();
}

int
net80211_bridge_scan(const uint8_t *ssid, size_t len)
{
	struct lkpi_n80211_softc *sc = &lkpi_n80211_softc;
	struct ieee80211com *ic = &sc->ic;
	int ret;

	if (!sc->started) {
		ret = lkpi_n80211_up();
		if (ret != 0)
			return ret;
	}
	if (len > IEEE80211_NWID_LEN)
		return -EINVAL;

	net80211_serializer_lock();
	ic->ic_des_esslen = (int)len;
	memset(ic->ic_des_essid, 0, sizeof(ic->ic_des_essid));
	if (len != 0)
		memcpy(ic->ic_des_essid, ssid, len);
	memcpy(ic->ic_chan_scan, ic->ic_chan_active,
	    sizeof(ic->ic_chan_scan));
	net80211_serializer_unlock();

	(void)ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	return 0;
}

int
net80211_bridge_get_hwaddr(uint8_t addr[6])
{
	memcpy(addr, lkpi_n80211_softc.ic.ic_myaddr, 6);
	return 0;
}

/* D-3 diagnostic (removal = 单播 RX 断链结案): fresh MAC identity for
 * the mesh-stale-session fork.  add_interface / sw_scan_start / the
 * frame builder all read ic_myaddr live, so writing it here (before
 * up) re-identifies the whole stack without touching vendored core. */
void
net80211_bridge_set_mac(const uint8_t addr[6])
{
	net80211_serializer_lock();
	memcpy(lkpi_n80211_softc.ic.ic_myaddr, addr, 6);
	net80211_serializer_unlock();
	printf("lkpi80211: local MAC overridden %02x:%02x:%02x:%02x:%02x:%02x\n",
	    addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

/*
 * W4 connect = policy entry only.  The vendored net80211 state machine
 * owns the whole flow (ROAMING_AUTO): the targeted active scan runs with
 * ic_des_essid set, and ieee80211_end_scan -> match_bss -> sta_join
 * exits the scan and enters AUTH on its own the moment the target shows
 * up in the table.  This entry never joins by hand and never races the
 * worker - it only sets the policy inputs the core documents:
 *   - ic_des_essid: the scan target
 *   - IEEE80211_F_PRIVACY: matched to the target's beacon CAPINFO so
 *     match_bss accepts it (open AP vs WPA-capable AP); the WPA2 4-way
 *     round owns the F_WPA* flags
 */
int
net80211_bridge_connect(const uint8_t *ssid, size_t len)
{
	struct lkpi_n80211_softc *sc = &lkpi_n80211_softc;
	struct ieee80211com *ic = &sc->ic;
	struct ieee80211_node *ni;
	int ret;

	if (!sc->started) {
		ret = lkpi_n80211_up();
		if (ret != 0)
			return ret;
	}
	if (len == 0 || len > IEEE80211_NWID_LEN)
		return -EINVAL;

	net80211_serializer_lock();
	/* policy: privacy bit from the scan table's beacon (0 when the
	 * target has not been heard yet - a fresh sweep will pick it up) */
	ni = NULL;
	TAILQ_FOREACH(ni, &ic->ic_scan.nt_node, ni_list) {
		if (ni->ni_esslen == (int)len &&
		    memcmp(ni->ni_essid, ssid, len) == 0)
			break;
	}
	if (ni != NULL && (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) != 0)
		ic->ic_flags |= IEEE80211_F_PRIVACY;
	else
		ic->ic_flags &= ~IEEE80211_F_PRIVACY;
	ic->ic_flags &= ~(IEEE80211_F_WPA1 | IEEE80211_F_WPA2 |
	    IEEE80211_F_DROPUNENC);
	ic->ic_des_esslen = (int)len;
	memset(ic->ic_des_essid, 0, sizeof(ic->ic_des_essid));
	memcpy(ic->ic_des_essid, ssid, len);
	memcpy(ic->ic_chan_scan, ic->ic_chan_active,
	    sizeof(ic->ic_chan_scan));
	net80211_serializer_unlock();

	(void)ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	return 0;
}

void
net80211_bridge_status(void)
{
	struct lkpi_n80211_softc *sc = &lkpi_n80211_softc;
	struct ieee80211com *ic = &sc->ic;

	printf("rtw88u: state %s ch %u opmode %d rxdrop %u\n",
	    ieee80211_state_name[ic->ic_state],
	    ieee80211_chan2ieee(ic, ic->ic_curchan),
	    (int)ic->ic_opmode, sc->rx_dropped);
	printf("rtw88u: rx frames %u input %u short %u notstarted %u\n",
	    sc->rx_frames, sc->rx_input, sc->rx_short, sc->rx_notstarted);
	printf("rtw88u: rx mgmt bcn %u prsp %u auth %u asrsp %u "
	    "deauth %u disassoc %u\n",
	    sc->rx_beacon, sc->rx_probe_resp, sc->rx_auth,
	    sc->rx_assoc_resp, sc->rx_deauth, sc->rx_disassoc);
	if (ic->ic_bss != NULL && ic->ic_bss->ni_esslen != 0)
		printf("rtw88u: bss %.*s bssid %s ch %d rssi %u\n",
		    ic->ic_bss->ni_esslen, (const char *)ic->ic_bss->ni_essid,
		    ether_sprintf(ic->ic_bss->ni_bssid),
		    ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan),
		    ic->ic_bss->ni_rssi);
}

static void
lkpi_n80211_scan_dump_node(void *arg, struct ieee80211_node *ni)
{
	int ch = ieee80211_chan2ieee(&lkpi_n80211_softc.ic, ni->ni_chan);

	if (ni->ni_esslen != 0)
		printf("rtw88u: scan %.*s ch %d rssi %u bssid %s\n",
		    ni->ni_esslen, (const char *)ni->ni_essid, ch,
		    ni->ni_rssi, ether_sprintf(ni->ni_bssid));
	else
		printf("rtw88u: scan (hidden) ch %d rssi %u bssid %s\n",
		    ch, ni->ni_rssi, ether_sprintf(ni->ni_bssid));
	(void)arg;
}

void
net80211_bridge_scan_dump(void)
{
	net80211_serializer_lock();
	ieee80211_iterate_nodes(&lkpi_n80211_softc.ic.ic_scan,
	    lkpi_n80211_scan_dump_node, NULL);
	net80211_serializer_unlock();
}

/* ------------------------------------------------------------------ */
/* attach (the mac80211 side calls this at ieee80211_register_hw)      */
/* ------------------------------------------------------------------ */

int
lkpi_n80211_attach(struct ieee80211_hw *hw, const uint8_t perm_addr[6],
    const struct lkpi_n80211_drv *drv)
{
	struct lkpi_n80211_softc *sc = &lkpi_n80211_softc;
	struct ieee80211com *ic = &sc->ic;
	struct ifnet *ifp = &sc->ifp;
	int i;

	sc->hw = hw;
	sc->drv = *drv;
	memset(ic, 0, sizeof(*ic));
	memset(ifp, 0, sizeof(*ifp));

	ic->ic_ifp = ifp;
	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_caps =
	    IEEE80211_C_MONITOR |
	    IEEE80211_C_SHPREAMBLE |
	    IEEE80211_C_SHSLOT |
	    IEEE80211_C_WPA;		/* 802.11i, software crypto */
	/* keep the core's own auto sequencing: end_scan matches
	 * ic_des_essid and joins (sta_join -> S_AUTH) on its own - the
	 * bridge only sets policy, the state machine is the judge */
	ic->ic_roaming = IEEE80211_ROAMING_AUTO;

	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
		setbit(ic->ic_chan_avail, i);
		setbit(ic->ic_chan_active, i);
		setbit(ic->ic_chan_scan, i);
	}

	/* rtw88 exported the efuse address through SET_IEEE80211_PERM_ADDR */
	memcpy(ic->ic_myaddr, perm_addr, IEEE80211_ADDR_LEN);

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	memcpy(ifp->if_xname, "rtw88u", IFNAMSIZ);
	ifp->if_start = lkpi_n80211_if_start;
	ifp->if_ioctl = lkpi_n80211_if_ioctl;
	ifp->if_init = lkpi_n80211_if_init;
	IFQ_SET_READY(&ifp->if_snd);

	if_initialize(ifp);
	ieee80211_ifattach(ic);

	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = lkpi_n80211_newstate;

	ieee80211_media_init(ic, ieee80211_media_change,
	    ieee80211_media_status);
	ifp->if_percpuq = if_percpuq_create(ifp);
	if_register(ifp);
	if_set_sadl(ifp, ic->ic_myaddr, IEEE80211_ADDR_LEN, false);
	ieee80211_announce(ic);

	lkpi_mutex_init(&sc->mtx);
	INIT_WORK(&sc->work, lkpi_n80211_worker);
	INIT_WORK(&sc->up_work, lkpi_n80211_up_work);
	lkpi_timer_setup(&sc->dwell_to, lkpi_n80211_dwell);
	lkpi_timer_setup(&sc->wd_to, lkpi_n80211_wd_tick);
	sc->attached = true;

	printf("lkpi80211: net80211 bridge attached (scan world)\n");
	return 0;
}
