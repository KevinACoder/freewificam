/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linuxkpi_80211_helpers.c
 * @brief   The rest of the mac80211 face's closure: the calls the interface
 *          declares and the drivers here reach, whose bodies are neither an
 *          object this port owns (linuxkpi_80211.c) nor a one-line predicate
 *          the header can inline (include/linux/ieee80211.h).
 *
 * @design the same ports-and-adapters body as linuxkpi_80211.c, split out by
 *         subject rather than by layer: what lives here are the helpers that
 *         upstream keeps beside the stack's own bookkeeping - the frame
 *         builders (net/mac80211/tx.c), the work/deliver entry points
 *         (net/mac80211/util.c, net/mac80211/rx.c), the IE and frequency
 *         helpers (net/wireless/util.c, net/wireless/scan.c) and the band
 *         record installer (net/mac80211/main.c).  Each body names the
 *         upstream site it follows; where this port cannot follow it, the
 *         difference is stated at the body rather than left to be discovered.
 *
 * Why this file exists at all: the port's face was extracted from one driver's
 * undefined symbols, so a second driver reaches calls the first never made.
 * The list below is what the rtw89 build found missing, completed here so the
 * face is the closure of the drivers this repository carries rather than of
 * the one it started with.
 * @date    10.10.2026
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>

#include "linuxkpi_80211.h"

/* ------------------------------------------------------------------ */
/* work and delivery                                                   */
/* ------------------------------------------------------------------ */

void
ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{

	/*
	 * [LNX] include/net/mac80211.h: `static inline void
	 * ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
	 * { queue_work(hw->workqueue, work); }`.  The hw's own queue is the
	 * system queue in this port (the interface's per-hw workqueue has no
	 * counterpart), which is where the engine's deferred work already runs.
	 */
	(void)hw;
	(void)lkpi_queue_work(lkpi_system_wq, work);
}

void
ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
    struct delayed_work *dwork, unsigned long delay)
{

	/* [LNX] include/net/mac80211.h: the delayed form of the same call */
	(void)hw;
	(void)lkpi_queue_delayed_work(lkpi_system_wq, dwork, delay);
}

/*
 * The receive entry's own evidence.  A bring-up that sees an empty receive table
 * needs to tell three things apart: nothing was handed over, frames were handed
 * over but the bridge dropped them, or what arrived is not a frame at all (the
 * firmware's own reports ride the same path).  The count answers the first, and
 * the first frame's type/subtype answers the third.
 */
static u32 lkpi80211_rx_frames;
static bool lkpi80211_rx_fc_seen;
static u8 lkpi80211_rx_first_fc;

void
lkpi80211_rx_counters(u32 *frames, u32 *first_fc, int *fc_seen)
{

	if (frames != NULL)
		*frames = lkpi80211_rx_frames;
	if (first_fc != NULL)
		*first_fc = lkpi80211_rx_first_fc;
	if (fc_seen != NULL)
		*fc_seen = lkpi80211_rx_fc_seen ? 1 : 0;
}

void
ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
    struct sk_buff *skb, struct napi_struct *napi)
{

	/*
	 * [LNX] net/mac80211/rx.c:ieee80211_rx_napi is the receive entry; the
	 * napi argument only names the poll instance that delivered the frame,
	 * and this port has no NAPI.  The frame goes where ieee80211_rx_irqsafe
	 * sends it - the bridge - which is the same single path the interface
	 * would end at after its own RX processing, minus that processing: the
	 * port's bridge owns MLME and hands the frame on from there.
	 */
	(void)sta;
	(void)napi;
	lkpi80211_rx_frames++;
	if (!lkpi80211_rx_fc_seen && skb != NULL && skb->len >= 1) {
		lkpi80211_rx_fc_seen = true;
		lkpi80211_rx_first_fc = skb->data[0];
	}
	ieee80211_rx_irqsafe(hw, skb);
}

void
ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb)
{

	/*
	 * [LNX] net/mac80211/status.c:1279: report the frame as used, then
	 * release it.  There is no report to make here - a frame this port
	 * drops never reached a status path - so the substance is the release.
	 */
	(void)hw;
	if (skb != NULL)
		lkpi_skb_free(skb);
}

