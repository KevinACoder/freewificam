/*
 * @file
 * @brief The driver-half seam adapter for the kernelmod line, on kernel mac80211 types.
 *
 * The net80211 side of the bridge (ports/net80211/bridge/wlan_bridge.c) calls
 * the scalar seam vtable (ports/net80211/seam.h); this file implements that
 * vtable by calling the verbatim driver's ieee80211_ops with the kernel's own
 * ieee80211_hw/ieee80211_vif types. It is the kernel-typed counterpart of the
 * RTOS images' wlan_seam_adapter.c, which does the same through the linuxkpi
 * substitute.
 *
 * Two-world rule: this file includes the kernel mac80211 header and the
 * bridge's scalar-only header, and never a net80211 header. Everything that
 * crosses is in seam.h.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include <seam.h>
#include <wlan_bridge.h>

#include "wlan_linux.h"
#include "../netdev/wlan_netdev.h"

/*
 * Transmit diagnosis for the 8851BU data-plane round: the same frame, in the
 * one shape the driver lets us choose at load time.  The driver's inputs were
 * proven byte-identical to the linuxnative line before this knob existed, so
 * the only way to move the frame is to vary what the driver is allowed to
 * derive from it (its rate, its queue, its access category).  Diagnostic
 * build/load only; the production load leaves both at zero.
 *
 *   tx_diag 0  the production shape
 *           1  an explicit legacy rate: mac80211's INJECTED path, CCK1 on 2.4G
 *           2  the after-DTIM (management) queue: hiq, CH9 instead of ACH0
 *           3  a different TID/AC: tx_tid 1/2 -> BK, 4/5 -> VI, 6/7 -> VO
 *   tx_tid     the TID that selects the access category (default 0 = BE)
 */
static int wlan_tx_diag;
static int wlan_tx_tid;
static unsigned int wlan_tx_diag_seen;

module_param_named(tx_diag, wlan_tx_diag, int, 0644);
module_param_named(tx_tid, wlan_tx_tid, int, 0644);

static struct wlan_linux_hw *ctx_hw(struct wlan_seam_drv *drv)
{
	return wlan_hw_of((struct ieee80211_hw *)drv);
}

/*
 * The vif-wide half of a BSS change, the set mac80211 dispatches to
 * vif_cfg_changed instead of link_info_changed (net/mac80211/main.c:327). The
 * constant is private to mac80211, so the port states the same set here.
 */
#define WLAN_VIF_CFG_CHANGED	(BSS_CHANGED_ASSOC | BSS_CHANGED_IDLE |	\
				 BSS_CHANGED_PS | BSS_CHANGED_IBSS |	\
				 BSS_CHANGED_ARP_FILTER | BSS_CHANGED_SSID | \
				 BSS_CHANGED_MLD_VALID_LINKS |		\
				 BSS_CHANGED_MLD_TTLM)

/*
 * One queue object, sized for the driver's private tail. The kernelmod line has
 * no mac80211 TXQ engine (frames reach the driver through .tx), but a driver
 * still walks the interface/station queues at add time to initialise its
 * per-queue state: rtw89 does exactly that (vif->txq at add_interface,
 * sta->txq[] at station add), answering NULL by doing nothing. Providing the
 * objects keeps that bookkeeping on the shape mac80211 gives it without
 * pretending a queue engine exists behind them.
 */
/* the 802.1D priority-to-AC mapping mac80211 keeps privately */
static u8 wlan_ac_from_tid(u8 tid)
{
	static const u8 ac[8] = {
		IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_BK,
		IEEE80211_AC_BE, IEEE80211_AC_VI, IEEE80211_AC_VI,
		IEEE80211_AC_VO, IEEE80211_AC_VO,
	};

	return ac[tid & 7];
}

static struct ieee80211_txq *wlan_txq_alloc(struct wlan_linux_hw *w,
    struct ieee80211_vif *vif, struct ieee80211_sta *sta, u8 tid, u8 ac)
{
	struct ieee80211_txq *txq;

	txq = kzalloc(struct_size(txq, drv_priv, w->hw.txq_data_size),
	    GFP_KERNEL);
	if (txq == NULL) {
		return NULL;
	}
	txq->vif = vif;
	txq->sta = sta;
	txq->tid = tid;
	txq->ac = ac;
	return txq;
}

static void wlan_txq_free(struct ieee80211_txq *txq)
{
	kfree(txq);
}

/*
 * The channel context a connection runs on. mac80211 reserves one when an
 * interface starts using a channel and hands it to the driver before the peer's
 * station is uploaded (ieee80211_vif_use_channel); the driver keeps its own
 * handle in ctx->drv_priv and reads the binding back from the link. The
 * binding happens at the join, not at the scan's per-dwell config: a scan tunes
 * the radio many times and none of those tunings is a connection.
 */
static int wlan_chanctx_use(struct wlan_linux_hw *w, uint32_t freq)
{
	struct ieee80211_chanctx_conf *ctx;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan = NULL;
	int i, rc;

	if (w->vif == NULL || freq == 0) {
		return -EINVAL;
	}
	if (w->chanctx != NULL) {
		return 0;
	}
	sband = w->hw.wiphy->bands[NL80211_BAND_2GHZ];
	if (sband != NULL) {
		for (i = 0; i < sband->n_channels; i++) {
			if (sband->channels[i].center_freq == freq) {
				chan = &sband->channels[i];
				break;
			}
		}
	}
	if (chan == NULL) {
		return -EINVAL;
	}
	ctx = kzalloc(struct_size(ctx, drv_priv, w->hw.chanctx_data_size),
	    GFP_KERNEL);
	if (ctx == NULL) {
		return -ENOMEM;
	}
	ctx->def.chan = chan;
	ctx->def.width = NL80211_CHAN_WIDTH_20;
	ctx->def.center_freq1 = freq;
	ctx->def.center_freq2 = 0;
	ctx->min_def = ctx->def;
	if (w->ops->add_chanctx != NULL) {
		rc = w->ops->add_chanctx(&w->hw, ctx);
		if (rc != 0) {
			kfree(ctx);
			return rc;
		}
	}
	if (w->ops->assign_vif_chanctx != NULL) {
		rc = w->ops->assign_vif_chanctx(&w->hw, w->vif,
		    &w->vif->bss_conf, ctx);
		if (rc != 0) {
			if (w->ops->remove_chanctx != NULL) {
				w->ops->remove_chanctx(&w->hw, ctx);
			}
			kfree(ctx);
			return rc;
		}
	}
	w->chanctx = ctx;
	w->chanctx_assigned = 1;
	w->vif->bss_conf.chanctx_conf = ctx;
	w->vif->bss_conf.chanreq.oper = ctx->def;
	return 0;
}

