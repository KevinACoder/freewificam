/*
 * @file
 * @brief Per-frame instrumentation for the kernelmod line (PROF builds only).
 *
 * The ko module carries a whole substitute stack (netdev -> bridge -> net80211
 * -> seam -> mac80211 substitute -> driver).  Throughput alone says the line is
 * slow; this profiler says where each frame spends its time.  It is opt-in
 * (PROF=1 at build time, prof_enable=1 at runtime) so a delivered module is
 * untouched, and its numbers are labelled "instrumented" - never a rate
 * baseline.
 *
 * The stages are the ones a frame actually crosses:
 *
 *   TX  xmit      ndo_start_xmit (BH): skb queued for the netdev work
 *       defer     enqueue -> wlan_netdev_tx_work start (system_wq latency)
 *       work      wlan_netdev_tx_work: one skb handed to the bridge
 *       seam      seam_tx: skb copy + the driver's .tx (usb_submit_urb)
 *       status    ieee80211_tx_status_irqsafe (completion deferred)
 *       status_w  wlan_tx_work: one completion handled
 *   RX  irq       ieee80211_rx_irqsafe: trim + queue for the receive work
 *       defer     enqueue -> wlan_rx_work start (the workqueue latency)
 *       work      wlan_rx_work: one skb through bridge/net80211 input
 *       netdev    wlan_netdev_rx: skb alloc + copy + netif_rx
 *
 * Queue depths and frame/event counts are reported next to the stage times:
 * a large stage with a shallow queue means the stage itself is slow; a shallow
 * stage with a deep queue means the consumer is behind.
 */
#ifndef WLAN_KO_PROF_H
#define WLAN_KO_PROF_H

#include <linux/types.h>

enum wlan_ko_prof_stage {
	WLAN_KO_PROF_TX_XMIT = 0,
	WLAN_KO_PROF_TX_WORK,
	WLAN_KO_PROF_TX_DEFER,
	WLAN_KO_PROF_TX_SEAM,
	WLAN_KO_PROF_TX_STATUS,
	WLAN_KO_PROF_TX_STATUS_WORK,
	WLAN_KO_PROF_RX_IRQ,
	WLAN_KO_PROF_RX_WORK,
	WLAN_KO_PROF_RX_NETDEV,
	WLAN_KO_PROF_RX_DEFER,
	WLAN_KO_PROF_STAGE_NR,
};

enum wlan_ko_prof_q {
	WLAN_KO_PROF_Q_NETDEV_TX = 0,
	WLAN_KO_PROF_Q_RX,
	WLAN_KO_PROF_Q_TX_STATUS,
	WLAN_KO_PROF_Q_NR,
};

enum wlan_ko_prof_evt {
	WLAN_KO_PROF_EVT_TX_ENQ = 0,
	WLAN_KO_PROF_EVT_TX_WORK,
	WLAN_KO_PROF_EVT_TX_SEAM,
	WLAN_KO_PROF_EVT_TX_STATUS,
	WLAN_KO_PROF_EVT_TX_BUSY,
	WLAN_KO_PROF_EVT_TX_LEN_DROP,
	WLAN_KO_PROF_EVT_TX_BRIDGE_FULL,
	WLAN_KO_PROF_EVT_RX_FRAME,
	WLAN_KO_PROF_EVT_RX_WORK,
	WLAN_KO_PROF_EVT_RX_DROP,
	WLAN_KO_PROF_EVT_NR,
};

#ifdef CONFIG_WLAN_KO_PROF

u64 wlan_ko_prof_begin(void);
void wlan_ko_prof_end(int stage, u64 t0);
void wlan_ko_prof_value(int stage, u64 ns);
void wlan_ko_prof_depth(int q, int delta);
void wlan_ko_prof_event(int evt);
bool wlan_ko_prof_enabled(void);

#else /* !CONFIG_WLAN_KO_PROF */

static inline u64 wlan_ko_prof_begin(void)
{
	return 0;
}

static inline void wlan_ko_prof_end(int stage, u64 t0)
{
	(void)stage;
	(void)t0;
}

static inline void wlan_ko_prof_value(int stage, u64 ns)
{
	(void)stage;
	(void)ns;
}

static inline void wlan_ko_prof_depth(int q, int delta)
{
	(void)q;
	(void)delta;
}

static inline void wlan_ko_prof_event(int evt)
{
	(void)evt;
}

static inline bool wlan_ko_prof_enabled(void)
{
	return false;
}

#endif /* CONFIG_WLAN_KO_PROF */

#endif /* WLAN_KO_PROF_H */