/* ------------------------------------------------------------------ */
/* frame builders                                                      */
/* ------------------------------------------------------------------ */

struct sk_buff *
ieee80211_nullfunc_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    int link_id, bool qos_ok)
{
	struct ieee80211_hdr_3addr *hdr;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	const u8 *bssid;

	(void)hw;
	(void)qos_ok;
	/* one link per vif in this port (link.c: the default link is 0) */
	(void)link_id;
	if (vif == NULL)
		return NULL;
	bssid = vif->bss_conf.bssid != NULL ? vif->bss_conf.bssid :
	    vif->cfg.ap_addr;
	if (bssid == NULL)
		return NULL;
	/*
	 * [LNX] net/mac80211/tx.c:ieee80211_nullfunc_get builds the frame
	 * through ieee80211_build_hdr, which also runs the header conversion
	 * and the key selection.  The frame itself is the interface's: a
	 * three-address data frame with the null-function subtype and no
	 * payload.  The QoS form is not built - the plain form is legal
	 * wherever the QoS one is, so a caller that asked for either gets a
	 * frame it can send.
	 */
	skb = dev_alloc_skb(sizeof(*hdr));
	if (skb == NULL)
		return NULL;
	hdr = (struct ieee80211_hdr_3addr *)skb_put(skb, sizeof(*hdr));
	if (hdr == NULL) {
		lkpi_skb_free(skb);
		return NULL;
	}
	memset(hdr, 0, sizeof(*hdr));
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

/* ------------------------------------------------------------------ */
/* power save, off-channel and aggregation: declared, not reached here  */
/* ------------------------------------------------------------------ */

/*
 * The calls below belong to the interface's power-save, remain-on-channel and
 * aggregation machinery.  This port declares itself without all three: there
 * is no PS state machine, no off-channel work and no block-ack session (the
 * aggregation entry points already answer -EOPNOTSUPP, see linuxkpi_80211.c).
 * A driver that calls them therefore gets the refusal the interface would give
 * a chip that does not offer the feature - a no-op notification, or an error -
 * rather than a silent lie about work that was never done.
 */

void
ieee80211_sta_pspoll(struct ieee80211_sta *sta)
{

	/* [LNX] net/mac80211/tx.c: send a PS-Poll for that station */
	(void)sta;
}

void
ieee80211_sta_uapsd_trigger(struct ieee80211_sta *sta, u8 tid)
{

	/* [LNX] net/mac80211/tx.c: the U-APSD trigger for that TID */
	(void)sta;
	(void)tid;
}

void
ieee80211_ready_on_channel(struct ieee80211_hw *hw)
{

	/* [LNX] net/mac80211/offchannel.c: the channel is ready for use */
	(void)hw;
}

void
ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw)
{

	/* [LNX] net/mac80211/offchannel.c: the dwell time ran out */
	(void)hw;
}

int
ieee80211_stop_tx_ba_session(struct ieee80211_sta *sta, u16 tid)
{

	/* [LNX] net/mac80211/agg-tx.c: stop an aggregation session.  Nothing
	 * was ever started (ieee80211_start_tx_ba_session refuses), so this is
	 * the same refusal the start side gives. */
	(void)sta;
	(void)tid;
	return -EOPNOTSUPP;
}

/* ------------------------------------------------------------------ */
/* station iteration and the band record installer                     */
/* ------------------------------------------------------------------ */

struct ieee80211_sta *
__ieee80211_iterate_stations(struct ieee80211_hw *hw,
    struct ieee80211_sta *prev)
{

	/*
	 * [LNX] net/mac80211/driver-ops.c: the interface walks its own station
	 * table; this port's table lives in linuxkpi_80211.c, and the walk
	 * helper it exposes is asked for the entry after `prev`.  The
	 * for_each_station() macro is written against exactly this shape.
	 */
	return lkpi80211_hw_sta_next(hw, prev);
}