static void wlan_chanctx_release(struct wlan_linux_hw *w)
{
	if (w->chanctx == NULL) {
		return;
	}
	if (w->chanctx_assigned && w->vif != NULL &&
	    w->ops->unassign_vif_chanctx != NULL) {
		w->ops->unassign_vif_chanctx(&w->hw, w->vif,
		    &w->vif->bss_conf, w->chanctx);
	}
	w->chanctx_assigned = 0;
	if (w->vif != NULL) {
		w->vif->bss_conf.chanctx_conf = NULL;
	}
	if (w->ops->remove_chanctx != NULL) {
		w->ops->remove_chanctx(&w->hw, w->chanctx);
	}
	kfree(w->chanctx);
	w->chanctx = NULL;
}

/*
 * The BSS-change dispatch mac80211 makes: the vif-wide bits go to
 * vif_cfg_changed, everything else to link_info_changed with the vif's own
 * bss_conf as the link, and a driver declaring the older bss_info_changed
 * receives the whole mask as it always did. rtw89 declares the new pair only;
 * rtl8xxxu and rtw88 declare the old callback only, so each driver is called
 * through the family it implements.
 */
static void wlan_bss_changed(struct wlan_linux_hw *w, u64 changed)
{
	struct ieee80211_vif *vif = w->vif;
	u64 cfg, link;

	if (vif == NULL || changed == 0) {
		return;
	}
	cfg = changed & (u64)WLAN_VIF_CFG_CHANGED;
	link = changed & ~(u64)WLAN_VIF_CFG_CHANGED;
	if (cfg != 0 && w->ops->vif_cfg_changed != NULL) {
		w->ops->vif_cfg_changed(&w->hw, vif, cfg);
	}
	if (link != 0 && w->ops->link_info_changed != NULL) {
		w->ops->link_info_changed(&w->hw, vif, &vif->bss_conf, link);
	}
	if (w->ops->bss_info_changed != NULL) {
		w->ops->bss_info_changed(&w->hw, vif, &vif->bss_conf, changed);
	}
	pr_info("wlan: bss changed 0x%llx (vif_cfg=0x%llx link=0x%llx)\n",
	    (unsigned long long)changed, (unsigned long long)cfg,
	    (unsigned long long)link);
}

static int seam_start(struct wlan_seam_drv *drv)
{
	struct wlan_linux_hw *w = ctx_hw(drv);

	if (w->ops->start == NULL) {
		return -EOPNOTSUPP;
	}
	return w->ops->start(&w->hw);
}

static int seam_stop(struct wlan_seam_drv *drv)
{
	struct wlan_linux_hw *w = ctx_hw(drv);

	if (w->ops->stop != NULL) {
		w->ops->stop(&w->hw, false);
	}
	return 0;
}

static int seam_add_interface(struct wlan_seam_drv *drv, const uint8_t mac[6],
    int iftype)
{
	struct wlan_linux_hw *w = ctx_hw(drv);
	struct ieee80211_vif *vif;
	int rc;

	if (w->vif != NULL) {
		return -EBUSY;
	}
	vif = kzalloc(sizeof(*vif) + w->hw.vif_data_size, GFP_KERNEL);
	if (vif == NULL) {
		return -ENOMEM;
	}
	vif->type = NL80211_IFTYPE_STATION;
	memcpy(vif->addr, mac, 6);
	vif->addr_valid = true;
	/*
	 * The default link's bookkeeping, the way the interface sets it up for
	 * every vif: the default link's conf IS the vif's own bss_conf and its
	 * link id is 0 (link.c:ieee80211_link_init). A driver's link-level
	 * handlers read it - rtw89's link_info_changed looks its own per-link
	 * record up by conf->link_id - and valid_links stays 0, which is what a
	 * non-MLO vif carries.
	 */
	vif->bss_conf.link_id = 0;
	vif->link_conf[0] = &vif->bss_conf;
	/*
	 * The default link's own address and its back-pointer to the vif. A
	 * driver that takes the interface reads the link address, not vif->addr:
	 * rtw89 copies it into its per-link record at add_interface and
	 * programs the address CAM from it, so a zero address matches no
	 * unicast frame in hardware.
	 */
	memcpy(vif->bss_conf.addr, mac, 6);
	vif->bss_conf.vif = vif;
	/*
	 * Never leave bss_conf.bssid NULL: the driver's receive path iterates
	 * the active interfaces on every frame and compares that pointer before
	 * it has joined anything (rtw88's rtw_rx_addr_match_iter reads it
	 * unconditionally). Point it at the zeroed BSSID until the join
	 * installs one.
	 */
	vif->bss_conf.bssid = w->bssid;
	/*
	 * The interface's own queue, before the driver hears about the
	 * interface: add_interface initialises its private queue state through
	 * it (rtw89 mac80211.c:201).
	 */
	if (w->vif_txq == NULL) {
		w->vif_txq = wlan_txq_alloc(w, vif, NULL, 0,
		    (u8)IEEE80211_AC_BE);
		if (w->vif_txq == NULL) {
			kfree(vif);
			return -ENOMEM;
		}
	}
	vif->txq = w->vif_txq;
	if (vif->txq_mgmt == NULL) {
		vif->txq_mgmt = wlan_txq_alloc(w, vif, NULL,
		    IEEE80211_NUM_TIDS, (u8)IEEE80211_AC_VO);
		if (vif->txq_mgmt == NULL) {
			kfree(vif);
			return -ENOMEM;
		}
	}
	if (w->ops->add_interface == NULL) {
		w->vif = vif;
		return 0;
	}
	/*
	 * The vif must not be visible to the driver's iterators until
	 * add_interface returns: iwl_mvm_mac_ctxt_add() walks the active
	 * interfaces to assign a MAC id, and with this vif already in the
	 * list it takes the resume path and returns before initialising
	 * time_event_data.id (TE_MAX).  The first mgd_prepare_tx then trips
	 * iwl_mvm_time_event_send_add's WARN and the session protection is
	 * never installed.
	 */
	rc = w->ops->add_interface(&w->hw, vif);
	if (rc != 0) {
		kfree(vif);
		return rc;
	}
	w->vif = vif;
	return 0;
}

