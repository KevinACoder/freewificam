/*
 * @file
 * @brief mac80211 closure for the iwlwifi arm: the symbols mvm needs that the
 *        net80211 bridge + substitute do not otherwise provide.
 *
 * The list is not guessed: it is the `nm -u wlan_ko.ko | grep ieee80211_` set
 * after the 7260 mvm build against the ko kernel, minus the helpers cfg80211
 * itself exports (channel<->freq, hdrlen, ie_split, data_to_8023, vht nss).
 * Those are linked from the builtin cfg80211 and listed in
 * tests/board/linux-ko/check-closure.sh; everything else is here.
 *
 * Real semantics vs stubs:
 *   - real: rate-control registration (mvm_init fails without it), interface
 *     iteration, vif->wdev, TX status completion;
 *   - stubs: AP/beacon, CSA, TDLS, TKIP, BA timers, PS buffering, sched-scan.
 *     The 7260 station happy path does not call them; each one returns the
 *     neutral value so a stray call cannot crash. They are `documented` in
 *     fw-trm/iwl7260/11 (D-list), not `verified`.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/mac80211.h>

#include "wlan_linux.h"

/* ------------------------------------------------------------------ */
/* real: registration / iteration / status                             */
/* ------------------------------------------------------------------ */

const char *__ieee80211_get_radio_led_name(struct ieee80211_hw *hw)
{
	(void)hw;
	return "wlan_ko-radio";
}

void ieee80211_iterate_interfaces(struct ieee80211_hw *hw, u32 iter_flags,
    void (*iterator)(void *data, u8 *mac, struct ieee80211_vif *vif),
    void *data)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	(void)iter_flags;
	if (w == NULL || w->vif == NULL || iterator == NULL) {
		return;
	}
	iterator(data, w->vif->addr, w->vif);
}

struct ieee80211_vif *__ieee80211_iterate_interfaces(struct ieee80211_hw *hw,
    struct ieee80211_vif *prev, u32 iter_flags)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);

	(void)iter_flags;
	if (w == NULL || prev != NULL) {
		return NULL;
	}
	return w->vif;
}

static const struct rate_control_ops *wlan_rc_ops;

int ieee80211_rate_control_register(const struct rate_control_ops *ops)
{
	if (ops == NULL || ops->name == NULL) {
		return -EINVAL;
	}
	if (wlan_rc_ops != NULL) {
		return -EEXIST;
	}
	wlan_rc_ops = ops;
	pr_info("wlan_ko: rate control '%s' registered\n", ops->name);
	return 0;
}
EXPORT_SYMBOL_GPL(ieee80211_rate_control_register);

void ieee80211_rate_control_unregister(const struct rate_control_ops *ops)
{
	if (wlan_rc_ops == ops) {
		wlan_rc_ops = NULL;
	}
}
EXPORT_SYMBOL_GPL(ieee80211_rate_control_unregister);

struct wireless_dev *ieee80211_vif_to_wdev(struct ieee80211_vif *vif)
{
	struct wlan_linux_hw *w = wlan_hw_from_vif(vif);

	return w != NULL ? &w->wdev : NULL;
}

void ieee80211_tx_status_ext(struct ieee80211_hw *hw,
    struct ieee80211_tx_status *status)
{
	if (status != NULL && status->skb != NULL) {
		wlan_adapter_tx_complete(hw, status->skb);
	}
}

void ieee80211_tx_status_skb(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	if (skb != NULL) {
		wlan_adapter_tx_complete(hw, skb);
	}
}

/* ------------------------------------------------------------------ */
/* neutral stubs: AP/beacon, CSA, TDLS, TKIP, BA, PS, sched-scan       */
/* ------------------------------------------------------------------ */

struct sk_buff *ieee80211_beacon_get_template(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, struct ieee80211_mutable_offsets *offs,
    unsigned int link_id)
{
	(void)hw;
	(void)vif;
	(void)offs;
	(void)link_id;
	return NULL;
}

u8 ieee80211_beacon_update_cntdwn(struct ieee80211_vif *vif,
    unsigned int link_id)
{
	(void)vif;
	(void)link_id;
	return 0;
}

void ieee80211_beacon_set_cntdwn(struct ieee80211_vif *vif, u8 counter)
{
	(void)vif;
	(void)counter;
}

