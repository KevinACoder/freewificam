/*
 * @file
 * @brief The mac80211 substitute body for the kernelmod line, with kernel types.
 *
 * This is the kernelmod line's mini-mac80211: the driver-facing API a mac80211 SoftMAC
 * driver calls, implemented over the net80211 bridge instead of over the
 * kernel's mac80211. It is the kernel-typed counterpart of the RTOS images'
 * components/linuxkpi/linuxkpi_80211.c; the call sequence semantics come from
 * that file, the types come from <net/mac80211.h>.
 *
 * Symbol discipline: wlan_ko.ko must leave **no** undefined ieee80211_* (or
 * __ieee80211_*) symbol. If it did, a kernel with MAC80211=y would resolve it
 * against the built-in mac80211 (a silent mixed stack) and a kernel with
 * MAC80211=m would refuse the load. The kernelmod line's debug kernel makes
 * that loud; the nm -u gate in docs/linux-ko.md makes it a build error.
 *
 * Scope: the closure is the one the rtw88 8821CU build leaves undefined
 * (verified with nm -u against the kernel's own rtw88_* modules). Entry points
 * the station scan/join path reaches are live; the data-path queue, BA, WoWLAN,
 * recovery and AP-mode entries are link closure and say so in their bodies.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>

#include "wlan_linux.h"

/*
 * wiphy_new_nm() wants a cfg80211_ops table. The kernelmod line never registers with
 * cfg80211, so the table exists only to satisfy the allocator; every entry is
 * NULL and no cfg80211 path is reachable through it.
 */
static const struct cfg80211_ops wlan_cfg80211_ops;

/*
 * The bridge is a single-adapter component, so the one hw this module hosts is
 * also the answer to the one vif-to-hw query the driver's sta lookup makes
 * (ieee80211_find_sta gets a vif, and the vif has no back-pointer here).
 */
static struct ieee80211_hw *wlan_single_hw;

static void wlan_rx_work(struct work_struct *work);
static void wlan_tx_work(struct work_struct *work);
static void wlan_scan_work(struct work_struct *work);

struct ieee80211_hw *ieee80211_alloc_hw_nm(size_t priv_data_len,
    const struct ieee80211_ops *ops, const char *requested_name)
{
	struct wlan_linux_hw *w;
	struct wiphy *wiphy;

	if (ops == NULL) {
		return NULL;
	}
	/*
	 * The wiphy's private area holds one pointer back to the hw descriptor:
	 * wow.c reaches the hw through wiphy_to_ieee80211_hw(), and this port
	 * does not embed the hw in the wiphy the way mac80211's local does.
	 */
	wiphy = wiphy_new_nm(&wlan_cfg80211_ops, sizeof(struct ieee80211_hw *),
	    requested_name);
	if (wiphy == NULL) {
		return NULL;
	}
	w = kzalloc(sizeof(*w) + priv_data_len, GFP_KERNEL);
	if (w == NULL) {
		wiphy_free(wiphy);
		return NULL;
	}
	w->ops = ops;
	w->hw.wiphy = wiphy;
	w->hw.priv = (void *)(w + 1);
	w->hw.queues = 4;
	*(struct ieee80211_hw **)wiphy_priv(wiphy) = &w->hw;
	wlan_single_hw = &w->hw;
	skb_queue_head_init(&w->rx_q);
	skb_queue_head_init(&w->tx_q);
	INIT_WORK(&w->rx_work, wlan_rx_work);
	INIT_WORK(&w->tx_work, wlan_tx_work);
	INIT_WORK(&w->scan_work, wlan_scan_work);
	/*
	 * Two queues, deliberately: the receive/transmit-completion handoff
	 * enters the net80211 serializer, and a driver work item (watchdog,
	 * C2H, TXQ) must not sit in front of a frame. The scan completion is a
	 * bridge handoff and uses the handoff queue; the driver's own work uses
	 * the second one.
	 */
	w->wq = alloc_ordered_workqueue("wlan_ko", 0);
	w->hw_wq = alloc_ordered_workqueue("wlan_ko_hw", 0);
	if (w->wq == NULL || w->hw_wq == NULL) {
		if (w->wq != NULL) {
			destroy_workqueue(w->wq);
		}
		wlan_single_hw = NULL;
		wiphy_free(wiphy);
		kfree(w);
		return NULL;
	}
	/* the default channel the receive path reads before config() */
	w->chan.center_freq = 2412;
	w->chan.hw_value = 1;
	w->chan.band = NL80211_BAND_2GHZ;
	w->chan.flags = IEEE80211_CHAN_2GHZ;
	w->hw.conf.chandef.chan = &w->chan;
	w->hw.conf.chandef.width = NL80211_CHAN_WIDTH_20;
	w->hw.conf.chandef.center_freq1 = w->chan.center_freq;
	return &w->hw;
}