static int seam_config(struct wlan_seam_drv *drv, uint32_t changed,
    uint32_t freq)
{
	struct wlan_linux_hw *w = ctx_hw(drv);
	struct ieee80211_hw *hw = &w->hw;
	struct ieee80211_supported_band *sband;
	struct cfg80211_chan_def *chandef = &hw->conf.chandef;
	int i;

	if ((changed & WLAN_SEAM_CONFIG_CHANNEL) == 0 || freq == 0) {
		return 0;
	}
	sband = hw->wiphy->bands[NL80211_BAND_2GHZ];
	if (sband == NULL) {
		return -EINVAL;
	}
	for (i = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].center_freq == freq) {
			chandef->chan = &sband->channels[i];
			break;
		}
	}
	if (chandef->chan == NULL) {
		return -EINVAL;
	}
	chandef->width = NL80211_CHAN_WIDTH_20;
	chandef->center_freq1 = freq;
	chandef->center_freq2 = 0;
	/*
	 * The channel the join will bind its context to. A scan's per-dwell
	 * config overwrites it many times; the context is only reserved at the
	 * join, where the value names the AP's channel.
	 */
	w->tuned_freq = freq;
	w->tuned_hw_value = chandef->chan->hw_value;
	if (w->ops->config != NULL) {
		return w->ops->config(hw, 0, IEEE80211_CONF_CHANGE_CHANNEL);
	}
	return 0;
}

static int seam_filter(struct wlan_seam_drv *drv, uint32_t changed,
    uint32_t total)
{
	struct wlan_linux_hw *w = ctx_hw(drv);
	unsigned int total_flags = total;
	struct net_device *dev = wlan_netdev_get();
	u64 mc = 0;

	/*
	 * mac80211 builds the driver's multicast command through
	 * prepare_multicast() and hands the cookie to configure_filter();
	 * iwl_mvm stores it and later WARNs from recalc_multicast if it is
	 * NULL.  The single netdev's dev->mc is the list mac80211 would walk.
	 */
	if (w->ops->prepare_multicast != NULL && dev != NULL) {
		mc = w->ops->prepare_multicast(&w->hw, &dev->mc);
	}
	if (w->ops->configure_filter != NULL) {
		w->ops->configure_filter(&w->hw, changed, &total_flags, mc);
	}
	return 0;
}

static int seam_scan_start(struct wlan_seam_drv *drv)
{
	struct wlan_linux_hw *w = ctx_hw(drv);

	if (w->vif == NULL || w->ops->sw_scan_start == NULL) {
		return -EOPNOTSUPP;
	}
	w->ops->sw_scan_start(&w->hw, w->vif, w->vif->addr);
	return 0;
}

static void seam_scan_complete(struct wlan_seam_drv *drv)
{
	struct wlan_linux_hw *w = ctx_hw(drv);

	if (w->vif != NULL && w->ops->sw_scan_complete != NULL) {
		w->ops->sw_scan_complete(&w->hw, w->vif);
	}
}

static int seam_has_hw_scan(struct wlan_seam_drv *drv)
{
	struct wlan_linux_hw *w = ctx_hw(drv);

	return w->ops->hw_scan != NULL;
}

/*
 * The probe-request IEs a 2.4 GHz hardware scan carries. mac80211 builds them
 * for the driver (ieee80211_build_preq_ies) and hands the result in the scan
 * request's ies field; rtw89's offload path appends them to the probe template
 * and the firmware sends that frame. The set is the legacy rate pair (supported
 * plus extended supported) - what a 20 MHz, no-HT AP expects in a probe
 * request; a scan that carried none leaves the firmware with a bare header.
 */
static const u8 wlan_probe_ies_2ghz[] = {
	WLAN_EID_SUPP_RATES, 8,
	0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
	WLAN_EID_EXT_SUPP_RATES, 4,
	0x30, 0x48, 0x60, 0x6c,
};

static int seam_hw_scan_start(struct wlan_seam_drv *drv, const uint32_t *freqs,
    size_t n_freqs)
{
	struct wlan_linux_hw *w = ctx_hw(drv);
	struct ieee80211_scan_request *req;
	size_t i, n = 0;
	int rc;

	if (w->vif == NULL || w->ops->hw_scan == NULL) {
		return -EOPNOTSUPP;
	}
	if (w->hw_scanning || w->hw_scan_req != NULL) {
		return -EBUSY;
	}
	if (freqs == NULL || n_freqs == 0) {
		return -EINVAL;
	}
	if (n_freqs > 64) {
		n_freqs = 64;
	}
	req = kzalloc(struct_size(req, req.channels, n_freqs) +
	    sizeof(wlan_probe_ies_2ghz), GFP_KERNEL);
	if (req == NULL) {
		return -ENOMEM;
	}
	req->req.wiphy = w->hw.wiphy;
	req->req.wdev = NULL;
	req->req.n_ssids = 0;
	for (i = 0; i < n_freqs; i++) {
		struct ieee80211_channel *chan;

		chan = ieee80211_get_channel(w->hw.wiphy, (int)freqs[i]);
		if (chan == NULL) {
			continue;
		}
		req->req.channels[n++] = chan;
	}
	if (n == 0) {
		kfree(req);
		return -EINVAL;
	}
	req->req.n_channels = n;
	/*
	 * The built IEs sit after the channel list in the same allocation. The
	 * driver reads them through ies.ies[band] and sizes the probe template's
	 * tailroom from req.ie_len, so both must name the same bytes.
	 */
	{
		u8 *ie = (u8 *)&req->req.channels[n_freqs];

		memcpy(ie, wlan_probe_ies_2ghz, sizeof(wlan_probe_ies_2ghz));
		req->req.ie = ie;
		req->req.ie_len = sizeof(wlan_probe_ies_2ghz);
		req->ies.ies[NL80211_BAND_2GHZ] = ie;
		req->ies.len[NL80211_BAND_2GHZ] =
		    sizeof(wlan_probe_ies_2ghz);
	}
	w->hw_scan_req = req;
	w->hw_scanning = 1;
	rc = w->ops->hw_scan(&w->hw, w->vif, req);
	if (rc != 0) {
		w->hw_scan_req = NULL;
		w->hw_scanning = 0;
		kfree(req);
	}
	return rc;
}

