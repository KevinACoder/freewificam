/*
 * @file
 * @brief K4: the Linux netdev that carries IP over the kernelmod line.
 *
 * Until now the kernelmod line had no network device: the bridge attached, the stack
 * reached RUN, but nothing could hand it an IP packet. This device is that
 * hand-off. It is presentation only - the 802.11 work stays in the bridge and
 * the net80211 stack:
 *
 *   TX  ndo_start_xmit only queues the ethernet frame (it runs in BH context);
 *       an ordered workqueue calls wlan_bridge_tx_raw() from process context,
 *       which is where the bridge takes its serializer and the driver submits
 *       the URB.
 *   RX  the bridge's data seam (wlan_bridge_set_data_rx) delivers a complete
 *       ethernet frame from the stack's receive path; the netdev wraps it in an
 *       skb and hands it to the kernel.
 *
 * The link is brought up by the driver's start (already run by the bridge
 * attach); ndo_open only marks carrier and the queue. ndo_stop deliberately
 * does not call wlan_bridge_down(): the module detach is the single owner of
 * the driver stop, so a user "ip link set down" does not stop the radio under
 * a still-attached bridge.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/if_ether.h>
#include <linux/errno.h>
#include <linux/string.h>

#include <stdint.h>
#include <stddef.h>

#include <wlan_bridge.h>

#include <port/port.h>

#include "wlan_linux.h"
#include "wlan_netdev.h"
#include "wlan_ko_prof.h"

/*
 * The high-water mark: at the cap the device stops its queue and tells the
 * stack to come back (NETDEV_TX_BUSY) instead of dropping frames on the floor.
 * The low-water wake is what reopens it once the work has caught up.
 */
#define WLAN_NETDEV_TX_LOW	(WLAN_NETDEV_TX_MAX - 64)

/*
 * Frames handed to the bridge in one go.  Each frame used to go through
 * wlan_bridge_tx_raw() on its own, and the drain that follows wakes the
 * driver's txq work immediately; the work then found one frame, and rtw88's
 * USB layer built a 20 KB aggregate around a 64-byte frame and paid a URB
 * round trip for it (measured: 3.6k frames/s one-way vs 21.5k on the native
 * kernel, 11 KB of memory per in-flight frame).  Queueing the batch first and
 * opening the gate once lets __rtw_tx_work() take them all, so the aggregate
 * is filled.  The cap is the bridge's own 64-entry send queue.
 */
#ifndef CONFIG_WLAN_KO_TX_BATCH
#define CONFIG_WLAN_KO_TX_BATCH 32
#endif
/*
 * How long a frame may wait for its batch.  Frames arrive from the stack one at
 * a time, so a queue that is drained the moment it is non-empty never holds
 * more than the one or two frames that happen to overlap: the driver then takes
 * one or two per wake and the USB aggregate is built around a 64-byte frame.
 * Waiting a fixed slice lets the queue reach CONFIG_WLAN_KO_TX_BATCH, at the
 * cost of that much latency per batch (throughput cells do not care; the link
 * does when the queue is nearly empty).
 */
#ifndef CONFIG_WLAN_KO_TX_BATCH_US
#define CONFIG_WLAN_KO_TX_BATCH_US 1500
#endif

