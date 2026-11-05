/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_tx_ba.c
 * @brief   The TX block-ack wire exchange, once for both ports.
 *
 * The state machine is the kernel port's (linux-ko ports/mac80211/mac80211_ba.c,
 * TX half): the same 64-frame window request, the same 100 ms retry with two
 * attempts, the same dialog-token increment, the same ADDBA frame bytes.  The
 * receive reorder window is not here; each port keeps its own.
 *
 * Locking: the session table is under ba.lock.  The driver call (ampdu) and the
 * management transmit can sleep, so they are made after the lock is dropped.
 */

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/ieee80211.h>
#include <net/mac80211.h>

#include "wlan_tx_ba.h"

#define TX_BA_TID_MAX	16
#define TX_BA_WIN	64
#define TX_BA_RETRY_MS	100
#define TX_BA_RETRIES	2

#define TX_BA_IDLE	0
#define TX_BA_PENDING	1
#define TX_BA_ACTIVE	2

struct wlan_tx_ba_tid {
	int		state;
	u16		dialog;
	u16		buf;
	unsigned int	retries;
};

static struct {
	struct ieee80211_hw		*hw;
	const struct wlan_tx_ba_ops	*ops;
	void				*arg;
	spinlock_t			lock;
	struct wlan_tx_ba_tid		tx[TX_BA_TID_MAX];
	unsigned int			dialog;
	struct work_struct		tx_work;
	struct delayed_work		tx_retry;
} ba;

static int
tx_ba_ampdu(enum ieee80211_ampdu_mlme_action action, u16 tid, u16 ssn,
    u16 buf, bool amsdu, u16 timeout)
{
	if (ba.ops == NULL || ba.ops->ampdu == NULL)
		return -EOPNOTSUPP;
	return ba.ops->ampdu(ba.arg, action, tid, ssn, buf, amsdu, timeout);
}

static void
tx_ba_note(int evt)
{
	if (ba.ops != NULL && ba.ops->note != NULL)
		ba.ops->note(ba.arg, evt);
}

static u16
tx_ba_next_ssn(u16 tid)
{
	if (ba.ops == NULL || ba.ops->last_tx_seq == NULL)
		return 0;
	return (u16)(ba.ops->last_tx_seq(ba.arg, (int)tid) + 1);
}

static void
tx_ba_send_addba_request(u16 tid, u16 ssn, u16 dialog)
{
	const uint8_t *ra, *ta;
	u8 f[33];
	u16 params;

	if (ba.ops == NULL || ba.ops->mgmt_tx == NULL ||
	    ba.ops->sta_addr == NULL || ba.ops->local_addr == NULL)
		return;
	ra = ba.ops->sta_addr(ba.arg);
	ta = ba.ops->local_addr(ba.arg);
	if (ra == NULL || ta == NULL)
		return;
	memset(f, 0, sizeof(f));
	f[0] = 0xd0;			/* action */
	memcpy(f + 4, ra, 6);		/* RA */
	memcpy(f + 10, ta, 6);		/* TA */
	memcpy(f + 16, ra, 6);		/* DA */
	f[24] = 3;			/* Block Ack category */
	f[25] = 0;			/* ADDBA Request */
	f[26] = (u8)dialog;
	params = (u16)(0x0002 | (tid << 2) | (TX_BA_WIN << 6));
	f[27] = (u8)params;
	f[28] = (u8)(params >> 8);
	f[29] = 0;			/* timeout 0 */
	f[30] = 0;
	f[31] = (u8)((ssn << 4) & 0xff);
	f[32] = (u8)((ssn >> 4) & 0xff);
	(void)ba.ops->mgmt_tx(ba.arg, f, sizeof(f));
}