static void seam_hw_scan_cancel(struct wlan_seam_drv *drv)
{
	struct wlan_linux_hw *w = ctx_hw(drv);

	/*
	 * Clear the flag before the driver's cancel: a driver that reports the
	 * completion from inside cancel_hw_scan must not queue a second bridge
	 * notification for a sweep this side is already closing.
	 */
	w->hw_scanning = 0;
	if (w->vif != NULL && w->ops->cancel_hw_scan != NULL) {
		w->ops->cancel_hw_scan(&w->hw, w->vif);
	}
	kfree(w->hw_scan_req);
	w->hw_scan_req = NULL;
}

static int seam_tx(struct wlan_seam_drv *drv, const uint8_t *frame,
    size_t len, int is_mgmt, int hw_crypto, int pairwise, int key_idx)
{
	struct wlan_linux_hw *w = ctx_hw(drv);
	struct ieee80211_hw *hw = &w->hw;
	struct ieee80211_tx_control control = {};
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;

	(void)hw_crypto;
	(void)pairwise;
	(void)key_idx;
	if (w->ops->tx == NULL || frame == NULL || len == 0) {
		return -EINVAL;
	}
	/*
	 * mac80211 calls .mgd_prepare_tx before an auth/assoc exchange; rtw88
	 * uses it to leave deep LPS and notify coex. Without it the chip can be
	 * asleep when the association response arrives (the AP reports the
	 * station associated, the board never sees the reply). The call happens
	 * under the bridge serializer here; the RTOS line measured what that
	 * costs the data path, and this is the join-only shape until the call
	 * is moved out.
	 */
	if (is_mgmt && w->vif != NULL && w->ops->mgd_prepare_tx != NULL) {
		const struct ieee80211_hdr *hdr = (const struct ieee80211_hdr *)frame;
		__le16 fc = hdr->frame_control;

		/*
		 * mac80211 calls mgd_prepare_tx for the frames that need the
		 * session protected: auth, assoc and reassoc.  Probe requests
		 * are sent during a scan, where a time event is already
		 * active; preparing a second one trips iwl_mvm's
		 * te_data->id != TE_MAX WARN.  Deauth/disassoc use
		 * mgd_complete_tx's was_assoc path instead.
		 */
		if (ieee80211_is_auth(fc) || ieee80211_is_assoc_req(fc) ||
		    ieee80211_is_reassoc_req(fc)) {
			struct ieee80211_prep_tx_info info = {
				.subtype = (u16)(fc & IEEE80211_FCTL_STYPE),
				.link_id = 0,
			};

			w->ops->mgd_prepare_tx(hw, w->vif, &info);
		}
	}
	skb = dev_alloc_skb(len + hw->extra_tx_headroom);
	if (skb == NULL) {
		return -ENOMEM;
	}
	skb_reserve(skb, hw->extra_tx_headroom);
	memcpy(skb_put(skb, len), frame, len);
	info = IEEE80211_SKB_CB(skb);
	memset(info, 0, sizeof(*info));
	info->control.vif = w->vif;
	/*
	 * A data frame travels with its station, as mac80211 sends it: rtw89
	 * then builds the descriptor from the station link (rate/bandwidth/
	 * er_cap) instead of the vif's designated link.  The mac_id itself is
	 * the same either way for a station mode vif, so this is not a mac_id
	 * switch - see the long note below.
	 */
	if (!is_mgmt && w->sta != NULL) {
		control.sta = w->sta;
	}

	/*
	 * The queue-selection byte. The bridge's frames come from a L2 world
	 * with no per-socket priority, so the production shape always asks for
	 * best effort; the diagnostic shape can name another TID/AC.
	 */
	skb->priority = (u8)(wlan_tx_tid & 0xf);
	if (!is_mgmt) {
		switch (wlan_tx_diag) {
		case 1:
			/* the driver's INJECTED path: explicit rate, no data
			 * feedback; requires the rate the descriptor is built
			 * from, which the driver reads from rates[0] */
			info->flags |= IEEE80211_TX_CTL_INJECTED;
			info->control.rates[0].idx = 0;
			info->control.rates[0].count = 1;
			break;
		case 2:
			/* the management queue's hiq shape (CH9, port != 0) */
			info->flags |= IEEE80211_TX_CTL_SEND_AFTER_DTIM;
			break;
		default:
			break;
		}
	}
	if (wlan_tx_diag != 0 && !is_mgmt && wlan_tx_diag_seen < 8) {
		wlan_tx_diag_seen++;
		pr_info("wlan: txdiag %d tid=%u len=%zu flags=0x%x\n",
		    wlan_tx_diag, skb->priority, len, info->flags);
	}
	/*
	 * The transmit control block names the station for a data frame, which
	 * is what mac80211 hands the driver (rtw89 reads it for the station
	 * link: rate, bandwidth, er_cap).  The 8851BU round first left it NULL
	 * and later measured the two shapes as equivalent - the note that
	 * matters is why: the station's firmware mac_id is the vif's by design
	 * for a station mode vif (__rtw89_ops_sta_add assigns
	 * rtw89_vif_get_main_macid), so the descriptor's mac_id does not change;
	 * what does change is the station link the descriptor is built from.
	 */
	/*
	 * DIAG (bring-up): which channel does the join's first management frame
	 * go out on?  The stack tunes the radio before it authenticates; if the
	 * tune has not happened yet, the frame leaves on the scan's last dwell
	 * channel and the AP never hears it (an intermittent join).
	 */
	if (is_mgmt && wlan_tx_diag_seen < 6) {
		const struct ieee80211_hdr *hdr = (const struct ieee80211_hdr *)frame;

		wlan_tx_diag_seen++;
		pr_info("wlan: tx mgt fc=%04x len=%zu tuned=%u conf=%u\n",
		    le16_to_cpu(hdr->frame_control), len, w->tuned_freq,
		    w->hw.conf.chandef.chan != NULL ?
			w->hw.conf.chandef.chan->center_freq : 0);
	}
	/*
	 * Drivers with wake_tx_queue (iwlwifi) take every frame through a
	 * TXQ: the queue is allocated by the first wake, and a direct .tx
	 * leaves mvm's mgmt TID queue invalid ("Invalid TXQ id").  The other
	 * lines keep the direct shape they were measured with.
	 */
	if (w->ops->wake_tx_queue != NULL && w->vif != NULL) {
		struct ieee80211_txq *txq = NULL;

		if (is_mgmt && w->sta != NULL) {
			txq = w->sta->txq[IEEE80211_NUM_TIDS];
		} else if (is_mgmt) {
			txq = w->vif->txq_mgmt != NULL ?
			    w->vif->txq_mgmt : w->vif->txq;
		} else if (w->sta != NULL) {
			u8 tid = skb->priority & 0xf;

			txq = w->sta->txq[tid] != NULL ?
			    w->sta->txq[tid] : w->vif->txq;
		} else {
			txq = w->vif->txq;
		}
		if (txq != NULL && wlan_txq_enqueue(txq, skb) == 0) {
			ieee80211_schedule_txq(hw, txq);
			w->ops->wake_tx_queue(hw, txq);
			return 0;
		}
	}
	w->ops->tx(hw, &control, skb);
	return 0;
}

