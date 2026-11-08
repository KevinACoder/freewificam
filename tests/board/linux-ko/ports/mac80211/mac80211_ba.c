/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    mac80211_ba.c
 * @brief   Block-ack state machine for the kernelmod line.
 *
 * See mac80211_ba.h for why this exists.  The pieces:
 *   - RX: answer the AP's ADDBA Request, hand RX_START to the driver, and
 *     reorder QoS MPDUs by their 12-bit sequence number (64-slot window);
 *   - TX: turn the driver's ieee80211_start_tx_ba_session() into a real ADDBA
 *     Request, wait for the AP's Response, then TX_OPERATIONAL;
 *   - DELBA/BAR teardown.
 *
 * The module lives in the linux-ko ports tree because it needs both worlds:
 * the driver's ieee80211_ops (ampdu_action) and the net80211 bridge's
 * management window (wlan_bridge_mgmt_tx) and TID sequence memory.
 *
 * Locking: BA state is under ba.lock; the bridge calls (mgmt_tx, deliver) are
 * made after dropping it because they may sleep/allocate.
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/ieee80211.h>
#include <net/mac80211.h>

#include "wlan_linux.h"
#include "mac80211_ba.h"
#include "wlan_bridge.h"

#define BA_TID_MAX	16
#define BA_WIN		64
#define BA_RETRY_MS	100
#define BA_RETRIES	2

#define BA_TX_IDLE	0
#define BA_TX_PENDING	1
#define BA_TX_ACTIVE	2

struct ba_rx {
	int		active;
	u16		head;
	u16		win;
	struct sk_buff	*slot[BA_WIN];
	u16		sn[BA_WIN];
};

struct ba_tx {
	int		state;
	u16		dialog;
	u16		buf;
	unsigned int	retries;
};

static struct {
	struct ieee80211_hw	*hw;
	spinlock_t		lock;
	struct ba_rx		rx[BA_TID_MAX];
	struct ba_tx		tx[BA_TID_MAX];
	unsigned int		dialog;
	/* W7-12 census (CONFIG_WLAN_KO_CENSUS): see mac80211_ba.h */
	int			rx_start_rc;
	unsigned int		rx_calls, rx_fail, rx_stale;
	int			rx_last_rc;
	u16			rx_last_tid, rx_last_ssn, rx_last_win;
	unsigned int		hold;
	unsigned int		flush;
	unsigned int		drop_old;
	unsigned int		drop_dup;
	struct work_struct	tx_work;
	struct delayed_work	tx_retry;
	struct delayed_work	rx_timeout;
} ba;

/* 12-bit sequence comparison (IEEE 802.11's modulo-4096 serial numbers). */
static bool
ba_after(u16 a, u16 b)
{
	return a != b && ((u16)(a - b) & 0x0fff) < 0x800;
}

static void ba_send_addba_request(struct wlan_linux_hw *w, u16 tid, u16 ssn,
    u16 dialog);

static int
ba_ampdu(struct wlan_linux_hw *w, enum ieee80211_ampdu_mlme_action action,
    u16 tid, u16 ssn, u16 buf, bool amsdu, u16 timeout)
{
	struct ieee80211_ampdu_params params;
	int rc;

	/*
	 * W7-13 census: the window state alone cannot tell "the driver refused
	 * the BA" from "the driver took it and the traffic never came".  Record
	 * every RX_START call, whether the driver entry existed at that moment,
	 * and the last call's parameters.  Read-only.
	 */
	if (action == IEEE80211_AMPDU_RX_START) {
		ba.rx_calls++;
		if (w == NULL || w->vif == NULL || w->sta == NULL ||
		    w->ops == NULL || w->ops->ampdu_action == NULL)
			ba.rx_stale++;
	}
	if (w == NULL || w->vif == NULL || w->sta == NULL ||
	    w->ops == NULL || w->ops->ampdu_action == NULL)
		return -EOPNOTSUPP;
	memset(&params, 0, sizeof(params));
	params.action = action;
	params.sta = w->sta;
	params.tid = tid;
	params.ssn = ssn;
	params.buf_size = buf;
	params.amsdu = amsdu;
	params.timeout = timeout;
	rc = w->ops->ampdu_action(&w->hw, w->vif, &params);
	if (action == IEEE80211_AMPDU_RX_START) {
		ba.rx_last_rc = rc;
		ba.rx_last_tid = tid;
		ba.rx_last_ssn = ssn;
		ba.rx_last_win = buf;
		if (rc < 0)
			ba.rx_fail++;
	}
	return rc;
}