static void wlan_netdev_tx_work(struct work_struct *work)
{
	struct wlan_netdev *wn = container_of(work, struct wlan_netdev,
	    tx_work.work);
	struct net_device *dev = wn->dev;
	struct wlan_linux_hw *w = wlan_hw_single();
	struct sk_buff *skb;
	int rc, sent = 0;

	/*
	 * The bridge takes its serializer around every driver entry; this work
	 * is one of them (the RTOS netif's link_output takes it the same way).
	 * It also keeps the drain from racing the driver's queue wake, which
	 * arrives as a second drain path on its own work.
	 */
	wlan_port_serializer_lock();
	while (sent < CONFIG_WLAN_KO_TX_BATCH &&
	    (skb = skb_dequeue(&wn->txq)) != NULL) {
		u64 prof_t0 = wlan_ko_prof_begin();

#ifdef CONFIG_WLAN_KO_PROF
		if (ktime_to_ns(skb->tstamp) != 0) {
			wlan_ko_prof_value(WLAN_KO_PROF_TX_DEFER,
			    ktime_get_ns() - ktime_to_ns(skb->tstamp));
			skb->tstamp = 0;
		}
#endif
		rc = wlan_bridge_tx_enqueue(skb->data, skb->len);
		if (rc < 0) {
			/*
			 * Not a drop: the bridge's send queue is full, so the
			 * frame goes back to the head and the device queue stays
			 * stopped until the wake.  The stack already has the
			 * NETDEV_TX_BUSY contract; this is the same answer one
			 * layer up.
			 */
			skb_queue_head(&wn->txq, skb);
			wlan_ko_prof_event(WLAN_KO_PROF_EVT_TX_BRIDGE_FULL);
			wlan_ko_prof_end(WLAN_KO_PROF_TX_WORK, prof_t0);
			break;
		}
		wlan_ko_prof_depth(WLAN_KO_PROF_Q_NETDEV_TX, -1);
		wlan_ko_prof_event(WLAN_KO_PROF_EVT_TX_WORK);
		wlan_ko_prof_end(WLAN_KO_PROF_TX_WORK, prof_t0);
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += skb->len;
		dev_kfree_skb(skb);
		sent++;
	}
	/*
	 * Open the gate once for the batch: seam_tx() defers its wake_tx_queue
	 * while tx_batch is set, so the driver's work runs after the whole
	 * batch is in its queue instead of once per frame.
	 */
	if (sent > 0) {
		if (w != NULL) {
			unsigned long flags;

			spin_lock_irqsave(&w->tx_batch_lock, flags);
			w->tx_batch++;
			spin_unlock_irqrestore(&w->tx_batch_lock, flags);
			wlan_bridge_tx_flush();
			spin_lock_irqsave(&w->tx_batch_lock, flags);
			w->tx_batch--;
			spin_unlock_irqrestore(&w->tx_batch_lock, flags);
			wlan_adapter_tx_kick(&w->hw);
		} else {
			wlan_bridge_tx_flush();
		}
	}
	wlan_port_serializer_unlock();
	/*
	 * The batch cap can leave frames behind, and so can a full bridge
	 * queue: re-arm the work so the rest keeps moving, and open the device
	 * queue again.  It has to be opened even when frames remain - the
	 * seam's queue_ready() reads this queue's stopped flag, so a queue left
	 * stopped also closes the gate into the driver (measured 2026-11-05:
	 * 15 frames handed, then nothing moved at all).
	 */
	if (!skb_queue_empty(&wn->txq)) {
		mod_delayed_work(system_wq, &wn->tx_work, 0);
	}
	netif_wake_queue(dev);
}

static netdev_tx_t wlan_netdev_start_xmit(struct sk_buff *skb,
    struct net_device *dev)
{
	struct wlan_netdev *wn = netdev_priv(dev);
	u64 prof_t0 = wlan_ko_prof_begin();
	unsigned long flags;

	if (skb->len > 1600) {
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		wlan_ko_prof_event(WLAN_KO_PROF_EVT_TX_LEN_DROP);
		wlan_ko_prof_end(WLAN_KO_PROF_TX_XMIT, prof_t0);
		return NETDEV_TX_OK;
	}
	spin_lock_irqsave(&wn->tx_lock, flags);
	if (skb_queue_len(&wn->txq) >= WLAN_NETDEV_TX_MAX) {
		spin_unlock_irqrestore(&wn->tx_lock, flags);
		/*
		 * Backpressure, not a drop: stop the queue and let the kernel
		 * requeue the frame (dev_queue_xmit handles NETDEV_TX_BUSY by
		 * stopping the queue it found busy and retrying later).
		 */
		netif_stop_queue(dev);
		wlan_ko_prof_event(WLAN_KO_PROF_EVT_TX_BUSY);
		wlan_ko_prof_end(WLAN_KO_PROF_TX_XMIT, prof_t0);
		return NETDEV_TX_BUSY;
	}
	skb_queue_tail(&wn->txq, skb);
	spin_unlock_irqrestore(&wn->tx_lock, flags);
#ifdef CONFIG_WLAN_KO_PROF
	if (prof_t0)
		skb->tstamp = ns_to_ktime(prof_t0);
#endif
	wlan_ko_prof_depth(WLAN_KO_PROF_Q_NETDEV_TX, 1);
	wlan_ko_prof_event(WLAN_KO_PROF_EVT_TX_ENQ);
	wlan_ko_prof_end(WLAN_KO_PROF_TX_XMIT, prof_t0);
	/*
	 * Fill a batch before opening the gate: one immediate run when the queue
	 * already holds a batch, otherwise one delayed run that the following
	 * frames join.  The delay is not re-armed per frame (that would push the
	 * deadline out forever under a steady stream), so the first frame of a
	 * batch sets the deadline and the rest ride along.
	 */
	if (skb_queue_len(&wn->txq) >= CONFIG_WLAN_KO_TX_BATCH) {
		mod_delayed_work(system_wq, &wn->tx_work, 0);
	} else if (!delayed_work_pending(&wn->tx_work)) {
		schedule_delayed_work(&wn->tx_work,
		    usecs_to_jiffies(CONFIG_WLAN_KO_TX_BATCH_US));
	}
	return NETDEV_TX_OK;
}