static int seam_set_key(struct wlan_seam_drv *drv, int pairwise, int key_idx,
    int cipher, const uint8_t *key, size_t key_len)
{
	(void)drv;
	(void)pairwise;
	(void)key_idx;
	(void)cipher;
	(void)key;
	(void)key_len;
	return 0;
}

static int seam_clear_key(struct wlan_seam_drv *drv, int pairwise, int key_idx)
{
	(void)drv;
	(void)pairwise;
	(void)key_idx;
	return 0;
}

/*
 * The one peer this line joins: the driver learns about it where the bridge
 * learns the BSSID (before the authentication exchange, the moment mac80211
 * uploads the AP's station), and the association completes it at RUN.
 */
static struct ieee80211_sta *seam_sta_upload(struct wlan_linux_hw *w,
    const u8 *addr)
{
	struct ieee80211_sta *sta;
	unsigned int i;
	int rc;

	if (w->sta != NULL) {
		return w->sta;
	}
	if (w->vif == NULL || addr == NULL) {
		return NULL;
	}
	sta = kzalloc(struct_size(sta, drv_priv, w->hw.sta_data_size),
	    GFP_KERNEL);
	if (sta == NULL) {
		return NULL;
	}
	ether_addr_copy(sta->addr, addr);
	ether_addr_copy(sta->deflink.addr, addr);
	sta->deflink.sta = sta;
	sta->deflink.link_id = 0;
	sta->link[0] = &sta->deflink;
	/*
	 * A station always has at least one spatial stream: drivers encode
	 * "nss - 1" in rate fields, so a zero here underflows (rtw89's rate
	 * setup would turn it into a 3-bit 7).
	 */
	sta->deflink.rx_nss = 1;
	/*
	 * mac80211 leaves SMPS in OFF for an associated station; mvm's
	 * sta_send_to_fw switch WARNs on AUTOMATIC/NUM_MODES (mvm/sta.c:195).
	 */
	sta->deflink.smps_mode = IEEE80211_SMPS_OFF;
	/*
	 * mac80211 gives every station a current-link aggregation record and
	 * points sta->cur at it; iwl_mvm's rate init reads
	 * sta->cur->max_amsdu_len unconditionally (mvm/rs.c:2948).  Without
	 * it the first sta_state upload from the join path NULL-derefs.
	 */
	sta->deflink.agg.max_amsdu_len = 3839;
	sta->cur = &sta->deflink.agg;
	/*
	 * The station's per-TID queues, before the driver hears about the
	 * station: its station setup walks them to initialise the
	 * driver-private queue state (rtw89 mac80211.c:516-517).
	 */
	for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
		w->sta_txq[i] = wlan_txq_alloc(w, w->vif, sta, (u8)i,
		    wlan_ac_from_tid((u8)i));
		if (w->sta_txq[i] == NULL) {
			goto err_free;
		}
		sta->txq[i] = w->sta_txq[i];
	}
	if (ieee80211_hw_check(&w->hw, STA_MMPDU_TXQ)) {
		w->sta_txq[IEEE80211_NUM_TIDS] =
		    wlan_txq_alloc(w, w->vif, sta, IEEE80211_NUM_TIDS,
			(u8)IEEE80211_AC_VO);
		if (w->sta_txq[IEEE80211_NUM_TIDS] == NULL) {
			goto err_free;
		}
		sta->txq[IEEE80211_NUM_TIDS] =
		    w->sta_txq[IEEE80211_NUM_TIDS];
	}
	/*
	 * The upload step. A driver with the new callback is given the
	 * NOTEXIST -> NONE transition mac80211 gives it while it prepares the
	 * connection; the older sta_add spelling is called where the previous
	 * rounds measured it, so the rtl8xxxu and rtw88 lines keep their shape.
	 */
	if (w->ops->sta_state != NULL) {
		rc = w->ops->sta_state(&w->hw, w->vif, sta,
		    IEEE80211_STA_NOTEXIST, IEEE80211_STA_NONE);
		pr_info("wlan: station upload %pM rc=%d\n", addr, rc);
	} else if (w->ops->sta_add != NULL) {
		rc = w->ops->sta_add(&w->hw, w->vif, sta);
	} else {
		rc = 0;
	}
	if (rc != 0) {
		pr_info("wlan: driver refused the peer %pM (%d)\n", addr, rc);
		goto err_free;
	}
	w->sta = sta;
	w->sta_joined = 0;
	w->sta_auth_sent = 0;
	return sta;

err_free:
	for (i = 0; i <= IEEE80211_NUM_TIDS; i++) {
		wlan_txq_free(w->sta_txq[i]);
		w->sta_txq[i] = NULL;
	}
	kfree(sta);
	return NULL;
}