static void wlan_attach_work_fn(struct work_struct *work)
{
	struct wlan_linux_hw *w = container_of(to_delayed_work(work),
	    struct wlan_linux_hw, attach_work);
	int rc;

	rc = wlan_adapter_attach(&w->hw);
	w->attach_rc = rc;
	if (rc != 0) {
		pr_err("wlan: adapter attach failed rc=%d\n", rc);
	}
}

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	int rc;

	/*
	 * The registration's regulatory announcement. mac80211 registers the
	 * wiphy with cfg80211 at this point, and cfg80211's registration path
	 * hands the last regulatory request to the driver's reg_notifier before
	 * the driver's own register_hw returns
	 * (wiphy_regulatory_register -> wiphy_update_regulatory ->
	 * reg_call_notifier). This line does not register the wiphy with
	 * cfg80211, so the same world request is delivered here: a driver that
	 * seeds its per-chip regulatory record from that notification (rtw89's
	 * regulatory.regd) would otherwise read an empty record while it
	 * finishes its own registration.
	 */
	if (hw->wiphy->reg_notifier != NULL) {
		struct regulatory_request request;

		memset(&request, 0, sizeof(request));
		/* not a registered wiphy: the index is informational */
		request.wiphy_idx = -1;
		request.initiator = NL80211_REGDOM_SET_BY_CORE;
		request.alpha2[0] = '0';
		request.alpha2[1] = '0';
		hw->wiphy->reg_notifier(hw->wiphy, &request);
	}
	/*
	 * The driver calls this from inside its own register_hw and keeps
	 * initialising after it returns (rtw89 registers its BT coexistence
	 * there).  Attaching here would run the driver's start before that
	 * initialisation, so the attach is deferred: see attach_work.
	 */
	INIT_DELAYED_WORK(&w->attach_work, wlan_attach_work_fn);
	w->attach_rc = 0;
	schedule_delayed_work(&w->attach_work, msecs_to_jiffies(200));
	return 0;
}

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	cancel_work_sync(&w->rx_work);
	cancel_work_sync(&w->tx_work);
	cancel_work_sync(&w->scan_work);
	wlan_adapter_detach(hw);
}

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	struct sk_buff *skb;

	if (w->wq != NULL) {
		cancel_work_sync(&w->rx_work);
		cancel_work_sync(&w->tx_work);
		cancel_work_sync(&w->scan_work);
		destroy_workqueue(w->wq);
		w->wq = NULL;
	}
	if (w->hw_wq != NULL) {
		destroy_workqueue(w->hw_wq);
		w->hw_wq = NULL;
	}
	while ((skb = skb_dequeue(&w->rx_q)) != NULL) {
		dev_kfree_skb_any(skb);
	}
	while ((skb = skb_dequeue(&w->tx_q)) != NULL) {
		dev_kfree_skb_any(skb);
	}
	kfree(w->hw_scan_req);
	w->hw_scan_req = NULL;
	kfree(w->vif);
	w->vif = NULL;
	if (wlan_single_hw == hw) {
		wlan_single_hw = NULL;
	}
	wiphy_free(hw->wiphy);
	kfree(w);
}

