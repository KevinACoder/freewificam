/*
 * @file
 * @brief The kernelmod line's kernel-world context shared by the mac80211 substitute
 *        and the seam adapter.
 *
 * The driver-world objects live here: the ieee80211_hw the driver allocated,
 * the driver's ops table, the one station vif and the wiphy. The net80211
 * world never sees this header - it only meets the seam (components/net80211/ports/seam.h,
 * scalars and opaque handles), exactly as in the RTOS images.
 */

#ifndef _WLAN_LINUX_WLAN_H_
#define _WLAN_LINUX_WLAN_H_

#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/mac80211.h>

#include <seam.h>

/* the one adapter this module hosts; the bridge is a singleton too */
struct wlan_linux_hw {
	struct ieee80211_hw	hw;	/* must stay first: the driver's pointer */
	const struct ieee80211_ops *ops;
	struct ieee80211_vif	*vif;
	struct ieee80211_sta	*sta;
	uint8_t			bssid[6];

	/*
	 * The firmware-scan request: the adapter builds it and hands it to the
	 * driver's hw_scan; the completion arrives through the substitute's
	 * deferred scan work, which releases it.
	 */
	struct ieee80211_scan_request *hw_scan_req;
	int			hw_scanning;

	/*
	 * The adapter attach (bridge attach, netdev creation, and with them the
	 * driver's start) must not run inside the driver's own register path:
	 * the substitute's ieee80211_register_hw is called by the driver from
	 * the middle of its register_hw, and rtw89 initialises its BT
	 * coexistence after that call returns while its start reads that
	 * initialisation (measured: the start ran first, the coexistence
	 * init returned early, and the firmware never transmitted data).  The
	 * attach is therefore deferred to this work, which runs after the
	 * driver's register returned.
	 */
	struct delayed_work	attach_work;
	int			attach_rc;

	/*
	 * The default channel hw->conf.chandef points at. The driver reads
	 * chandef.chan in its receive path before any config() happened (the
	 * first beacons arrive while the interface is still in INIT), and a
	 * NULL there is a guaranteed oops in rtl8xxxu_parse_rxdesc16. The RTOS
	 * substitute keeps the same default.
	 */
	struct ieee80211_channel chan;

	/* the cfg80211 wdev a vif is embedded in, for ieee80211_vif_to_wdev */
	struct wireless_dev wdev;
	bool wiphy_registered;
	bool sta_auth_sent;
	/* the ASSOC -> AUTHORIZED step was accepted (sta_authz); the station is
	 * stepped back to ASSOC before it is wound down */
	bool sta_authorized;

	/*
	 * The connection's channel context. A driver that declares the channel
	 * context ops (rtw89 does) keeps per-context state in ctx->drv_priv and
	 * reads the vif's binding back in its station and channel paths; the
	 * adapter reserves the context at the join, the moment mac80211 binds an
	 * interface to the channel it is about to connect on. Older drivers
	 * declare no such op and never get one.
	 */
	struct ieee80211_chanctx_conf *chanctx;
	int			chanctx_assigned;

	/*
	 * The channel the bridge last tuned the radio to. The join's channel
	 * context is built from it; the scan's per-dwell tuning overwrites it
	 * many times and none of those tunings is a connection.
	 */
	uint32_t		tuned_freq;
	uint8_t			tuned_hw_value;
	/* the association's width and centre, set by the bridge's config_ht()
	 * before sta_join and preserved by the per-dwell config() calls */
	uint32_t		tuned_width;
	uint32_t		tuned_center_freq1;

	/*
	 * The queue objects the driver initialises at add time through
	 * vif->txq and sta->txq[] (rtw89 walks them and keeps its private
	 * state in their tails). No queue engine stands behind them: frames
	 * reach the driver through the seam's .tx, exactly as on the 8821CU
	 * line.
	 */
	struct ieee80211_txq	*vif_txq;
	struct ieee80211_txq	*sta_txq[IEEE80211_NUM_TIDS + 1];

	/* the association step of the station ladder has been reached */
	int			sta_joined;

	/*
	 * Deferred completions: USB URB completions run in interrupt context,
	 * while the net80211 world allocates with GFP_KERNEL and takes the port
	 * serializer. The receive/transmit-completion paths only queue here and
	 * an ordered workqueue enters the bridge in process context - the same
	 * "interrupts only defer" rule the RTOS images state.
	 */
	struct workqueue_struct *wq;
	struct work_struct	rx_work;
	struct work_struct	tx_work;
	struct work_struct	scan_work;
	struct sk_buff_head	rx_q;
	struct sk_buff_head	tx_q;

	/*
	 * The driver's own workqueue (watchdog, C2H, TXQ). It is separate from
	 * the handoff queue above so a driver work item cannot sit in front of
	 * a received frame; mac80211 gives drivers hw->workqueue for the same
	 * reason.
	 */
	struct workqueue_struct *hw_wq;