/*
 * W7-14 F2: answer the AP's ADDBA Request with the WLAN_STATUS_REQUEST_DECLINED
 * of IEEE 802.11-2020 9.6.5.1 instead of accepting it.  The reference station
 * accepts (status 0) and then emits a BlockAck for every A-MPDU; this line
 * accepts and emits none, which is the open issue.  Declining moves the AP to
 * single-frame delivery through the exact same receive path, so the two arms
 * differ only in "does the AP aggregate" -- if the downlink rate follows the
 * aggregation state and not our HT table, the 6 Mbit/s residual is downstream
 * of the missing BlockAck; if the rate stays at MCS1-3, the missing BlockAck
 * is a symptom, not the owner.
 */
static bool ba_decline_rx;
module_param_named(ba_decline_rx, ba_decline_rx, bool, 0644);
MODULE_PARM_DESC(ba_decline_rx,
    "1 = decline the AP's ADDBA Request (single-frame downlink, lab probe)");

/* Answer an ADDBA Request in the same dialog, with status 0 and the request's
 * own parameter set (TID/buffer/policy). */
static void
ba_send_response(struct ieee80211_hw *hw, const u8 *req, size_t len)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	u8 rsp[33];

	if (len < 33 || w == NULL || w->sta == NULL)
		return;
	memset(rsp, 0, sizeof(rsp));
	rsp[0] = 0xd0;			/* action */
	rsp[1] = 0;
	/* receiver = the request's transmitter (the AP), transmitter = us */
	memcpy(rsp + 4, req + 10, 6);
	memcpy(rsp + 10, req + 4, 6);
	memcpy(rsp + 16, req + 16, 6);
	rsp[24] = 3;			/* Block Ack category */
	rsp[25] = 1;			/* ADDBA Response */
	rsp[26] = req[26];		/* dialog token */
	/* 0 = success, 37 = WLAN_STATUS_REQUEST_DECLINED */
	rsp[27] = ba_decline_rx ? 37 : 0;
	rsp[28] = 0;
	rsp[29] = req[27];		/* parameter set, echoed */
	rsp[30] = req[28];
	rsp[31] = 0;			/* timeout */
	rsp[32] = 0;
	(void)wlan_bridge_mgmt_tx(rsp, sizeof(rsp));
}

static void
ba_send_delba(struct ieee80211_hw *hw, u16 tid, bool tx)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	u8 f[30];

	if (w == NULL || w->sta == NULL)
		return;
	memset(f, 0, sizeof(f));
	f[0] = 0xd0;
	memcpy(f + 4, w->sta->addr, 6);
	memcpy(f + 10, w->vif != NULL ? w->vif->addr : w->hw.wiphy->perm_addr,
	    6);
	memcpy(f + 16, w->sta->addr, 6);
	f[24] = 3;			/* Block Ack */
	f[25] = 2;			/* DELBA */
	/* DELBA Parameter Set: bits 0-3 TID, bit 4 initiator role */
	f[26] = (u8)(tid | (tx ? 0x00 : 0x10));
	f[27] = 0;
	f[28] = 0;			/* reason: unspecified */
	f[29] = 0;
	(void)wlan_bridge_mgmt_tx(f, sizeof(f));
}

