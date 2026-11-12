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

#include "linux80211/diag.h"

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
	unsigned long	ps_sleep;	/* sta marked dozing (AP mode only) */
	unsigned long	ps_wake;	/* sta marked awake */
	unsigned long	ps_buffered;	/* ieee80211_sta_set_buffered calls */
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

/*
 * The AP-only power-save path.  iwlwifi sets IEEE80211_HW_AP_LINK_PS when the
 * firmware advertises STA_PM_NOTIF: from then on mac80211 does NOT look at the
 * PM bit itself, it waits for the driver to call ieee80211_sta_ps_transition().
 * A station marked dozing stops receiving unicast frames - mac80211 buffers
 * them in the station's ps_tx_buf - so a missing "awake" leaves the data path
 * dead with no completion, no error and a filling skb pool.  That path exists
 * only for AP vifs (iwlwifi's iwl_mvm_sta_pm_notif returns early for others),
 * which is the one structural difference from the STA rounds that ran for
 * hours.
 */
extern int __real_ieee80211_sta_ps_transition(struct ieee80211_sta *sta,
					      bool start);
extern bool __real_ieee80211_sta_set_buffered(struct ieee80211_sta *sta,
					      u8 tid, bool buffered);

int
__wrap_ieee80211_sta_ps_transition(struct ieee80211_sta *sta, bool start)
{
	if (start)
		tx_health.ps_sleep++;
	else
		tx_health.ps_wake++;
	return __real_ieee80211_sta_ps_transition(sta, start);
}

bool
__wrap_ieee80211_sta_set_buffered(struct ieee80211_sta *sta, u8 tid,
				  bool buffered)
{
	tx_health.ps_buffered++;
	return __real_ieee80211_sta_set_buffered(sta, tid, buffered);
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

/*
 * The A4 stall round's MMIO breadcrumbs.  The interrupt freeze's two host-side
 * candidates are "the driver never re-wrote CSR_INT_MASK" and "the threaded
 * half never returned"; a count of every mask/ack/doorbell access, with the
 * last four carrying their caller and the NAPI poll depth, answers both without
 * touching iwlwifi's bytes.  The four offsets are the adapter's interrupt-trace
 * registers (iwl-csr.h) and the transmit doorbell (HBUS_TARG_WRPTR).
 */
extern unsigned int lkpi_napi_poll_depth;

static struct lkpi_mmio_health lkpi_mmio_health;

void
lkpi_mmio_health_get(struct lkpi_mmio_health *out)
{

	if (out != NULL)
		*out = lkpi_mmio_health;
}

static void
lkpi_mmio_note(uint32_t ofs, uint32_t val, int write, const void *caller)
{
	struct lkpi_mmio_op *op;

	switch (ofs) {
	case LKPI_MMIO_CSR_INT:
	case LKPI_MMIO_CSR_INT_MASK:
	case LKPI_MMIO_CSR_FH_INT_STATUS:
	case LKPI_MMIO_HBUS_TARG_WRPTR:
		break;
	default:
		return;
	}

	if (write) {
		if (ofs == LKPI_MMIO_CSR_INT_MASK) {
			lkpi_mmio_health.mask_writes++;
			if (val == 0u)
				lkpi_mmio_health.mask_zero_writes++;
		} else if (ofs == LKPI_MMIO_CSR_INT) {
			lkpi_mmio_health.ack_writes++;
		} else if (ofs == LKPI_MMIO_HBUS_TARG_WRPTR) {
			lkpi_mmio_health.doorbell_writes++;
		}
	} else if (ofs == LKPI_MMIO_CSR_INT) {
		lkpi_mmio_health.int_reads++;
	} else if (ofs == LKPI_MMIO_CSR_FH_INT_STATUS) {
		lkpi_mmio_health.fh_reads++;
	}

	op = &lkpi_mmio_health.ring[lkpi_mmio_health.ring_next];
	op->tick = jiffies;
	op->ofs = ofs;
	op->val = val;
	op->ctx = lkpi_napi_poll_depth;
	op->caller = caller;
	op->write = (uint8_t)(write ? 1 : 0);
	lkpi_mmio_health.ring_next =
	    (lkpi_mmio_health.ring_next + 1u) % LKPI_MMIO_RING_MAX;
	if (lkpi_mmio_health.ring_count < LKPI_MMIO_RING_MAX)
		lkpi_mmio_health.ring_count++;
}

extern u32 __real_iwl_read32(void *trans, u32 ofs);
extern void __real_iwl_write32(void *trans, u32 ofs, u32 val);
extern void __real_iwl_trans_pci_interrupts(void *trans, bool enable);

u32
__wrap_iwl_read32(void *trans, u32 ofs)
{
	u32 val = __real_iwl_read32(trans, ofs);

	lkpi_mmio_note(ofs, val, 0, __builtin_return_address(0));
	return val;
}

void
__wrap_iwl_write32(void *trans, u32 ofs, u32 val)
{

	lkpi_mmio_note(ofs, val, 1, __builtin_return_address(0));
	__real_iwl_write32(trans, ofs, val);
}

void
__wrap_iwl_trans_pci_interrupts(void *trans, bool enable)
{

	if (enable)
		lkpi_mmio_health.int_enable_calls++;
	else
		lkpi_mmio_health.int_disable_calls++;
	__real_iwl_trans_pci_interrupts(trans, enable);
}