void
_ieee80211_set_sband_iftype_data(struct ieee80211_supported_band *sband,
    const struct ieee80211_sband_iftype_data *iftd, u16 n_iftd)
{
	struct ieee80211_sband_iftype_data *copy;

	/*
	 * [LNX] net/mac80211/main.c: the band record takes a copy of the
	 * per-interface-type capabilities (HE/EHT) the driver assembled, and
	 * the stack reads them back for every frame it builds for that type.
	 * The copy is the point: the driver's array is a stack local.
	 */
	if (sband == NULL || iftd == NULL || n_iftd == 0)
		return;
	copy = kzalloc(sizeof(*copy) * n_iftd, GFP_KERNEL);
	if (copy == NULL)
		return;
	memcpy(copy, iftd, sizeof(*copy) * n_iftd);
	sband->iftype_data = copy;
	sband->n_iftype_data = (u8)n_iftd;
}

/* ------------------------------------------------------------------ */
/* IE and frequency helpers                                            */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/ieee80211.h:3228,3231,3274 - the two element ids this
 * port's channel lookup reads (the interface's full enum is not carried here) */
#define LKPI80211_EID_DS_PARAMS		3
#define LKPI80211_EID_HT_OPERATION	61

const u8 *
cfg80211_find_ie(u8 eid, const u8 *ies, int len)
{
	const u8 *pos = ies;
	const u8 *end = ies + len;

	/*
	 * [LNX] include/net/cfg80211.h: `cfg80211_find_ie` is the interface's
	 * inline wrapper over cfg80211_find_ie_match with an empty match set;
	 * the walk below is that function's substance for the no-match case.
	 */
	if (ies == NULL || len <= 0)
		return NULL;
	while (pos + 2 <= end) {
		if (pos[1] > (size_t)(end - pos - 2))
			return NULL;
		if (pos[0] == eid)
			return pos;
		pos += pos[1] + 2;
	}
	return NULL;
}

int
cfg80211_get_ies_channel_number(const u8 *ie, size_t ielen,
    enum nl80211_band band)
{
	const u8 *elem;

	(void)band;
	/*
	 * [LNX] net/wireless/scan.c:cfg80211_get_ies_channel_number.  The
	 * 6 GHz and S1G arms of the interface's version read elements this
	 * port's chips never report (the 8851BU is a 2.4/5 GHz part), so the
	 * arms carried here are the two that apply: the DS parameter set, and
	 * the HT operation element whose first byte is the primary channel.
	 */
	elem = cfg80211_find_ie(LKPI80211_EID_DS_PARAMS, ie, (int)ielen);
	if (elem != NULL && elem[1] == 1)
		return elem[2];

	elem = cfg80211_find_ie(LKPI80211_EID_HT_OPERATION, ie, (int)ielen);
	if (elem != NULL && elem[1] >= 1)
		return elem[2];

	return -1;
}

int
ieee80211_channel_to_frequency(int chan, enum nl80211_band band)
{

	/*
	 * [LNX] net/wireless/util.c:ieee80211_channel_to_frequency.  The two
	 * arms below are the ones a 2.4/5 GHz part reaches; the interface's
	 * 6 GHz, 60 GHz and S1G arms are left out because nothing in this port
	 * can ask for them (the bands are not registered).
	 */
	if (chan <= 0)
		return 0;
	switch (band) {
	case NL80211_BAND_2GHZ:
		if (chan == 14)
			return 2484;
		if (chan < 14)
			return 2407 + chan * 5;
		break;
	case NL80211_BAND_5GHZ:
		if (chan >= 182 && chan <= 196)
			return 4000 + chan * 5;	/* 4.9 GHz */
		return 5000 + chan * 5;
	default:
		break;
	}
	return 0;
}

void
cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
    struct ieee80211_channel *channel,
    enum nl80211_channel_type chantype)
{