static void
ba_rx_free_tid(u16 tid)
{
	unsigned int i;

	for (i = 0; i < BA_WIN; i++) {
		if (ba.rx[tid].slot[i] != NULL) {
			dev_kfree_skb_any(ba.rx[tid].slot[i]);
			ba.rx[tid].slot[i] = NULL;
		}
		ba.rx[tid].sn[i] = 0;
	}
	ba.rx[tid].active = 0;
	ba.rx[tid].head = 0;
	ba.rx[tid].win = 0;
}

static void
ba_handle_addba_req(struct ieee80211_hw *hw, const u8 *d, size_t len)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	u16 params, tid, buf, ssc, ssn, win;
	unsigned long flags;

	if (len < 33)
		return;
	params = (u16)(d[27] | (d[28] << 8));
	tid = (params >> 2) & 0x0f;
	buf = (params >> 6) & 0x03ff;
	ssc = (u16)(d[31] | (d[32] << 8));
	ssn = ssc >> 4;
	win = buf == 0 ? BA_WIN : min_t(u16, buf, BA_WIN);
	if (tid >= BA_TID_MAX)
		return;
	if (ba_decline_rx) {
		/* No reorder window, no driver RX_START: the AP must fall back
		 * to single MPDUs, exactly as it does for a station that does
		 * not want aggregation. */
		ba_send_response(hw, d, len);
		return;
	}
	spin_lock_irqsave(&ba.lock, flags);
	ba_rx_free_tid(tid);
	ba.rx[tid].active = 1;
	ba.rx[tid].head = ssn;
	ba.rx[tid].win = win;
	spin_unlock_irqrestore(&ba.lock, flags);
	wlan_bridge_ba_note(WLAN_BRIDGE_BA_RX_START);
	if (w != NULL)
		ba.rx_start_rc = ba_ampdu(w, IEEE80211_AMPDU_RX_START, tid, ssn,
		    win, (params & 0x0001) != 0, 0);
	ba_send_response(hw, d, len);
}

static void
ba_handle_addba_resp(struct ieee80211_hw *hw, const u8 *d, size_t len)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	u16 status, params, tid, buf;
	unsigned long flags;
	int ok = 0;

	if (len < 33)
		return;
	status = (u16)(d[27] | (d[28] << 8));
	params = (u16)(d[29] | (d[30] << 8));
	tid = (params >> 2) & 0x0f;
	buf = (params >> 6) & 0x03ff;
	if (tid >= BA_TID_MAX)
		return;
	spin_lock_irqsave(&ba.lock, flags);
	if (ba.tx[tid].state == BA_TX_PENDING) {
		if (status == 0) {
			ba.tx[tid].state = BA_TX_ACTIVE;
			ba.tx[tid].buf = buf;
			ok = 1;
		} else {
			ba.tx[tid].state = BA_TX_IDLE;
		}
	}
	spin_unlock_irqrestore(&ba.lock, flags);
	cancel_delayed_work(&ba.tx_retry);
	if (ok) {
		wlan_bridge_ba_note(WLAN_BRIDGE_BA_TX_OPERATIONAL);
		if (w != NULL)
			(void)ba_ampdu(w, IEEE80211_AMPDU_TX_OPERATIONAL, tid, 0,
			    buf, false, 0);
	} else if (status != 0 && w != NULL) {
		(void)ba_ampdu(w, IEEE80211_AMPDU_TX_STOP_CONT, tid, 0, 0, false,
		    0);
		wlan_bridge_ba_note(WLAN_BRIDGE_BA_STOP);
	}
}