static void
tx_ba_handle_resp(const uint8_t *d, unsigned int len)
{
	u16 status, params, tid, buf;
	unsigned long flags;
	int ok = 0;

	if (len < 33)
		return;
	status = (u16)(d[27] | (d[28] << 8));
	params = (u16)(d[29] | (d[30] << 8));
	tid = (u16)((params >> 2) & 0x0f);
	buf = (u16)((params >> 6) & 0x03ff);
	if (tid >= TX_BA_TID_MAX)
		return;
	spin_lock_irqsave(&ba.lock, flags);
	if (ba.tx[tid].state == TX_BA_PENDING) {
		if (status == 0) {
			ba.tx[tid].state = TX_BA_ACTIVE;
			ba.tx[tid].buf = buf;
			ok = 1;
		} else {
			ba.tx[tid].state = TX_BA_IDLE;
		}
	}
	spin_unlock_irqrestore(&ba.lock, flags);
	cancel_delayed_work(&ba.tx_retry);
	if (ok) {
		tx_ba_note(WLAN_TX_BA_NOTE_OPERATIONAL);
		(void)tx_ba_ampdu(IEEE80211_AMPDU_TX_OPERATIONAL, tid, 0, buf,
		    false, 0);
	} else if (status != 0) {
		(void)tx_ba_ampdu(IEEE80211_AMPDU_TX_STOP_CONT, tid, 0, 0, false,
		    0);
		tx_ba_note(WLAN_TX_BA_NOTE_STOP);
	}
}

static void
tx_ba_handle_delba(const uint8_t *d, unsigned int len)
{
	u16 params, tid;
	unsigned long flags;

	if (len < 28)
		return;
	params = (u16)(d[26] | (d[27] << 8));
	tid = (u16)(params & 0x0f);
	if (tid >= TX_BA_TID_MAX)
		return;
	spin_lock_irqsave(&ba.lock, flags);
	ba.tx[tid].state = TX_BA_IDLE;
	spin_unlock_irqrestore(&ba.lock, flags);
	cancel_delayed_work(&ba.tx_retry);
	(void)tx_ba_ampdu(IEEE80211_AMPDU_TX_STOP_CONT, tid, 0, 0, false, 0);
	tx_ba_note(WLAN_TX_BA_NOTE_STOP);
}

static void
tx_ba_tx_work(struct work_struct *work)
{
	unsigned long flags;
	int t;

	(void)work;
	if (ba.ops == NULL)
		return;
	for (t = 0; t < TX_BA_TID_MAX; t++) {
		u16 ssn, dialog;
		int send = 0;

		spin_lock_irqsave(&ba.lock, flags);
		if (ba.tx[t].state == TX_BA_PENDING &&
		    ba.tx[t].retries == 0) {
			ssn = tx_ba_next_ssn((u16)t);
			ba.tx[t].dialog = (u16)(++ba.dialog & 0xff);
			dialog = ba.tx[t].dialog;
			send = 1;
		}
		spin_unlock_irqrestore(&ba.lock, flags);
		if (!send)
			continue;
		/* The driver's TX_START first: it may refuse (no queue, no
		 * station room), and a request sent for a session the driver
		 * never armed would be a lie on the air. */
		if (tx_ba_ampdu(IEEE80211_AMPDU_TX_START, (u16)t, ssn,
		    TX_BA_WIN, false, 0) < 0) {
			spin_lock_irqsave(&ba.lock, flags);
			ba.tx[t].state = TX_BA_IDLE;
			spin_unlock_irqrestore(&ba.lock, flags);
			tx_ba_note(WLAN_TX_BA_NOTE_STOP);
			continue;
		}
		tx_ba_note(WLAN_TX_BA_NOTE_START);
		tx_ba_send_addba_request((u16)t, ssn, dialog);
		schedule_delayed_work(&ba.tx_retry,
		    msecs_to_jiffies(TX_BA_RETRY_MS));
	}
}

