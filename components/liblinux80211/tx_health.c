/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    tx_health.c
 * @brief   The transmit path's health counters, as link seams.
 *
 * The AP round's data plane stops after a few hundred frames: the skb pool
 * fills, the egress reports failures, and the AP's beacons stop.  Two answers
 * are possible and only the driver's own calls tell them apart - either the
 * driver never reports transmit completions (the pool has nothing to return),
 * or it does and something else holds the buffers.  These wraps count the
 * driver -> mac80211 traffic-status calls and the queue stop/wake calls, which
 * is the whole question, without editing the stack or the driver.
 *
 * Link seams, same as tx_dequeue_context.c: the stack keeps its own behaviour,
 * the port states what it observes.
 */

#include "linuxkpi_core.h"

#include <net/mac80211.h>

extern unsigned int lkpi_tx_dequeue_depth;

struct lkpi_tx_health {
	unsigned long	tx_status;
	unsigned long	stop_queue;
	unsigned long	wake_queue;
	unsigned long	dequeue_calls;
	unsigned long	dequeue_frames;
	unsigned long	status_mgmt;
	unsigned long	status_data;
	unsigned long	iwl_tx;
	unsigned long	hcmd;
	unsigned long	hcmd_blockq;
	unsigned long	hcmd_async;
	unsigned long	mgmt_tx;	/* hostapd's send_mlme calls */
	unsigned long	mgmt_beacon;	/* ... of which beacons */
};

static struct lkpi_tx_health tx_health;

void
lkpi_tx_health_get(struct lkpi_tx_health *out)
{
	if (out != NULL)
		*out = tx_health;
}

void
lkpi_tx_health_mgmt(const void *frame, unsigned int len)
{
	const unsigned char *fc = frame;

	if (fc == NULL || len < 2u)
		return;
	tx_health.mgmt_tx++;
	if (((fc[0] >> 4) & 0xfu) == 8u)
		tx_health.mgmt_beacon++;
}

extern void __real_ieee80211_tx_status_skb(struct ieee80211_hw *hw,
					    struct sk_buff *skb);
extern void __real_ieee80211_stop_queue(struct ieee80211_hw *hw, int queue);
extern void __real_ieee80211_wake_queue(struct ieee80211_hw *hw, int queue);

extern struct sk_buff *__real_ieee80211_tx_dequeue(struct ieee80211_hw *hw,
						    struct ieee80211_txq *txq);

/* the driver's pull from mac80211: if this stops being called, frames are
 * stuck in the stack's queues rather than in the driver */
struct sk_buff *
__wrap_ieee80211_tx_dequeue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	struct sk_buff *skb;

	/* the stack asserts the driver dequeues in softirq context; this port's
	 * dequeue runs in iwlwifi's own deferred context (see
	 * tx_dequeue_context.c) */
	lkpi_tx_dequeue_depth++;
	tx_health.dequeue_calls++;
	skb = __real_ieee80211_tx_dequeue(hw, txq);
	lkpi_tx_dequeue_depth--;
	if (skb != NULL)
		tx_health.dequeue_frames++;
	return skb;
}

/* the driver's completion entry: net/mac80211/status.c:1146, called from
 * iwlwifi's tx response handler (mvm/tx.c) for every completed frame */
void
__wrap_ieee80211_tx_status_skb(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr;

	tx_health.tx_status++;
	if (skb != NULL && skb->data != NULL) {
		hdr = (const struct ieee80211_hdr *)skb->data;
		if (ieee80211_is_mgmt(hdr->frame_control))
			tx_health.status_mgmt++;
		else if (ieee80211_is_data(hdr->frame_control))
			tx_health.status_data++;
	}
	__real_ieee80211_tx_status_skb(hw, skb);
}

void
__wrap_ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
	tx_health.stop_queue++;
	__real_ieee80211_stop_queue(hw, queue);
}

void
__wrap_ieee80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
	tx_health.wake_queue++;
	__real_ieee80211_wake_queue(hw, queue);
}

/*
 * The driver's own hand-off to the transport and its host commands.  When TX
 * dies while the transport still receives frames, the frames are sitting in a
 * queue whose doorbell is not being rung - iwlwifi skips the ring write while
 * txq->block is set, and txq->block is raised exactly by the host commands that
 * carry CMD_BLOCK_TXQS (BIT(3)) and cleared when their response arrives.
 */
int __real_iwl_trans_pcie_tx(void *trans, struct sk_buff *skb, void *dev_cmd,
			     int txq_id);
int __real_iwl_trans_pcie_send_hcmd(void *trans, void *cmd);
int __real_iwl_mvm_send_cmd(void *mvm, void *cmd);

struct lkpi_iwl_host_cmd_view {
	const void	*data[2];
	void		*resp_pkt;
	unsigned long	_rx_page_addr;
	u32		_rx_page_order;
	u32		flags;
	u32		id;
};

int
__wrap_iwl_trans_pcie_tx(void *trans, struct sk_buff *skb, void *dev_cmd,
			 int txq_id)
{
	tx_health.iwl_tx++;
	return __real_iwl_trans_pcie_tx(trans, skb, dev_cmd, txq_id);
}

int
__wrap_iwl_trans_pcie_send_hcmd(void *trans, void *cmd)
{
	const struct lkpi_iwl_host_cmd_view *h = cmd;

	tx_health.hcmd++;
	if (h != NULL) {
		if ((h->flags & (1u << 3)) != 0u)
			tx_health.hcmd_blockq++;
		if ((h->flags & 1u) != 0u)
			tx_health.hcmd_async++;
	}
	return __real_iwl_trans_pcie_send_hcmd(trans, cmd);
}

int
__wrap_iwl_mvm_send_cmd(void *mvm, void *cmd)
{
	return __real_iwl_mvm_send_cmd(mvm, cmd);
}