static void
ba_handle_delba(struct ieee80211_hw *hw, const u8 *d, size_t len)
{
	struct wlan_linux_hw *w = wlan_hw_of(hw);
	u16 params, tid;
	unsigned long flags;

	if (len < 28)
		return;
	params = (u16)(d[26] | (d[27] << 8));
	/* DELBA parameter set: TID in bits 15:12, initiator in bit 11
	 * (IEEE80211_DELBA_PARAM_TID_MASK 0xf000; mac80211 ht.c:467).  Reading
	 * bits 3:0 made the AP's TID 7 DELBA tear down the TID 0 data session. */
	tid = (params & 0xf000) >> 12;
	if (tid >= BA_TID_MAX)
		return;
	spin_lock_irqsave(&ba.lock, flags);
	ba_rx_free_tid(tid);
	if (ba.tx[tid].state != BA_TX_IDLE) {
		ba.tx[tid].state = BA_TX_IDLE;
	}
	spin_unlock_irqrestore(&ba.lock, flags);
	wlan_bridge_ba_note(WLAN_BRIDGE_BA_STOP);
	if (w != NULL) {
		(void)ba_ampdu(w, IEEE80211_AMPDU_RX_STOP, tid, 0, 0, false, 0);
		(void)ba_ampdu(w, IEEE80211_AMPDU_TX_STOP_CONT, tid, 0, 0, false, 0);
	}
}

static int
ba_handle_action(struct ieee80211_hw *hw, const u8 *d, size_t len)
{
	if (len < 26)
		return 1;
	if (d[24] != 3)		/* only Block Ack category */
		return 1;
	switch (d[25]) {
	case 0:
		ba_handle_addba_req(hw, d, len);
		break;
	case 1:
		ba_handle_addba_resp(hw, d, len);
		break;
	case 2:
		ba_handle_delba(hw, d, len);
		break;
	default:
		break;
	}
	return 1;
}

/*
 * Deliver an in-order run.  The lock is dropped before the bridge call; the
 * skb array is local, so a concurrent RX path cannot see it.
 */
static void
ba_deliver_run(struct ieee80211_hw *hw, struct sk_buff **v, int n,
    int keep_first)
{
	int i;

	for (i = 0; i < n; i++) {
		if (hw != NULL)
			wlan_adapter_deliver(hw, v[i]);
		if (!(keep_first && i == 0))
			dev_kfree_skb_any(v[i]);
	}
}

static int
ba_rx_data(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	const u8 *d = skb->data;
	u16 tid, sn, idx;
	struct sk_buff *out[BA_WIN];
	int n = 0;
	unsigned long flags;
	struct ba_rx *r;

	if (skb->len < 26)
		return 0;
	tid = d[24] & 0x0f;
	if (tid >= BA_TID_MAX)
		return 0;
	sn = (u16)((d[22] | (d[23] << 8)) >> 4);
	spin_lock_irqsave(&ba.lock, flags);
	r = &ba.rx[tid];
	if (!r->active) {
		spin_unlock_irqrestore(&ba.lock, flags);
		return 0;
	}
	if (sn == r->head) {
		/* the next in-order frame, then whatever contiguous run the
		 * reorder window already holds */
		out[n++] = skb;
		r->head = (u16)((r->head + 1) & 0x0fff);
		for (;;) {
			idx = r->head & (BA_WIN - 1);
			if (r->slot[idx] == NULL || r->sn[idx] != r->head)
				break;
			out[n++] = r->slot[idx];
			r->slot[idx] = NULL;
			r->head = (u16)((r->head + 1) & 0x0fff);
		}
		spin_unlock_irqrestore(&ba.lock, flags);
		ba_deliver_run(hw, out, n, 1);
		return 1;
	}
	if (ba_after(sn, r->head) && ((u16)(sn - r->head) & 0x0fff) < r->win) {
		idx = sn & (BA_WIN - 1);
		if (r->slot[idx] == NULL) {
			r->slot[idx] = skb;
			r->sn[idx] = sn;
			ba.hold++;
			spin_unlock_irqrestore(&ba.lock, flags);
			schedule_delayed_work(&ba.rx_timeout,
			    msecs_to_jiffies(BA_RETRY_MS));
			return 2;	/* the reorder window owns the skb */
		}
		spin_unlock_irqrestore(&ba.lock, flags);
		ba.drop_dup++;
		wlan_bridge_ba_note(WLAN_BRIDGE_BA_REORDER_DROP);
		return 1;
	}
	/* old or outside the window: the AP retransmits, the stack must not see
	 * a duplicate */
	spin_unlock_irqrestore(&ba.lock, flags);
	ba.drop_old++;
	wlan_bridge_ba_note(WLAN_BRIDGE_BA_REORDER_DROP);
	return 1;
}