static void seam_sta_drop(struct wlan_linux_hw *w)
{
	struct ieee80211_sta *sta = w->sta;
	unsigned int i;

	if (sta == NULL) {
		return;
	}
	if (w->vif != NULL && w->ops->sta_state != NULL) {
		/*
		 * The ladder back down, in the order the interface runs it:
		 * the association ends (only when it was reached), then the
		 * station is gone.
		 */
		if (w->sta_joined) {
			(void)w->ops->sta_state(&w->hw, w->vif, sta,
			    IEEE80211_STA_ASSOC, IEEE80211_STA_AUTH);
		}
		(void)w->ops->sta_state(&w->hw, w->vif, sta,
		    IEEE80211_STA_NONE, IEEE80211_STA_NOTEXIST);
	} else if (w->vif != NULL && w->ops->sta_remove != NULL) {
		w->ops->sta_remove(&w->hw, w->vif, sta);
	}
	for (i = 0; i <= IEEE80211_NUM_TIDS; i++) {
		wlan_txq_free(w->sta_txq[i]);
		w->sta_txq[i] = NULL;
	}
	kfree(sta);
	w->sta = NULL;
	w->sta_joined = 0;
}

static u32 seam_rates(const u8 *rates, size_t rates_len, u32 *basic)
{
	static const struct {
		u8 half_mbps;
		u8 idx;
	} legacy[] = {
		{ 2, 0 }, { 4, 1 }, { 11, 2 }, { 22, 3 },
		{ 12, 4 }, { 18, 5 }, { 24, 6 }, { 36, 7 },
		{ 48, 8 }, { 72, 9 }, { 96, 10 }, { 108, 11 },
	};
	u32 supp = 0;
	size_t i, j;

	*basic = 0;
	for (i = 0; i < rates_len; i++) {
		for (j = 0; j < ARRAY_SIZE(legacy); j++) {
			if ((rates[i] & 0x7fu) != legacy[j].half_mbps) {
				continue;
			}
			supp |= 1u << legacy[j].idx;
			if ((rates[i] & 0x80u) != 0) {
				*basic |= 1u << legacy[j].idx;
			}
		}
	}
	return supp;
}

static void seam_bss_info(struct wlan_seam_drv *drv, int joined,
    const uint8_t bssid[6], uint32_t freq, uint16_t aid,
    const uint8_t *rates, size_t rates_len)
{
	struct wlan_linux_hw *w = ctx_hw(drv);
	struct ieee80211_vif *vif = w->vif;
	struct ieee80211_sta *sta;
	u32 basic = 0;
	u64 changed;

	(void)freq;
	if (vif == NULL) {
		return;
	}
	if (joined) {
		if (bssid != NULL) {
			memcpy(w->bssid, bssid, 6);
			vif->bss_conf.bssid = w->bssid;
			memcpy(vif->cfg.ap_addr, w->bssid, 6);
		}
		vif->cfg.assoc = true;
		vif->cfg.aid = aid;
		sta = seam_sta_upload(w, vif->bss_conf.bssid);
		if (sta != NULL && !w->sta_joined) {
			sta->aid = aid;
			if (rates != NULL && rates_len != 0) {
				sta->deflink.supp_rates[NL80211_BAND_2GHZ] =
				    seam_rates(rates, rates_len, &basic);
			}
			vif->bss_conf.basic_rates = basic;
			/*
			 * The association step of the station ladder, driven
			 * where the association actually completes. rtw89
			 * defers the chip-side association to the
			 * vif_cfg_changed(ASSOC) below, so this transition is
			 * the bookkeeping half mac80211 performs at the same
			 * moment.
			 */
			if (w->ops->sta_state != NULL) {
				(void)w->ops->sta_state(&w->hw, vif, sta,
				    IEEE80211_STA_AUTH,
				    IEEE80211_STA_ASSOC);
			}
			/*
			 * mac80211's rate-control update for the link. The
			 * driver derives the station's rate configuration from
			 * the link (supported rates, bandwidth, NSS) and pushes
			 * the RA report to the firmware; it also needs the
			 * update mask this call sets. The substitute runs no
			 * rate control of its own, so without this call the
			 * firmware never gets a rate for the station's data
			 * path - the fixed-rate management path keeps working,
			 * data frames are dropped.
			 */
			if (w->ops->link_sta_rc_update != NULL) {
				w->ops->link_sta_rc_update(&w->hw, vif,
				    &sta->deflink,
				    IEEE80211_RC_SUPP_RATES_CHANGED |
				    IEEE80211_RC_BW_CHANGED |
				    IEEE80211_RC_NSS_CHANGED);
			}
			/*
			 * The association notify: mac80211 sends this mask once
			 * the peer is in ASSOC (net/mac80211/util.c:2138), and
			 * it adds BSS_CHANGED_BEACON_INFO *only when a beacon
			 * was already heard* (util.c:2143).  The difference is
			 * load-bearing on this driver: BEACON_INFO makes mvm
			 * enable the firmware's beacon filter
			 * (mvm/mac80211.c:2679), and an enabled filter stops
			 * passing beacons to the host - so sending it at
			 * association means the first beacon never arrives, the
			 * TBTT anchor stays at the scan value, and the firmware
			 * reports every beacon as missed (the flood this port
			 * measured).  The port therefore sends the same mask
			 * mac80211 sends (without BEACON_INFO) and lets the
			 * first real beacon produce that notify through the
			 * bridge's beacon_sync.
			 */
			wlan_bss_changed(w, BSS_CHANGED_ASSOC |
			    BSS_CHANGED_ARP_FILTER | BSS_CHANGED_PS |
			    BSS_CHANGED_IDLE);
			w->sta_joined = 1;
		}
		/*
		 * The EDCA parameters mac80211 installs at association
		 * (net/mac80211/util.c:ieee80211_set_wmm_default calls
		 * drv_conf_tx once per access category). rtw89 stores them per
		 * link and programs the chip's queues from them; without the
		 * calls the data queues carry no EDCA and the firmware never
		 * transmits from them, while the management queue (a separate
		 * path) still works. Values are 802.11-2007 Table 7-37 with the
		 * 2.4 GHz aCWmin of 31 and QoS enabled - the same defaults
		 * mac80211 applies when the AP's WMM element is absent.
		 */
		{
			static const struct {
				u16 txop;
				u16 cw_min;
				u16 cw_max;
				u8 aifs;
			} edca[IEEE80211_NUM_ACS] = {
				[IEEE80211_AC_VO] = { 3264 / 32, 7, 15, 2 },
				[IEEE80211_AC_VI] = { 6016 / 32, 15, 31, 2 },
				[IEEE80211_AC_BE] = { 0, 31, 1023, 3 },
				[IEEE80211_AC_BK] = { 0, 31, 1023, 7 },
			};
			int ac;

			if (w->ops->conf_tx != NULL) {
				for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
					struct ieee80211_tx_queue_params params = {
						.txop = edca[ac].txop,
						.cw_min = edca[ac].cw_min,
						.cw_max = edca[ac].cw_max,
						.aifs = edca[ac].aifs,
					};

					(void)w->ops->conf_tx(&w->hw, vif, 0,
					    ac, &params);
				}
			}
			vif->bss_conf.qos = true;
		}
		/*
		 * The power-save half of the association contract. mac80211
		 * fills vif->cfg.ps and notifies the driver with
		 * BSS_CHANGED_PS when the association changes the PS state;
		 * rtw89's recalc_lps() then drives the firmware's LPS
		 * parameters (the linuxnative line sends them on every
		 * association). The 8851BU round measured the firmware left
		 * in its power-on PS default without this notify: the
		 * management queue still transmitted while the data queues
		 * stayed paused. Whether PS is allowed is the port's policy;
		 * this port runs the station with power save off, which is
		 * what mac80211's station defaults to and what the recalc below
		 * turns into leave-LPS: with PS advertised the firmware parks
		 * its data queues on the DTIM and the frames this line hands
		 * it never leave.
		 */
		vif->cfg.ps = false;
		changed = BSS_CHANGED_ASSOC | BSS_CHANGED_BSSID |
		    BSS_CHANGED_BASIC_RATES | BSS_CHANGED_PS |
		    BSS_CHANGED_ERP_SLOT;
	} else {
		vif->cfg.assoc = false;
		vif->cfg.aid = 0;
		memset(vif->cfg.ap_addr, 0, 6);
		/*
		 * A NULL bssid would fault the driver's receive-path iterator;
		 * the zeroed address is the unassociated answer it compares
		 * against.
		 */
		vif->bss_conf.bssid = w->bssid;
		seam_sta_drop(w);
		wlan_chanctx_release(w);
		changed = BSS_CHANGED_ASSOC;
	}
	wlan_bss_changed(w, changed);
}