	/*
	 * The driver's transmit-queue state and the port's wake.  rtl8xxxu
	 * stops its queues when the tx-urb pool crosses the low water and
	 * wakes them above the high water; the substitute carries that pair
	 * to the bridge (which asks `queue_stopped` before a frame and drains
	 * on the wake) instead of forwarding the wake from the completion
	 * context into the serializer.
	 */
	int			queue_stopped;
	struct work_struct	tx_wake_work;
	void			(*tx_wake_cb)(void *arg);
	void			*tx_wake_arg;
	unsigned int		tx_wakes;

	/*
	 * The seam's transmit admission gauge (W6-05).  rtw88's USB path never
	 * stops its queues, so `queue_stopped` above is always false on this
	 * line and the bridge has nothing to ask: frames the seam handed over
	 * pile up as pending bulk-OUT URBs (a 32 KB aggregate buffer each, plus
	 * the frame skbs) until the device drains them.  This pair is the
	 * substitute's own bound - frames handed to the driver minus transmit
	 * statuses the driver reported back - and the adapter refuses a data
	 * frame while their difference has reached tx_inflight_max.  The refusals
	 * themselves are the bridge's tx_gated (a refusal happens at its drain,
	 * and this function is also asked by the statistics snapshot, which must
	 * not count as one).
	 */
	unsigned int		tx_handed;
	unsigned int		tx_completed;
	/* refill work items run: one per completion batch, not per frame */
	unsigned int		tx_refills;
	/*
	 * What the driver asks for, and takes, per wake: __rtw_tx_work() reads
	 * ieee80211_txq_get_depth() and then dequeues exactly that many frames
	 * (rtw88/tx.c:656).  depth_frames/depth_calls is therefore the batch size
	 * that reaches the HCI queue, and the aggregate the USB layer builds from
	 * it; a mean of one or two means the 20 KB aggregate buffer carries a
	 * single 64-byte frame and the URB round trip is paid per frame.
	 */
	unsigned int		tx_depth_calls;
	unsigned int		tx_depth_frames;
	unsigned int		tx_depth_max;
	/*
	 * Batched handoff: while tx_batch is set, seam_tx() enqueues but leaves
	 * the driver's wake to the caller (wlan_adapter_tx_kick), so the whole
	 * batch is in the txq when the driver's work finally runs.
	 */
	unsigned int		tx_batch;
	unsigned int		tx_wake_pending;
	/* protects tx_batch/tx_wake_pending/tx_batch_txq[]: the netdev work and
	 * the completion pump run on different CPUs and both open windows */
	spinlock_t		tx_batch_lock;
	/*
	 * Every txq that received a frame during the current batch.  The wake is
	 * one per batch, so it has to wake all of them: a batch that carries
	 * data frames and a management frame fills two queues, and waking only
	 * the last one leaves the other's frames in their slot with no driver
	 * work scheduled to come for them (measured 2026-11-05: duplex cell
	 * stalled halfway and the station fell off the AP).
	 */
	struct ieee80211_txq	*tx_batch_txq[8];
	unsigned int		tx_batch_txqs;

	/*
	 * The receive-shape census (W7-12, CONFIG_WLAN_KO_CENSUS).  The seam's
	 * bridge contract is one contiguous frame (`wlan_bridge_input(frame,
	 * len)` copies `len` bytes from the pointer), while a driver is free to
	 * hand up a nonlinear skb: the iwlwifi arm's 7260 (no MQ RX) allocates
	 * a 128-byte skb and puts everything past its tailroom (~190 B) into
	 * page frags (mvm/rx.c:324, :80, :89).  A copied-from pointer then
	 * yields the linear head and heap bytes where the payload should be.
	 * rx_frag is the count of frames in that shape; rx_lin_fail the
	 * linearisation attempts that could not allocate.  Diagnostic builds
	 * only - the delivered build must not depend on this block.
	 */
	unsigned int		rx_seen;	/* frames offered to the rx entry */
	unsigned int		rx_linear;	/* skb_headlen == skb->len */
	unsigned int		rx_frag;	/* nonlinear: head + page frags */
	unsigned int		rx_lin_fail;	/* skb_linearize() failed */
	unsigned int		rx_amsdu;	/* QoS data, A-MSDU present bit */
	unsigned int		rx_len[4];	/* <=192/193-600/601-1200/>1200 */
	unsigned int		rx_type[4];	/* mgmt / ctrl / data / other */
	unsigned int		rx_ba_ret[3];	/* wlan_ba_rx(): 0/1/2 */
	int			rx_ba_start_rc;	/* ampdu_action(RX_START) rc */