static void
tx_ba_retry_work(struct work_struct *work)
{
	unsigned long flags;
	int t, again = 0;

	(void)work;
	if (ba.ops == NULL)
		return;
	for (t = 0; t < TX_BA_TID_MAX; t++) {
		int fail = 0;
		u16 ssn, dialog;

		spin_lock_irqsave(&ba.lock, flags);
		if (ba.tx[t].state != TX_BA_PENDING) {
			spin_unlock_irqrestore(&ba.lock, flags);
			continue;
		}
		ba.tx[t].retries++;
		if (ba.tx[t].retries > TX_BA_RETRIES) {
			ba.tx[t].state = TX_BA_IDLE;
			fail = 1;
		} else {
			again = 1;
		}
		ssn = tx_ba_next_ssn((u16)t);
		ba.tx[t].dialog = (u16)(++ba.dialog & 0xff);
		dialog = ba.tx[t].dialog;
		spin_unlock_irqrestore(&ba.lock, flags);
		if (fail) {
			(void)tx_ba_ampdu(IEEE80211_AMPDU_TX_STOP_CONT, (u16)t,
			    0, 0, false, 0);
			tx_ba_note(WLAN_TX_BA_NOTE_STOP);
			continue;
		}
		tx_ba_send_addba_request((u16)t, ssn, dialog);
	}
	if (again)
		schedule_delayed_work(&ba.tx_retry,
		    msecs_to_jiffies(TX_BA_RETRY_MS));
}

int
wlan_tx_ba_init(struct ieee80211_hw *hw, const struct wlan_tx_ba_ops *ops,
    void *arg)
{
	memset(&ba, 0, sizeof(ba));
	ba.hw = hw;
	ba.ops = ops;
	ba.arg = arg;
	spin_lock_init(&ba.lock);
	INIT_WORK(&ba.tx_work, tx_ba_tx_work);
	INIT_DELAYED_WORK(&ba.tx_retry, tx_ba_retry_work);
	return 0;
}

void
wlan_tx_ba_detach(struct ieee80211_hw *hw)
{
	unsigned long flags;
	int t;

	(void)hw;
	if (ba.ops == NULL)
		return;
	cancel_work_sync(&ba.tx_work);
	cancel_delayed_work_sync(&ba.tx_retry);
	spin_lock_irqsave(&ba.lock, flags);
	for (t = 0; t < TX_BA_TID_MAX; t++)
		ba.tx[t].state = TX_BA_IDLE;
	spin_unlock_irqrestore(&ba.lock, flags);
	ba.hw = NULL;
	ba.ops = NULL;
	ba.arg = NULL;
}

int
wlan_tx_ba_start(struct ieee80211_hw *hw, struct ieee80211_sta *sta, u16 tid,
    u16 timeout)
{
	unsigned long flags;

	(void)hw;
	(void)timeout;
	if (ba.ops == NULL || sta == NULL || tid >= TX_BA_TID_MAX)
		return -EINVAL;
	if (!sta->deflink.ht_cap.ht_supported)
		return -EINVAL;
	spin_lock_irqsave(&ba.lock, flags);
	if (ba.tx[tid].state != TX_BA_IDLE) {
		spin_unlock_irqrestore(&ba.lock, flags);
		return 0;
	}
	ba.tx[tid].state = TX_BA_PENDING;
	ba.tx[tid].retries = 0;
	spin_unlock_irqrestore(&ba.lock, flags);
	schedule_work(&ba.tx_work);
	return 0;
}

void
wlan_tx_ba_stop(struct ieee80211_hw *hw, const uint8_t *ra, u16 tid)
{
	unsigned long flags;

	(void)hw;
	(void)ra;
	if (tid >= TX_BA_TID_MAX)
		return;
	spin_lock_irqsave(&ba.lock, flags);
	ba.tx[tid].state = TX_BA_IDLE;
	spin_unlock_irqrestore(&ba.lock, flags);
	cancel_delayed_work(&ba.tx_retry);
}

int
wlan_tx_ba_rx(struct ieee80211_hw *hw, const uint8_t *frame, unsigned int len)
{
	(void)hw;
	if (ba.ops == NULL || frame == NULL || len < 26)
		return 0;
	if ((frame[0] & 0x0c) != 0x00 || (frame[0] & 0xf0) != 0xd0)
		return 0;
	if (frame[24] != 3)
		return 0;
	switch (frame[25]) {
	case 1:
		tx_ba_handle_resp(frame, len);
		return 1;
	case 2:
		/*
		 * The TX session ends here, but the frame is offered on: a port
		 * with a receive reorder window must see the DELBA too.  The
		 * caller's own receive path consumes it.
		 */
		tx_ba_handle_delba(frame, len);
		return 0;
	default:
		return 0;
	}
}