static void wlan_netdev_rx(const uint8_t *frame, size_t len, void *arg)
{
	struct net_device *dev = arg;
	struct sk_buff *skb;
	u64 prof_t0 = wlan_ko_prof_begin();

	skb = dev_alloc_skb(len + 2);
	if (skb == NULL) {
		dev->stats.rx_dropped++;
		wlan_ko_prof_event(WLAN_KO_PROF_EVT_RX_DROP);
		wlan_ko_prof_end(WLAN_KO_PROF_RX_NETDEV, prof_t0);
		return;
	}
	skb_reserve(skb, 2);
	memcpy(skb_put(skb, len), frame, len);
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;
	netif_rx(skb);
	wlan_ko_prof_end(WLAN_KO_PROF_RX_NETDEV, prof_t0);
}

static int wlan_netdev_open(struct net_device *dev)
{
	int rc;

	rc = wlan_bridge_up();
	if (rc != 0) {
		netdev_err(dev, "bridge up refused rc=%d\n", rc);
		return rc;
	}
	netif_carrier_on(dev);
	netif_start_queue(dev);
	return 0;
}

static int wlan_netdev_stop(struct net_device *dev)
{
	/* the module detach owns the driver stop; see the file comment */
	netif_stop_queue(dev);
	netif_carrier_off(dev);
	return 0;
}

static const struct net_device_ops wlan_netdev_ops = {
	.ndo_open = wlan_netdev_open,
	.ndo_stop = wlan_netdev_stop,
	.ndo_start_xmit = wlan_netdev_start_xmit,
};

static struct net_device *wlan_netdev;

int wlan_netdev_create(struct ieee80211_hw *hw)
{
	struct net_device *dev;
	struct wlan_netdev *wn;
	uint8_t mac[6];
	int rc;

	dev = alloc_etherdev(sizeof(*wn));
	if (dev == NULL) {
		return -ENOMEM;
	}
	wn = netdev_priv(dev);
	wn->dev = dev;
	skb_queue_head_init(&wn->txq);
	spin_lock_init(&wn->tx_lock);
	INIT_DELAYED_WORK(&wn->tx_work, wlan_netdev_tx_work);
	dev->netdev_ops = &wlan_netdev_ops;
	strscpy(dev->name, "wlan%d", sizeof(dev->name));
	SET_NETDEV_DEV(dev, hw->wiphy->dev.parent);
	/* cfg80211's WEXT face hangs off the wiphy (see wlan_wext.c) */
	wn->wdev.wiphy = hw->wiphy;
	wn->wdev.iftype = NL80211_IFTYPE_STATION;
	wn->wdev.netdev = dev;
	dev->ieee80211_ptr = &wn->wdev;

	if (wlan_bridge_hwaddr(mac) == 0) {
		eth_hw_addr_set(dev, mac);
	}
	rc = register_netdev(dev);
	if (rc != 0) {
		netdev_err(dev, "register refused rc=%d\n", rc);
		free_netdev(dev);
		return rc;
	}
	wlan_bridge_set_data_rx(wlan_netdev_rx, dev);
	(void)wlan_wext_attach(dev);
	wlan_netdev = dev;
	netdev_info(dev, "kernelmod line data plane up, addr %pM\n", dev->dev_addr);
	return 0;
}

struct net_device *wlan_netdev_get(void)
{
	return wlan_netdev;
}

void wlan_netdev_tx_kick(void)
{
	struct net_device *dev = wlan_netdev;
	struct wlan_netdev *wn;

	if (dev == NULL)
		return;
	wn = netdev_priv(dev);
	mod_delayed_work(system_wq, &wn->tx_work, 0);
}

void wlan_netdev_destroy(void)
{
	struct net_device *dev = wlan_netdev;
	struct wlan_netdev *wn;
	struct sk_buff *skb;

	if (dev == NULL) {
		return;
	}
	wlan_netdev = NULL;
	wlan_wext_detach(dev);
	wlan_bridge_set_data_rx(NULL, NULL);
	wn = netdev_priv(dev);
	unregister_netdev(dev);
	cancel_delayed_work_sync(&wn->tx_work);
	while ((skb = skb_dequeue(&wn->txq)) != NULL) {
		dev_kfree_skb(skb);
	}
	free_netdev(dev);
}