	/*
	 * The transmit-rate census (W7-12): what the firmware says it actually
	 * used.  mac80211's band-2GHz legacy index 0 is CCK 1 Mbit/s; an HT
	 * frame carries IEEE80211_TX_RC_MCS and an MCS index in 0..15 instead.
	 * Separated by frame class because the 1.0 Mbit/s question is about
	 * data frames only.
	 */
	unsigned int		tx_status_seen;
	unsigned int		tx_status_ack;
	unsigned int		tx_status_noack;
	unsigned int		tx_status_filtered;
	unsigned int		tx_rate_legacy[32];	/* data, legacy idx */
	unsigned int		tx_rate_mcs[16];	/* data, HT MCS idx */
	unsigned int		tx_rate_mgmt[32];
	unsigned int		rc_tx_status;	/* rate_control_ops.tx_status */

	/*
	 * The rate-feedback plumbing (W7-12, rc_feedback).  mac80211 keeps the
	 * driver's registered rate-control table (rate_control_alloc in
	 * ieee80211_register_hw), allocates its per-station state when the
	 * station is created, and calls its tx_status from the transmit-status
	 * path; this substitute kept only the registration.  Held here so the
	 * callback runs against the same objects mac80211 would pass.
	 */
	const struct rate_control_ops *rc_ops;
	void			*rc_priv;
	void			*rc_sta_priv;
};

/* the driver-half seam vtable (tests/board/linux-ko/ports/adapter/wlan_seam_adapter_linux.c) */
extern const struct wlan_seam_drv_ops wlan_seam_ops;

/* the substitute's registration path: build the bridge around this hw */
int wlan_adapter_attach(struct ieee80211_hw *hw);

/* the one hw this module hosts (the bridge is a singleton too) */
struct wlan_linux_hw *wlan_hw_of(struct ieee80211_hw *hw);
struct wlan_linux_hw *wlan_hw_from_vif(struct ieee80211_vif *vif);
struct wlan_linux_hw *wlan_hw_single(void);
void wlan_adapter_detach(struct ieee80211_hw *hw);

/* TXQ engine (tests/board/linux-ko/ports/mac80211/mac80211_txq.c) */
int wlan_txq_enqueue(struct ieee80211_txq *txq, struct sk_buff *skb);
unsigned long wlan_txq_len(struct ieee80211_txq *txq);

/* the driver's queue state (its stop/wake pair) and the port's arm of the
 * wake work: the allocator arms the work, the adapter registers the delivery */
void wlan_tx_wake_init(struct wlan_linux_hw *w);
void wlan_tx_wake_set_cb(struct ieee80211_hw *hw, void (*cb)(void *arg),
    void *arg);
int wlan_tx_queue_stopped(struct ieee80211_hw *hw);

/* the seam's transmit admission (W6-05): frames handed minus statuses seen.
 * An aggregate completion arrives as one extra status for the frames it
 * carried, so the difference can go to zero early; it is a bound, not a
 * census.  The pump restarts the bridge's queue from the completion work -
 * with the admission gate in place a completion is the only event that moves
 * the line, and rtw88's USB path will not raise a queue wake. */
unsigned int wlan_adapter_tx_inflight(struct ieee80211_hw *hw);
void wlan_adapter_tx_pump(struct ieee80211_hw *hw);
void wlan_adapter_tx_kick(struct ieee80211_hw *hw);

/* a received frame, from ieee80211_rx_irqsafe() to the net80211 bridge */
void wlan_adapter_rx(struct ieee80211_hw *hw, struct sk_buff *skb);
/* hand one 802.11 frame (no FCS) to the net80211 bridge; the caller keeps
 * ownership of skb and frees it afterwards */
void wlan_adapter_deliver(struct ieee80211_hw *hw, struct sk_buff *skb);

/* a transmitted frame completed (ieee80211_tx_status_irqsafe) */
void wlan_adapter_tx_complete(struct ieee80211_hw *hw, struct sk_buff *skb);

/* the rc_feedback module parameter's live value (W7-12): the transmit-status
 * path asks it before it feeds the driver's rate control */
bool wlan_adapter_rc_feedback(void);

/* the rate-control plumbing the rc_feedback arm wires (W7-12): attach the
 * table the driver registered, allocate its per-station state at station
 * creation, and the getter the iwlwifi closure exposes */
const struct rate_control_ops *wlan_rc_ops_get(void);
void wlan_adapter_rc_attach(struct ieee80211_hw *hw);
void wlan_adapter_rc_sta_add(struct ieee80211_hw *hw, struct ieee80211_sta *sta);

/* the firmware sweep ended (the substitute's deferred scan work) */
void wlan_adapter_scan_complete(struct ieee80211_hw *hw);

/* the driver registration glue (tests/board/linux-ko/ports/driver_glue.h) */
int wlan_driver_register(void);
void wlan_driver_unregister(void);

/* the sysfs control plane (tests/board/linux-ko/ports/control/wlan_control_linux.c) */
void wlan_control_exit(void);

/* the kernelmod line data plane (tests/board/linux-ko/ports/netdev/wlan_netdev_linux.c) */
int wlan_netdev_create(struct ieee80211_hw *hw);
struct net_device *wlan_netdev_get(void);
void wlan_netdev_destroy(void);

#endif /* _WLAN_LINUX_WLAN_H_ */