	/*
	 * [LNX] net/wireless/chan.c:cfg80211_chandef_create.  The interface
	 * also zeroes the EDMG fields, which this face's chan_def record does
	 * not carry.
	 */
	if (chandef == NULL || channel == NULL)
		return;
	memset(chandef, 0, sizeof(*chandef));
	chandef->chan = channel;
	chandef->freq1_offset = channel->freq_offset;
	switch (chantype) {
	case NL80211_CHAN_NO_HT:
		chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
		chandef->center_freq1 = channel->center_freq;
		break;
	case NL80211_CHAN_HT20:
		chandef->width = NL80211_CHAN_WIDTH_20;
		chandef->center_freq1 = channel->center_freq;
		break;
	case NL80211_CHAN_HT40PLUS:
		chandef->width = NL80211_CHAN_WIDTH_40;
		chandef->center_freq1 = channel->center_freq + 10;
		break;
	case NL80211_CHAN_HT40MINUS:
		chandef->width = NL80211_CHAN_WIDTH_40;
		chandef->center_freq1 = channel->center_freq - 10;
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* the wider closure: the calls the second driver reaches              */
/* ------------------------------------------------------------------ */

/*
 * What follows is the rest of the face the rtw89 build asked for.  Each body
 * states which of three things it is: the interface's own arithmetic, a
 * feature this port does not carry (and therefore an honest refusal or a
 * no-op notification rather than a silent lie), or a builder whose frame the
 * port's bridge produces instead.  The distinction matters on a board: a
 * no-op where the interface would do work is a bug, and the comments are how a
 * later round tells the two apart.
 */

/* the 802.2 LLC/SNAP header a translated Ethernet frame carries
 * ([LNX] net/wireless/util.c:2353) */
const unsigned char rfc1042_header[6] __aligned(2) =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

void
get_random_mask_addr(u8 *buf, const u8 *addr, const u8 *mask)
{
	unsigned int i;

	/* [LNX] net/mac80211/util.c: the interface masks an address with the
	 * device's address mask (the per-device randomisation it is named for
	 * is the caller's, not this helper's) */
	if (buf == NULL || addr == NULL || mask == NULL)
		return;
	for (i = 0; i < ETH_ALEN; i++)
		buf[i] = addr[i] & mask[i];
}

int
sysfs_emit(char *buf, const char *fmt, ...)
{
	va_list args;
	int len;

	/* [LNX] fs/sysfs/file.c: the show callbacks format into the page the
	 * attribute is read through, and the return value is what was written */
	if (buf == NULL || fmt == NULL)
		return 0;
	va_start(args, fmt);
	len = vsnprintf(buf, PAGE_SIZE, fmt, args);
	va_end(args);
	if (len < 0)
		return 0;
	if (len >= PAGE_SIZE)
		len = PAGE_SIZE - 1;
	return len;
}

void
sort(void *base, size_t num, size_t size,
    int (*cmp)(const void *, const void *), void (*swap_fn)(void *, void *, int))
{
	unsigned char *b = base;
	size_t i, j;

	/*
	 * [LNX] lib/sort.c: heapsort over a caller-supplied comparison and
	 * swap.  This port only ever sorts small descriptor tables, so the
	 * insertion form is used: it is the same total order, and it needs no
	 * auxiliary storage.  (The parameter is named swap_fn because this
	 * face's contract header defines swap() as the byte-swap helper.)
	 */
	if (base == NULL || cmp == NULL || swap_fn == NULL || num < 2)
		return;
	for (i = 1; i < num; i++)
		for (j = i; j > 0; j--) {
			if (cmp(b + (j - 1) * size, b + j * size) <= 0)
				break;
			swap_fn(b + (j - 1) * size, b + j * size, (int)size);
		}
}

void
ieee80211_purge_tx_queue(struct ieee80211_hw *hw, struct sk_buff_head *skbs)
{
	struct sk_buff *skb;

	/* [LNX] net/mac80211/util.c: release everything still queued */
	if (skbs == NULL)
		return;
	while ((skb = skb_dequeue(skbs)) != NULL)
		ieee80211_free_txskb(hw, skb);
}

bool
napi_schedule_prep(struct napi_struct *n)
{

	/*
	 * [LNX] include/linux/netdevice.h: "can this instance be scheduled",
	 * answered from the SCHED bit.  There is no NAPI framework in this
	 * port and no softirq to defer to, so the poll runs inline in
	 * __napi_schedule below; the bit is still kept, because a caller that
	 * checks it is asking whether the poll is already pending.
	 */
	if (n == NULL || n->poll == NULL)
		return false;
	if (test_and_set_bit(NAPI_STATE_SCHED, &n->state))
		return false;
	return true;
}

void
__napi_schedule(struct napi_struct *n)
{

	/*
	 * [LNX] net/core/dev.c: hand the instance to the softirq poller.  With
	 * no poller here, the poll is run in the caller's context - which is
	 * the driver's completion path, exactly the context the interface's own
	 * NAPI would have resumed in had the poller run immediately.  The
	 * budget is the instance's weight, so a poll that keeps finding work
	 * returns to its caller rather than spinning.
	 */
	if (n == NULL || n->poll == NULL)
		return;
	(void)n->poll(n, n->weight);
	clear_bit(NAPI_STATE_SCHED, &n->state);
}

void
wiphy_lock(struct wiphy *wiphy)
{

	/* [LNX] include/net/cfg80211.h: the interface's own inline takes the
	 * wiphy mutex; the face declared it instead of defining it, so the body
	 * is here */
	if (wiphy != NULL)
		mutex_lock(&wiphy->mtx);
}

void
wiphy_unlock(struct wiphy *wiphy)
{

	if (wiphy != NULL)
		mutex_unlock(&wiphy->mtx);
}

int
regulatory_hint(struct wiphy *wiphy, const char *alpha2)
{

	/*
	 * [LNX] net/wireless/reg.c: tell the regulatory core which country the
	 * device is in.  This port carries no regulatory database and no
	 * notifier chain, so there is nothing to apply; accepting the hint
	 * (rather than refusing it) is the same answer the interface gives when
	 * it has no rule for the pair, and it keeps the driver's probe from
	 * failing on a fact the port cannot use.
	 */
	(void)wiphy;
	(void)alpha2;
	return 0;
}

bool
cfg80211_channel_is_psc(struct ieee80211_channel *chan)
{

	/* [LNX] net/wireless/chan.c: the preferred-scanning-channel flag, which
	 * exists only in the 6 GHz band.  No band this port registers has it. */
	(void)chan;
	return false;
}

const struct element *
cfg80211_find_elem(u8 eid, const u8 *ies, int len)
{

	/* [LNX] include/net/cfg80211.h: the element form of cfg80211_find_ie -
	 * same walk, and the record it returns starts with the id/length pair
	 * the raw pointer already points at */
	return (const struct element *)cfg80211_find_ie(eid, ies, len);
}

u16
ieee80211_vif_usable_links(const struct ieee80211_vif *vif)
{

	/* [LNX] include/net/mac80211.h: a vif with no valid links (a non-MLO
	 * one, which is every vif here) is usable on its default link */
	if (vif == NULL)
		return 0;
	return vif->valid_links != 0 ? vif->valid_links : 1;
}

/* ------------------------------------------------------------------ */
/* MLME notifications and the calls that belong to absent features     */
/* ------------------------------------------------------------------ */

/*
 * The interface's MLME owns association, the connection-loss report and the
 * CQM thresholds; in this port that state machine is net80211's, on the other
 * side of the bridge, and the bridge is what tells the driver.  A driver that
 * reports one of these events to the stack is reporting to a machine that is
 * not the one running the connection, so the report is dropped rather than
 * re-entered.  The scan-completion notice is the same case: this port's sweep
 * is driven by the bridge, which knows when it ended.
 */
void
ieee80211_beacon_loss(struct ieee80211_vif *vif)
{

	(void)vif;
}

void
ieee80211_connection_loss(struct ieee80211_vif *vif)
{

	(void)vif;
}

void
ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
    enum nl80211_cqm_rssi_threshold_event rssi_event, s32 rssi_level,
    gfp_t gfp)
{

	(void)vif;
	(void)rssi_event;
	(void)rssi_level;
	(void)gfp;
}

void
ieee80211_scan_completed(struct ieee80211_hw *hw, struct cfg80211_scan_info *info)
{

	(void)hw;
	(void)info;
}

void
ieee80211_restart_hw(struct ieee80211_hw *hw)
{

	/* [LNX] net/mac80211/main.c: restart the driver after a fatal error.
	 * This port has no recovery path to restart into; the call is accepted
	 * and does nothing, which is the honest answer for a stack that would
	 * have to re-run probe to mean it. */
	(void)hw;
}

void
ieee80211_iter_keys_rcu(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    void (*iter)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_sta *sta, struct ieee80211_key_conf *key,
		 void *data),
    void *iter_data)
{

	/* [LNX] net/mac80211/key.c: walk the keys the stack installed.  In this
	 * port the key set lives with the supplicant on the bridge's side and
	 * the driver is told about each key as it is installed, so there is no
	 * stack-side key list to walk. */
	(void)hw;
	(void)vif;
	(void)iter;
	(void)iter_data;
}