static void wlan_rx_work(struct work_struct *work)
{
	struct wlan_linux_hw *w = container_of(work, struct wlan_linux_hw, rx_work);
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&w->rx_q)) != NULL) {
		wlan_adapter_rx(&w->hw, skb);
	}
}

static void wlan_tx_work(struct work_struct *work)
{
	struct wlan_linux_hw *w = container_of(work, struct wlan_linux_hw, tx_work);
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&w->tx_q)) != NULL) {
		wlan_adapter_tx_complete(&w->hw, skb);
		dev_kfree_skb_any(skb);
	}
}

static void wlan_scan_work(struct work_struct *work)
{
	struct wlan_linux_hw *w = container_of(work, struct wlan_linux_hw, scan_work);

	wlan_adapter_scan_complete(&w->hw);
}

void ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	/*
	 * The driver that declares RX_INCLUDES_FCS hands up the four FCS bytes
	 * too; mac80211 strips them at the receive entry. The imported
	 * net80211 parser expects the frame without them (its IE walk and
	 * length checks read the trailing four as an element header otherwise).
	 */
	if (test_bit(IEEE80211_HW_RX_INCLUDES_FCS, hw->flags)) {
		if (skb->len < 4) {
			dev_kfree_skb_any(skb);
			return;
		}
		skb_trim(skb, skb->len - 4);
	}
	skb_queue_tail(&w->rx_q, skb);
	queue_work(w->wq, &w->rx_work);
}

/*
 * The NAPI receive entry. rtw89's USB receive path hands frames up through this
 * spelling (core.c:4031, inside local_bh_disable()); the napi argument only
 * names the poll instance that delivered the frame and this line has no NAPI.
 * The frame goes through the same deferred path as ieee80211_rx_irqsafe,
 * including the RX_INCLUDES_FCS trim the 8851BU asks for - one receive entry,
 * not two, so a driver cannot get a path that skips the trim.
 */
void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
    struct sk_buff *skb, struct napi_struct *napi)
{
	(void)sta;
	(void)napi;
	if (hw == NULL || skb == NULL) {
		dev_kfree_skb_any(skb);
		return;
	}
	ieee80211_rx_irqsafe(hw, skb);
}

void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	skb_queue_tail(&w->tx_q, skb);
	queue_work(w->wq, &w->tx_work);
}

/* ------------------------------------------------------------------ */
/* Driver work: mac80211's hardware workqueue                          */
/* ------------------------------------------------------------------ */

void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	if (w->hw_wq != NULL) {
		queue_work(w->hw_wq, work);
	}
}

void ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
    struct delayed_work *dwork, unsigned long delay)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	if (w->hw_wq != NULL) {
		queue_delayed_work(w->hw_wq, dwork, delay);
	}
}

/* ------------------------------------------------------------------ */
/* Scan: a firmware sweep is asked for, the completion is deferred      */
/* ------------------------------------------------------------------ */

void ieee80211_scan_completed(struct ieee80211_hw *hw,
    struct cfg80211_scan_info *info)
{
	struct wlan_linux_hw *w;

	(void)info;
	if (hw == NULL) {
		return;
	}
	w = wlan_hw_of(hw);
	if (!w->hw_scanning) {
		return;
	}
	/*
	 * The driver may call this from its C2H work while it holds its own
	 * mutex. The bridge is entered from the handoff queue instead: the
	 * bridge may call back into the driver (and take that mutex), so the
	 * driver -> bridge direction must never sit on the driver's lock.
	 */
	w->hw_scanning = 0;
	queue_work(w->wq, &w->scan_work);
}

/* ------------------------------------------------------------------ */
/* The object queries the driver makes                                 */
/* ------------------------------------------------------------------ */

void ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw,
    u32 iter_flags,
    void (*iterator)(void *data, u8 *mac, struct ieee80211_vif *vif),
    void *data)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	(void)iter_flags;
	if (w->vif != NULL && iterator != NULL) {
		iterator(data, w->vif->addr, w->vif);
	}
}