static int seam_bssid(struct wlan_seam_drv *drv, const uint8_t bssid[6],
    uint16_t beacon_int, uint8_t dtim_period)
{
	struct wlan_linux_hw *w = ctx_hw(drv);
	struct ieee80211_vif *vif = w->vif;
	struct ieee80211_sta *sta;

	if (vif == NULL) {
		return -EINVAL;
	}
	if (bssid == NULL) {
		vif->bss_conf.bssid = w->bssid;
		seam_sta_drop(w);
		wlan_chanctx_release(w);
		return 0;
	}
	memcpy(w->bssid, bssid, 6);
	vif->bss_conf.bssid = w->bssid;
	vif->bss_conf.beacon_int = beacon_int;
	vif->bss_conf.dtim_period = dtim_period != 0 ? dtim_period : 1;
	/* the two numbers mvm's MAC context copies into bi/dtim_interval, and
	 * the listen interval it copies from the running config - logged
	 * together because a zero in any of them shows up as "every beacon is
	 * missed" rather than as a command failure */
	pr_info("wlan: bssid %pM beacon_int=%u dtim=%u listen=%u\n", w->bssid,
	    vif->bss_conf.beacon_int, vif->bss_conf.dtim_period,
	    w->hw.conf.listen_interval);
	/*
	 * The channel context first, then the peer: mac80211 reserves the
	 * connection's channel before it uploads the AP's station, and a driver
	 * that keeps per-context state refuses the upload without the binding.
	 * Only a driver that declares the channel context ops gets one; the
	 * older lines keep the shape their rounds measured.
	 */
	if (w->ops->add_chanctx != NULL || w->ops->assign_vif_chanctx != NULL) {
		int cr = wlan_chanctx_use(w, w->tuned_freq);

		pr_info("wlan: chanctx use freq=%u rc=%d\n", w->tuned_freq, cr);
		if (cr != 0) {
			pr_info("wlan: channel context refused (%d)\n", cr);
			return cr;
		}
	}
	sta = seam_sta_upload(w, w->bssid);
	if (sta != NULL && !w->sta_auth_sent && w->ops->sta_state != NULL) {
		int rc = w->ops->sta_state(&w->hw, vif, sta,
		    IEEE80211_STA_NONE, IEEE80211_STA_AUTH);

		w->sta_auth_sent = 1;
		pr_info("wlan: station auth-state %pM rc=%d\n", w->bssid, rc);
	}
	/*
	 * mac80211's pre-association notify for a station vif: the BSSID, the
	 * basic rates it just derived and the beacon interval (mlme.c:9779).
	 * The interval is what the firmware's beacon tracker is armed with -
	 * sending only BSSID leaves it counting at the wrong cadence.
	 */
	wlan_bss_changed(w, BSS_CHANGED_BSSID | BSS_CHANGED_BASIC_RATES |
	    BSS_CHANGED_BEACON_INT);
	return 0;
}

/*
 * The TBTT anchor, as mac80211 keeps it for a driver with
 * IEEE80211_HW_TIMING_BEACON_ONLY (which mvm sets): the AP's beacon timestamp,
 * the device clock at that reception, and the TIM countdown.  mvm folds the
 * three into the MAC context (iwl_mvm_set_fw_dtim_tbtt); all-zero leaves the
 * firmware without a TBTT and it reports every beacon as missed.  The notify
 * is the first beacon of the association only - mac80211's have_beacon branch -
 * and it is what makes the driver rebuild that context.
 */
static int seam_beacon_sync(struct wlan_seam_drv *drv, const uint8_t bssid[6],
    uint64_t tsf, uint32_t device_ts, uint8_t dtim_count, int notify)
{
	struct wlan_linux_hw *w = ctx_hw(drv);
	struct ieee80211_vif *vif = w->vif;

	(void)bssid;
	if (vif == NULL) {
		return -EINVAL;
	}
	vif->bss_conf.sync_tsf = tsf;
	vif->bss_conf.sync_device_ts = device_ts;
	vif->bss_conf.sync_dtim_count = dtim_count;
	if (notify) {
		pr_info("wlan: beacon sync tsf=%llu dev_ts=%u dtim_count=%u\n",
		    (unsigned long long)tsf, device_ts, dtim_count);
		wlan_bss_changed(w, BSS_CHANGED_BEACON_INFO);
	}
	return 0;
}