void ieee80211_chswitch_done(struct ieee80211_vif *vif, bool success,
    unsigned int link_id)
{
	(void)vif;
	(void)success;
	(void)link_id;
}

void ieee80211_channel_switch_disconnect(struct ieee80211_vif *vif)
{
	(void)vif;
}

void ieee80211_cqm_beacon_loss_notify(struct ieee80211_vif *vif, gfp_t gfp)
{
	(void)vif;
	(void)gfp;
}

void ieee80211_disconnect(struct ieee80211_vif *vif, bool reconnect)
{
	(void)vif;
	(void)reconnect;
}

void ieee80211_hw_restart_disconnect(struct ieee80211_vif *vif)
{
	(void)vif;
}

void ieee80211_resume_disconnect(struct ieee80211_vif *vif)
{
	(void)vif;
}

void ieee80211_key_mic_failure(struct ieee80211_key_conf *keyconf)
{
	(void)keyconf;
}

void ieee80211_key_replay(struct ieee80211_key_conf *keyconf)
{
	(void)keyconf;
}

void ieee80211_mark_rx_ba_filtered_frames(struct ieee80211_sta *pubsta, u8 tid,
    u16 ssn, u64 filtered, u16 received_mpdus)
{
	(void)pubsta;
	(void)tid;
	(void)ssn;
	(void)filtered;
	(void)received_mpdus;
}

void ieee80211_rx_ba_timer_expired(struct ieee80211_vif *vif, const u8 *addr,
    unsigned int tid)
{
	(void)vif;
	(void)addr;
	(void)tid;
}

int ieee80211_reserve_tid(struct ieee80211_sta *sta, u8 tid)
{
	(void)sta;
	(void)tid;
	return 0;
}

void ieee80211_unreserve_tid(struct ieee80211_sta *sta, u8 tid)
{
	(void)sta;
	(void)tid;
}

void ieee80211_sched_scan_results(struct ieee80211_hw *hw)
{
	(void)hw;
}

void ieee80211_sched_scan_stopped(struct ieee80211_hw *hw)
{
	(void)hw;
}

void ieee80211_send_eosp_nullfunc(struct ieee80211_sta *pubsta, int tid)
{
	(void)pubsta;
	(void)tid;
}

void ieee80211_sta_block_awake(struct ieee80211_hw *hw,
    struct ieee80211_sta *pubsta, bool block)
{
	(void)hw;
	(void)pubsta;
	(void)block;
}

void ieee80211_sta_eosp(struct ieee80211_sta *pubsta)
{
	(void)pubsta;
}

void ieee80211_sta_set_buffered(struct ieee80211_sta *sta, u8 tid, bool buffered)
{
	(void)sta;
	(void)tid;
	(void)buffered;
}

void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
    u16 tid)
{
	(void)vif;
	(void)ra;
	(void)tid;
}

void ieee80211_tdls_oper_request(struct ieee80211_vif *vif, const u8 *peer,
    enum nl80211_tdls_operation oper, u16 reason_code, gfp_t gfp)
{
	(void)vif;
	(void)peer;
	(void)oper;
	(void)reason_code;
	(void)gfp;
}

u8 *ieee80211_tkip_add_iv(u8 *pos, struct ieee80211_key_conf *keyconf, u64 pn)
{
	(void)keyconf;
	(void)pn;
	return pos;
}

void ieee80211_get_tkip_p1k_iv(struct ieee80211_key_conf *keyconf, u32 iv32,
    u16 *p1k)
{
	(void)keyconf;
	(void)iv32;
	memset(p1k, 0, 5 * sizeof(*p1k));
}

void ieee80211_get_tkip_rx_p1k(struct ieee80211_key_conf *keyconf,
    const u8 *ta, u32 iv32, u16 *p1k)
{
	(void)keyconf;
	(void)ta;
	(void)iv32;
	memset(p1k, 0, 5 * sizeof(*p1k));
}

void ieee80211_get_tkip_p2k(struct ieee80211_key_conf *keyconf,
    struct sk_buff *skb, u8 *p2k)
{
	(void)keyconf;
	(void)skb;
	memset(p2k, 0, 16);
}

void ieee80211_update_mu_groups(struct ieee80211_vif *vif, unsigned int link_id,
    const u8 *membership, const u8 *position)
{
	(void)vif;
	(void)link_id;
	(void)membership;
	(void)position;
}