void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
    void (*iterator)(void *data, struct ieee80211_sta *sta), void *data)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	if (w->sta != NULL && iterator != NULL) {
		iterator(data, w->sta);
	}
}

/*
 * The station walker the for_each_station() macro is written against
 * (include/net/mac80211.h): return the station after @prev, or NULL at the end.
 * This line carries one station, so the walk is "the station, then NULL".
 * rtw89 walks it to recompute per-station state (core.c:3929, phy.c:787); the
 * table is stable for the connection's lifetime and the accessor takes no lock.
 */
struct ieee80211_sta *__ieee80211_iterate_stations(struct ieee80211_hw *hw,
    struct ieee80211_sta *prev)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	if (w->sta == NULL) {
		return NULL;
	}
	return prev == NULL ? w->sta : NULL;
}

struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif,
    const u8 *addr)
{
	struct wlan_linux_hw *w;

	(void)vif;
	if (wlan_single_hw == NULL || addr == NULL) {
		return NULL;
	}
	w = wlan_hw_of(wlan_single_hw);
	if (w->sta != NULL && ether_addr_equal(w->sta->addr, addr)) {
		return w->sta;
	}
	return NULL;
}

struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw,
    const u8 *addr, const u8 *localaddr)
{
	struct wlan_linux_hw *w;

	(void)localaddr;
	if (hw == NULL || addr == NULL) {
		return NULL;
	}
	w = wlan_hw_of(hw);
	if (w->sta != NULL && ether_addr_equal(w->sta->addr, addr)) {
		return w->sta;
	}
	return NULL;
}

struct ieee80211_hw *wiphy_to_ieee80211_hw(struct wiphy *wiphy)
{
	struct ieee80211_hw **slot;

	if (wiphy == NULL) {
		return NULL;
	}
	slot = wiphy_priv(wiphy);
	return slot != NULL ? *slot : NULL;
}

/* ------------------------------------------------------------------ */
/* Transmit completion helpers                                         */
/* ------------------------------------------------------------------ */

void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	(void)hw;
	dev_kfree_skb_any(skb);
}

void ieee80211_purge_tx_queue(struct ieee80211_hw *hw,
    struct sk_buff_head *skbs)
{
	(void)hw;
	skb_queue_purge(skbs);
}

/* ------------------------------------------------------------------ */
/* Power-save and null frames the firmware reserved-page path asks for  */
/* ------------------------------------------------------------------ */

static const u8 *wlan_vif_bssid(struct ieee80211_vif *vif)
{
	if (vif->bss_conf.bssid != NULL &&
	    !is_zero_ether_addr(vif->bss_conf.bssid)) {
		return vif->bss_conf.bssid;
	}
	if (!is_zero_ether_addr(vif->cfg.ap_addr)) {
		return vif->cfg.ap_addr;
	}
	return NULL;
}

struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif)
{
	struct ieee80211_pspoll *pspoll;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	const u8 *bssid;

	(void)hw;
	if (vif == NULL) {
		return NULL;
	}
	bssid = wlan_vif_bssid(vif);
	if (bssid == NULL) {
		return NULL;
	}
	skb = dev_alloc_skb(sizeof(*pspoll));
	if (skb == NULL) {
		return NULL;
	}
	pspoll = skb_put_zero(skb, sizeof(*pspoll));
	pspoll->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
	    IEEE80211_STYPE_PSPOLL);
	pspoll->aid = cpu_to_le16((u16)(vif->cfg.aid | 0xc000));
	ether_addr_copy(pspoll->bssid, bssid);
	ether_addr_copy(pspoll->ta, vif->addr);
	info = IEEE80211_SKB_CB(skb);
	memset(info, 0, sizeof(*info));
	info->control.vif = vif;
	return skb;
}

struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, int link_id, bool qos_ok)
{
	struct ieee80211_hdr_3addr *hdr;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	const u8 *bssid;

	(void)hw;
	(void)link_id;
	(void)qos_ok;
	if (vif == NULL) {
		return NULL;
	}
	bssid = wlan_vif_bssid(vif);
	if (bssid == NULL) {
		return NULL;
	}
	skb = dev_alloc_skb(sizeof(*hdr));
	if (skb == NULL) {
		return NULL;
	}
	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
	    IEEE80211_STYPE_NULLFUNC);
	ether_addr_copy(hdr->addr1, bssid);
	ether_addr_copy(hdr->addr2, vif->addr);
	ether_addr_copy(hdr->addr3, bssid);
	info = IEEE80211_SKB_CB(skb);
	memset(info, 0, sizeof(*info));
	info->control.vif = vif;
	return skb;
}

/*
 * The probe-request template a hardware scan hands to the firmware. rtw89's
 * offload path builds each scan packet from this template plus the scan
 * request's own IEs (rtw89_hw_scan_append_wildcard_probe_req), so the template
 * is the frame mac80211 builds: a three-address management header and the SSID
 * element, with the caller's tailroom left for the IEs it appends. A NULL
 * return here fails the driver's scan outright ("Update probe request
 * failed"), which is what the 8851BU measured before this body existed.
 */
struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *hw,
    const u8 *src_addr, const u8 *ssid, size_t ssid_len, size_t tailroom)
{
	struct ieee80211_hdr_3addr *hdr;
	struct sk_buff *skb;
	size_t ie_ssid_len = 2 + ssid_len;
	u8 *pos;

	skb = dev_alloc_skb(hw->extra_tx_headroom + sizeof(*hdr) +
	    ie_ssid_len + tailroom);
	if (skb == NULL) {
		return NULL;
	}
	skb_reserve(skb, hw->extra_tx_headroom);

	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
	    IEEE80211_STYPE_PROBE_REQ);
	eth_broadcast_addr(hdr->addr1);
	memcpy(hdr->addr2, src_addr, ETH_ALEN);
	eth_broadcast_addr(hdr->addr3);

	pos = skb_put(skb, ie_ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = ssid_len;
	if (ssid_len != 0) {
		memcpy(pos, ssid, ssid_len);
	}

	return skb;
}

struct sk_buff *ieee80211_proberesp_get(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif)
{
	(void)hw;
	(void)vif;
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Link closure: no station scan/join path calls these                 */
/* ------------------------------------------------------------------ */

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	(void)hw;
}

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	(void)hw;
}

void ieee80211_handle_wake_tx_queue(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq)
{
	(void)hw;
	(void)txq;
}

/*
 * The TXQ path is the data plane's; this line's management frames reach the
 * driver through the bridge's .tx seam, not through a mac80211 TXQ. A data
 * round that wants the queue path fills these in with the port's txq object.
 */
struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq)
{
	(void)hw;
	(void)txq;
	return NULL;
}

void ieee80211_txq_get_depth(struct ieee80211_txq *txq,
    unsigned long *frame_cnt, unsigned long *byte_cnt)
{
	(void)txq;
	if (frame_cnt != NULL) {
		*frame_cnt = 0;
	}
	if (byte_cnt != NULL) {
		*byte_cnt = 0;
	}
}

int ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid,
    u16 timeout)
{
	(void)sta;
	(void)tid;
	(void)timeout;
	return 0;
}

void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
    u16 tid)
{
	(void)vif;
	(void)ra;
	(void)tid;
}

struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, u16 *tim_offset, u16 *tim_length,
    unsigned int link_id)
{
	(void)hw;
	(void)vif;
	(void)tim_offset;
	(void)tim_length;
	(void)link_id;
	return NULL;
}

bool ieee80211_beacon_cntdwn_is_complete(struct ieee80211_vif *vif,
    unsigned int link_id)
{
	(void)vif;
	(void)link_id;
	return false;
}

void ieee80211_csa_finish(struct ieee80211_vif *vif, unsigned int link_id)
{
	(void)vif;
	(void)link_id;
}

int ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_chanctx_conf *ctx)
{
	(void)hw;
	(void)ctx;
	return 0;
}

void ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_chanctx_conf *ctx)
{
	(void)hw;
	(void)ctx;
}

void ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_chanctx_conf *ctx, u32 changed)
{
	(void)hw;
	(void)ctx;
	(void)changed;
}

int ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_vif_chanctx_switch *vifs, int n_vifs,
    enum ieee80211_chanctx_switch_mode mode)
{
	(void)hw;
	(void)vifs;
	(void)n_vifs;
	(void)mode;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Notices the station happy path does not reach                       */
/* ------------------------------------------------------------------ */

void ieee80211_connection_loss(struct ieee80211_vif *vif)
{
	(void)vif;
}

void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
    enum nl80211_cqm_rssi_threshold_event rssi_event, s32 rssi_level,
    gfp_t gfp)
{
	(void)vif;
	(void)rssi_event;
	(void)rssi_level;
	(void)gfp;
}

void ieee80211_request_smps(struct ieee80211_vif *vif, unsigned int link_id,
    enum ieee80211_smps_mode smps_mode)
{
	(void)vif;
	(void)link_id;
	(void)smps_mode;
}

void ieee80211_restart_hw(struct ieee80211_hw *hw)
{
	(void)hw;
}

void ieee80211_report_wowlan_wakeup(struct ieee80211_vif *vif,
    struct cfg80211_wowlan_wakeup *wakeup, gfp_t gfp)
{
	(void)vif;
	(void)wakeup;
	(void)gfp;
}

/*
 * The key table is empty until the keyed round installs one: the open L0
 * association carries no keys, so both walks visit nothing.
 */
void ieee80211_iter_keys(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    void (*iter)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_sta *sta, struct ieee80211_key_conf *key,
		 void *data),
    void *iter_data)
{
	(void)hw;
	(void)vif;
	(void)iter;
	(void)iter_data;
}

void ieee80211_iter_keys_rcu(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif,
    void (*iter)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_sta *sta, struct ieee80211_key_conf *key,
		 void *data),
    void *iter_data)
{
	ieee80211_iter_keys(hw, vif, iter, iter_data);
}

const char *__ieee80211_create_tpt_led_trigger(struct ieee80211_hw *hw,
    unsigned int flags, const struct ieee80211_tpt_blink *blink_table,
    unsigned int blink_table_len)
{
	(void)hw;
	(void)flags;
	(void)blink_table;
	(void)blink_table_len;
	return NULL;
}
/* ------------------------------------------------------------------ */
/* The rtw89 face: entries the 8851BU driver links against             */
/* ------------------------------------------------------------------ */

/*
 * The rtw89 8851BU build leaves nineteen symbols in the mac80211 namespace
 * undefined that the rtw88 build did not. Three are on the station happy path
 * (the NAPI receive entry above, the station walker above, and the LED trigger
 * name below); the rest are link closure for paths this line does not drive:
 * the mac80211 TXQ engine (the data plane hands frames to .tx directly), block
 * acknowledgement, power save, GTK rekeying, remain-on-channel and multi-link
 * activation. Each body says which path it stands for; a round that drives one
 * of those paths replaces the body rather than working around it.
 */

/*
 * The associativity LED trigger name. mac80211 registers one trigger per hw and
 * returns its name here; this line has no LED trigger registry, so the name is
 * a stable string. The driver hands it to the LED class as the default trigger
 * (led.c:76); a trigger that does not exist leaves the LED unbound, which is a
 * cosmetic difference on a board with no LED wired to the chip.
 */
const char *__ieee80211_get_assoc_led_name(struct ieee80211_hw *hw)
{
	(void)hw;
	return "wlan_ko_assoc";
}

/*
 * The TXQ engine. The kernelmod line's data plane builds the 802.11 frame in
 * net80211 and hands it to the driver's .tx (the shape the 8821CU round
 * measured), so no queue is ever scheduled, dequeued or returned: rtw89 only
 * reaches these entries if its wake_tx_queue path runs, and mac80211 - not this
 * substitute - is what would call that. The bodies are the honest answer for a
 * queue that does not exist, not a silent reimplementation.
 */
void __ieee80211_schedule_txq(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq, bool force)
{
	(void)hw;
	(void)txq;
	(void)force;
}

struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, u8 ac)
{
	(void)hw;
	(void)ac;
	return NULL;
}

void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac)
{
	(void)hw;
	(void)ac;
}

/*
 * Block acknowledgement: the lab AP runs 20 MHz without HT, so no BA session is
 * started; rtw89 calls this only from its TXQ/aggregation teardown.
 */
int ieee80211_stop_tx_ba_session(struct ieee80211_sta *sta, u16 tid)
{
	(void)sta;
	(void)tid;
	return 0;
}

/*
 * Per-station aggregate bookkeeping. rtw89 calls it from its rate-adaptation
 * update (phy.c:3368); the firmware owns the rate and aggregation decision, and
 * this line has no mac80211 TXQ state to recompute.
 */
void ieee80211_sta_recalc_aggregates(struct ieee80211_sta *pubsta)
{
	(void)pubsta;
}

/*
 * Power save: the station vif never receives BSS_CHANGED_PS from this adapter,
 * so the driver does not enter its PS state machine. rtw89 calls these from the
 * mac/PS paths (mac.c:6055, core.c:4517/4521); they are unreachable on the
 * open, always-awake L0 connection.
 */
int ieee80211_sta_ps_transition(struct ieee80211_sta *sta, bool start)
{
	(void)sta;
	(void)start;
	return 0;
}

void ieee80211_sta_pspoll(struct ieee80211_sta *sta)
{
	(void)sta;
}

void ieee80211_sta_uapsd_trigger(struct ieee80211_sta *sta, u8 tid)
{
	(void)sta;
	(void)tid;
}

/*
 * Beacon loss reported from the firmware's beacon filter (mac.c:5422). The
 * net80211 stack runs its own beacon watchdog and moves the node to SCAN when
 * the AP goes away, so the notification has no second consumer here.
 */
void ieee80211_beacon_loss(struct ieee80211_vif *vif)
{
	(void)vif;
}

/*
 * GTK rekeying while suspended: the rtw89 entries are compiled in through
 * wow.c (CONFIG_PM), but the kernelmod line never suspends and the open L0
 * association has no keys.
 */
struct ieee80211_key_conf *
ieee80211_gtk_rekey_add(struct ieee80211_vif *vif, u8 idx, u8 *key_data,
    u8 key_len, int link_id)
{
	(void)vif;
	(void)idx;
	(void)key_data;
	(void)key_len;
	(void)link_id;
	return NULL;
}

void ieee80211_gtk_rekey_notify(struct ieee80211_vif *vif, const u8 *bssid,
    const u8 *replay_ctr, gfp_t gfp)
{
	(void)vif;
	(void)bssid;
	(void)replay_ctr;
	(void)gfp;
}

/* the key table is empty until the keyed round installs one */
void ieee80211_get_key_rx_seq(struct ieee80211_key_conf *keyconf, int tid,
    struct ieee80211_key_seq *seq)
{
	(void)keyconf;
	(void)tid;
	if (seq != NULL) {
		memset(seq, 0, sizeof(*seq));
	}
}

void ieee80211_set_key_rx_seq(struct ieee80211_key_conf *keyconf, int tid,
    struct ieee80211_key_seq *seq)
{
	(void)keyconf;
	(void)tid;
	(void)seq;
}

/*
 * Remain-on-channel: this line drives one station connection and no off-channel
 * operation; rtw89 reaches these from its ROC work only.
 */
void ieee80211_ready_on_channel(struct ieee80211_hw *hw)
{
	(void)hw;
}

void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw)
{
	(void)hw;
}

/*
 * Multi-link activation. The 8851B is not an MLD chip and this line builds a
 * non-MLO vif (valid_links == 0), so rtw89's only caller is guarded by
 * ieee80211_vif_is_mld() and never reaches here (core.c:7222-7230).
 */
int ieee80211_set_active_links(struct ieee80211_vif *vif, u16 active_links)
{
	(void)vif;
	(void)active_links;
	return -EOPNOTSUPP;
}