const struct wlan_seam_drv_ops wlan_seam_ops = {
	.start = seam_start,
	.stop = seam_stop,
	.add_interface = seam_add_interface,
	.config = seam_config,
	.filter = seam_filter,
	.sw_scan_start = seam_scan_start,
	.sw_scan_complete = seam_scan_complete,
	.has_hw_scan = seam_has_hw_scan,
	.hw_scan_start = seam_hw_scan_start,
	.hw_scan_cancel = seam_hw_scan_cancel,
	.tx = seam_tx,
	.set_key = seam_set_key,
	.clear_key = seam_clear_key,
	.bss_info = seam_bss_info,
	.bssid = seam_bssid,
	.beacon_sync = seam_beacon_sync,
};

int wlan_adapter_attach(struct ieee80211_hw *hw)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	uint8_t *mac = hw->wiphy->perm_addr;

	if (w->vif != NULL) {
		return -EBUSY;
	}
	int rc;

	rc = wlan_bridge_attach((struct wlan_seam_drv *)hw, &wlan_seam_ops,
	    mac);
	if (rc != 0) {
		return rc;
	}
	rc = wlan_netdev_create(hw);
	if (rc != 0) {
		wlan_bridge_detach();
		return rc;
	}
	return 0;
}

void wlan_adapter_detach(struct ieee80211_hw *hw)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	/*
	 * Wind the driver's objects down while it is still running: the peer
	 * first, then the interface, and only then the bridge's stop. This is
	 * the order mac80211 uses when the last interface goes away.
	 */
	seam_sta_drop(w);
	wlan_chanctx_release(w);
	wlan_netdev_destroy();
	if (w->vif != NULL) {
		if (w->ops->remove_interface != NULL) {
			w->ops->remove_interface(hw, w->vif);
		}
		kfree(w->vif);
		w->vif = NULL;
	}
	wlan_txq_free(w->vif_txq);
	w->vif_txq = NULL;
	kfree(w->hw_scan_req);
	w->hw_scan_req = NULL;
	wlan_bridge_detach();
}

void wlan_adapter_scan_complete(struct ieee80211_hw *hw)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	w->hw_scanning = 0;
	kfree(w->hw_scan_req);
	w->hw_scan_req = NULL;
	wlan_bridge_scan_hw_complete();
}

/*
 * The software sweep's receive channel. The driver's rx_status carries the
 * channel the hardware was configured for, not the channel the frame arrived
 * on; a frame delivered across a dwell therefore reads as the next channel and
 * the stack's channel-scoped parser drops it (the DS IE says one channel, the
 * status another). Beacons and probe responses carry their operating channel
 * in the DS Parameter IE, so the receive status is corrected from the frame
 * before the bridge sees it. The driver's own scan-offload path makes the same
 * correction from the packet descriptor; this is the software-sweep half.
 */
static void wlan_rx_freq_correct(struct sk_buff *skb,
    struct ieee80211_rx_status *status)
{
	struct ieee80211_mgmt *mgmt;
	const u8 *ie, *end;
	size_t hdr_len;
	u8 chan = 0;

	if (skb->len < 24 || status->band != NL80211_BAND_2GHZ) {
		return;
	}
	mgmt = (struct ieee80211_mgmt *)skb->data;
	if (ieee80211_is_beacon(mgmt->frame_control)) {
		hdr_len = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	} else if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		hdr_len = offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
	} else {
		return;
	}
	if (skb->len <= hdr_len) {
		return;
	}
	ie = skb->data + hdr_len;
	end = skb->data + skb->len;
	while (ie + 2 <= end) {
		u8 id = ie[0];
		u8 len = ie[1];

		if (ie + 2 + len > end) {
			break;
		}
		if (id == 3 && len >= 1) {	/* DS Parameter Set */
			chan = ie[2];
			break;
		}
		if (id == 61 && len >= 2 && chan == 0) {
			chan = ie[2];	/* HT Operation primary channel */
		}
		ie += 2 + len;
	}
	if (chan != 0) {
		u16 freq = ieee80211_channel_to_frequency(chan, status->band);

		if (freq != 0) {
			status->freq = freq;
		}
	}
}

void wlan_adapter_rx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	int fcs_bad = (status->flag & RX_FLAG_FAILED_FCS_CRC) != 0;
	int hw_dec = (status->flag & RX_FLAG_DECRYPTED) != 0;

	wlan_rx_freq_correct(skb, status);
	/* the device clock travels with the frame: the TBTT anchor pairs the
	 * AP's beacon timestamp with the device time of that same reception
	 * (mac80211's sync_device_ts <- bss->device_ts_beacon, which iwlwifi
	 * fills from phy_info->system_timestamp in mvm/rx.c) */
	wlan_bridge_input(skb->data, skb->len, status->signal, fcs_bad, hw_dec,
	    status->freq, status->device_timestamp);
	dev_kfree_skb_any(skb);
}

void wlan_adapter_tx_complete(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	const struct ieee80211_hdr *hdr = (const void *)skb->data;
	static unsigned int logged;

	/*
	 * The first few transmit statuses, with the bits that separate "the
	 * chip heard no ACK" from "the chip never put it on air". The bridge
	 * only counts the completion, so a data plane that fails silently
	 * otherwise leaves no trace here.
	 */
	if (logged < 8) {
		logged++;
		pr_info("wlan: tx done fc=%04x len=%u flags=0x%x ack=%d filtered=%d noack=%d rate=%d a1=%pM\n",
		    le16_to_cpu(hdr->frame_control), skb->len, info->flags,
		    !!(info->flags & IEEE80211_TX_STAT_ACK),
		    !!(info->flags & IEEE80211_TX_STAT_TX_FILTERED),
		    !!(info->flags & IEEE80211_TX_STAT_NOACK_TRANSMITTED),
		    info->status.rates[0].idx, hdr->addr1);
	}
	(void)hw;
	wlan_bridge_tx_complete();
}