static void
ba_rx_timeout_work(struct work_struct *work)
{
	struct sk_buff *out[BA_WIN];
	int n = 0, t;
	unsigned long flags;

	spin_lock_irqsave(&ba.lock, flags);
	for (t = 0; t < BA_TID_MAX; t++) {
		struct ba_rx *r = &ba.rx[t];
		u16 i, start, maxsn;

		if (!r->active)
			continue;
		/* advance to the first held slot, then flush held frames in
		 * sequence order.  A hole is dropped and counted; the AP's
		 * BlockAckReq path would otherwise wait forever. */
		for (i = 0; i < BA_WIN; i++) {
			u16 idx = (u16)((r->head + i) & (BA_WIN - 1));

			if (r->slot[idx] != NULL)
				break;
		}
		if (i == BA_WIN)
			continue;
		start = (u16)((r->head + i) & 0x0fff);
		if (start != r->head)
			wlan_bridge_ba_note(WLAN_BRIDGE_BA_REORDER_DROP);
		r->head = start;
		maxsn = start;
		for (i = 0; i < BA_WIN && n < BA_WIN; i++) {
			u16 idx = (u16)((r->head + i) & (BA_WIN - 1));

			if (r->slot[idx] != NULL) {
				out[n++] = r->slot[idx];
				r->slot[idx] = NULL;
				maxsn = r->sn[idx];
			}
		}
		r->head = (u16)((maxsn + 1) & 0x0fff);
	}
	ba.flush += (unsigned int)n;
	spin_unlock_irqrestore(&ba.lock, flags);
	ba_deliver_run((struct ieee80211_hw *)ba.hw, out, n, 0);
}

static void
ba_tx_retry_work(struct work_struct *work)
{
	struct wlan_linux_hw *w = wlan_hw_single();
	unsigned long flags;
	int t, again = 0;

	if (w == NULL || w->sta == NULL)
		return;
	for (t = 0; t < BA_TID_MAX; t++) {
		int fail = 0;
		u16 ssn, dialog;

		spin_lock_irqsave(&ba.lock, flags);
		if (ba.tx[t].state != BA_TX_PENDING) {
			spin_unlock_irqrestore(&ba.lock, flags);
			continue;
		}
		ba.tx[t].retries++;
		if (ba.tx[t].retries > BA_RETRIES) {
			ba.tx[t].state = BA_TX_IDLE;
			fail = 1;
		} else {
			again = 1;
		}
		ssn = (u16)(wlan_bridge_last_tx_seq(t) + 1);
		ba.tx[t].dialog = (u16)(++ba.dialog & 0xff);
		dialog = ba.tx[t].dialog;
		spin_unlock_irqrestore(&ba.lock, flags);
		if (fail) {
			(void)ba_ampdu(w, IEEE80211_AMPDU_TX_STOP_CONT, t, 0, 0,
			    false, 0);
			wlan_bridge_ba_note(WLAN_BRIDGE_BA_STOP);
			continue;
		}
		ba_send_addba_request(w, (u16)t, ssn, dialog);
	}
	if (again)
		schedule_delayed_work(&ba.tx_retry,
		    msecs_to_jiffies(BA_RETRY_MS));
}