void
cfg80211_bss_iter(struct wiphy *wiphy,
    struct cfg80211_chan_def *chandef,
    void (*iter)(struct wiphy *wiphy, struct cfg80211_bss *bss, void *data),
    void *data)
{

	/* [LNX] net/wireless/scan.c: walk the scan results the stack holds.
	 * The scan results in this port are net80211's node table, on the
	 * bridge's side, and the bridge is what reads them. */
	(void)wiphy;
	(void)chandef;
	(void)iter;
	(void)data;
}

int
ieee80211_set_active_links(struct ieee80211_vif *vif, u16 active_links)
{

	/* [LNX] net/mac80211/link.c: multi-link operation.  This port carries
	 * one link per interface (see the single-link bookkeeping in
	 * linuxkpi_80211.c), so there is no link set to switch. */
	(void)vif;
	(void)active_links;
	return -EOPNOTSUPP;
}

int
ieee80211_sta_ps_transition(struct ieee80211_sta *sta, bool start)
{

	/* [LNX] net/mac80211/sta_info.c: drive a station's power-save state.
	 * This port has no PS state machine (the interface's own PS calls are
	 * refusals here). */
	(void)sta;
	(void)start;
	return -EOPNOTSUPP;
}

void
ieee80211_sta_recalc_aggregates(struct ieee80211_sta *pubsta)
{

	/* [LNX] net/mac80211/sta_info.c: recompute the A-MPDU limits.  No
	 * aggregation session exists in this port, so the limits it would
	 * recompute are never read. */
	(void)pubsta;
}

