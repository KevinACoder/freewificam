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
};

/* the driver-half seam vtable (tests/board/linux-ko/ports/adapter/wlan_seam_adapter_linux.c) */
extern const struct wlan_seam_drv_ops wlan_seam_ops;

/* the substitute's registration path: build the bridge around this hw */
int wlan_adapter_attach(struct ieee80211_hw *hw);

/* the one hw this module hosts (the bridge is a singleton too) */
struct wlan_linux_hw *wlan_hw_of(struct ieee80211_hw *hw);
struct wlan_linux_hw *wlan_hw_from_vif(struct ieee80211_vif *vif);
void wlan_adapter_detach(struct ieee80211_hw *hw);

/* TXQ engine (tests/board/linux-ko/ports/mac80211/mac80211_txq.c) */
int wlan_txq_enqueue(struct ieee80211_txq *txq, struct sk_buff *skb);
unsigned long wlan_txq_len(struct ieee80211_txq *txq);

/* a received frame, from ieee80211_rx_irqsafe() to the net80211 bridge */
void wlan_adapter_rx(struct ieee80211_hw *hw, struct sk_buff *skb);

/* a transmitted frame completed (ieee80211_tx_status_irqsafe) */
void wlan_adapter_tx_complete(struct ieee80211_hw *hw, struct sk_buff *skb);

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