static void
ba_send_addba_request(struct wlan_linux_hw *w, u16 tid, u16 ssn, u16 dialog)
{
	u8 f[33];
	u16 params;

	memset(f, 0, sizeof(f));
	f[0] = 0xd0;
	memcpy(f + 4, w->sta->addr, 6);
	memcpy(f + 10, w->vif != NULL ? w->vif->addr : w->hw.wiphy->perm_addr,
	    6);
	memcpy(f + 16, w->sta->addr, 6);
	f[24] = 3;
	f[25] = 0;			/* ADDBA Request */
	f[26] = (u8)dialog;
	params = (u16)(0x0002 | (tid << 2) | (BA_WIN << 6));
	f[27] = (u8)params;
	f[28] = (u8)(params >> 8);
	f[29] = 0;			/* timeout 0 */
	f[30] = 0;
	f[31] = (u8)((ssn << 4) & 0xff);
	f[32] = (u8)((ssn >> 4) & 0xff);
	(void)wlan_bridge_mgmt_tx(f, sizeof(f));
}

static void
ba_tx_work(struct work_struct *work)
{
	struct wlan_linux_hw *w = wlan_hw_single();
	unsigned long flags;
	int t;

	if (w == NULL || w->sta == NULL)
		return;
	for (t = 0; t < BA_TID_MAX; t++) {
		u16 ssn, dialog;
		int send = 0;

		spin_lock_irqsave(&ba.lock, flags);
		if (ba.tx[t].state == BA_TX_PENDING && ba.tx[t].retries == 0) {
			ssn = (u16)(wlan_bridge_last_tx_seq(t) + 1);
			ba.tx[t].dialog = (u16)(++ba.dialog & 0xff);
			dialog = ba.tx[t].dialog;
			send = 1;
		}
		spin_unlock_irqrestore(&ba.lock, flags);
		if (!send)
			continue;
		if (ba_ampdu(w, IEEE80211_AMPDU_TX_START, t, ssn, BA_WIN,
		    false, 0) < 0) {
			spin_lock_irqsave(&ba.lock, flags);
			ba.tx[t].state = BA_TX_IDLE;
			spin_unlock_irqrestore(&ba.lock, flags);
			wlan_bridge_ba_note(WLAN_BRIDGE_BA_STOP);
			continue;
		}
		wlan_bridge_ba_note(WLAN_BRIDGE_BA_TX_START);
		ba_send_addba_request(w, t, ssn, dialog);
		schedule_delayed_work(&ba.tx_retry,
		    msecs_to_jiffies(BA_RETRY_MS));
	}
}

int
wlan_ba_init(struct ieee80211_hw *hw)
{
	memset(&ba, 0, sizeof(ba));
	ba.hw = hw;
	spin_lock_init(&ba.lock);
	INIT_WORK(&ba.tx_work, ba_tx_work);
	INIT_DELAYED_WORK(&ba.tx_retry, ba_tx_retry_work);
	INIT_DELAYED_WORK(&ba.rx_timeout, ba_rx_timeout_work);
	return 0;
}

void
wlan_ba_detach(struct ieee80211_hw *hw)
{
	unsigned long flags;
	int t;

	(void)hw;
	cancel_work_sync(&ba.tx_work);
	cancel_delayed_work_sync(&ba.tx_retry);
	cancel_delayed_work_sync(&ba.rx_timeout);
	spin_lock_irqsave(&ba.lock, flags);
	for (t = 0; t < BA_TID_MAX; t++) {
		ba_rx_free_tid((u16)t);
		ba.tx[t].state = BA_TX_IDLE;
	}
	spin_unlock_irqrestore(&ba.lock, flags);
	ba.hw = NULL;
}

/*
 * The W7-12 census snapshot.  Taken under the window lock so a concurrent
 * receive cannot move head/win between the fields; the counters are plain
 * increments, they may lag a frame and that is fine for a census.
 */