/* ------------------------------------------------------------------ */
/* frame builders the bridge owns                                      */
/* ------------------------------------------------------------------ */

/*
 * The interface builds a probe request, a PS-Poll and a beacon from the
 * driver's own state.  This port builds those frames on the bridge's side -
 * net80211's transmit path, which is where the interface's own builder sits
 * upstream too - and the driver never needs them.  The one caller that reaches
 * these is the firmware offload template registration, which asks the stack for
 * a frame to hand to the chip; a NULL answer makes that registration report a
 * failure, which is what an image without the offload feature should say.
 */
struct sk_buff *
ieee80211_probereq_get(struct ieee80211_hw *hw, const u8 *src_addr,
    const u8 *ssid, size_t ssid_len, size_t tailroom)
{

	(void)hw;
	(void)src_addr;
	(void)ssid;
	(void)ssid_len;
	(void)tailroom;
	return NULL;
}

struct sk_buff *
ieee80211_pspoll_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{

	(void)hw;
	(void)vif;
	return NULL;
}

struct sk_buff *
ieee80211_beacon_get_tim(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    u16 *tim_offset, u16 *tim_length, unsigned int link_id)
{

	(void)hw;
	(void)vif;
	(void)tim_offset;
	(void)tim_length;
	(void)link_id;
	/* the same answer ieee80211_beacon_get gives: AP mode is a non-goal */
	return NULL;
}