void
wlan_ba_get_census(struct wlan_ba_census *c)
{
	unsigned long flags;
	int t;

	if (c == NULL)
		return;
	memset(c, 0, sizeof(*c));
	spin_lock_irqsave(&ba.lock, flags);
	c->rx_start_rc = ba.rx_start_rc;
	c->hold = ba.hold;
	c->flush = ba.flush;
	c->drop_old = ba.drop_old;
	c->drop_dup = ba.drop_dup;
	for (t = 0; t < BA_TID_MAX; t++) {
		if (ba.rx[t].active)
			c->active++;
	}
	c->head = ba.rx[0].head;
	c->win = ba.rx[0].win;
	c->rx_calls = ba.rx_calls;
	c->rx_fail = ba.rx_fail;
	c->rx_stale = ba.rx_stale;
	c->rx_last_rc = ba.rx_last_rc;
	c->rx_last_tid = ba.rx_last_tid;
	c->rx_last_ssn = ba.rx_last_ssn;
	c->rx_last_win = ba.rx_last_win;
	spin_unlock_irqrestore(&ba.lock, flags);
}

int
wlan_ba_rx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	const u8 *d;
	u8 fc0;

	if (hw == NULL || skb == NULL || skb->len < 24)
		return 0;
	d = skb->data;
	fc0 = d[0];
	if ((fc0 & 0x0c) == 0x00 && (fc0 & 0xf0) == 0xd0)
		return ba_handle_action(hw, d, skb->len);
	if ((fc0 & 0x0c) == 0x04 && (fc0 >> 4) == 0x08) {
		/* BlockAckReq: advance the RX window to the requested start */
		u16 ssc, tid;
		unsigned long flags;

		if (skb->len < 20)
			return 1;
		tid = d[16] & 0x0f;
		ssc = (u16)(d[18] | (d[19] << 8));
		if (tid >= BA_TID_MAX)
			return 1;
		spin_lock_irqsave(&ba.lock, flags);
		if (ba.rx[tid].active && ba_after(ssc >> 4, ba.rx[tid].head))
			ba.rx[tid].head = (u16)((ssc >> 4) & 0x0fff);
		spin_unlock_irqrestore(&ba.lock, flags);
		return 1;
	}
	if ((fc0 & 0x0c) == 0x04 && (fc0 >> 4) == 0x09)
		return 1;		/* BlockAck: our TX side, no stack consumer */
	if ((fc0 & 0x0c) == 0x08 && (fc0 & 0x80) != 0)
		return ba_rx_data(hw, skb);
	return 0;
}

int
wlan_ba_start_tx(struct ieee80211_sta *sta, u16 tid, u16 timeout)
{
	struct wlan_linux_hw *w = wlan_hw_single();
	unsigned long flags;

	(void)timeout;
	if (w == NULL || w->sta == NULL || sta == NULL || tid >= BA_TID_MAX)
		return -EINVAL;
	if (!sta->deflink.ht_cap.ht_supported)
		return -EINVAL;
	spin_lock_irqsave(&ba.lock, flags);
	if (ba.tx[tid].state != BA_TX_IDLE) {
		spin_unlock_irqrestore(&ba.lock, flags);
		return 0;
	}
	ba.tx[tid].state = BA_TX_PENDING;
	ba.tx[tid].retries = 0;
	spin_unlock_irqrestore(&ba.lock, flags);
	schedule_work(&ba.tx_work);
	return 0;
}

int
wlan_ba_stop_tx(struct ieee80211_sta *sta, u16 tid)
{
	struct wlan_linux_hw *w = wlan_hw_single();
	unsigned long flags;

	if (w == NULL || w->sta == NULL || sta == NULL || tid >= BA_TID_MAX)
		return -EINVAL;
	spin_lock_irqsave(&ba.lock, flags);
	ba.tx[tid].state = BA_TX_IDLE;
	spin_unlock_irqrestore(&ba.lock, flags);
	cancel_delayed_work(&ba.tx_retry);
	(void)ba_ampdu(w, IEEE80211_AMPDU_TX_STOP_CONT, tid, 0, 0, false, 0);
	wlan_bridge_ba_note(WLAN_BRIDGE_BA_STOP);
	ba_send_delba(&w->hw, tid, true);
	return 0;
}
