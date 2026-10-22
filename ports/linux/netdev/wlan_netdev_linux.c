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
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/if_ether.h>
#include <linux/errno.h>
#include <linux/string.h>

#include <stdint.h>
#include <stddef.h>

#include <wlan_bridge.h>

#include "wlan_linux.h"
#include "wlan_netdev.h"

static void wlan_netdev_tx_work(struct work_struct *work)
{
	struct wlan_netdev *wn = container_of(work, struct wlan_netdev, tx_work);
	struct net_device *dev = wn->dev;
	struct sk_buff *skb;
	int rc;

	while ((skb = skb_dequeue(&wn->txq)) != NULL) {
		rc = wlan_bridge_tx_raw(skb->data, skb->len);
		if (rc < 0) {
			dev->stats.tx_errors++;
		} else {
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += skb->len;
		}
		dev_kfree_skb(skb);
	}
	netif_wake_queue(dev);
}

static netdev_tx_t wlan_netdev_start_xmit(struct sk_buff *skb,
    struct net_device *dev)
{
	struct wlan_netdev *wn = netdev_priv(dev);
	unsigned long flags;

	if (skb->len > 1600) {
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	spin_lock_irqsave(&wn->tx_lock, flags);
	if (skb_queue_len(&wn->txq) >= WLAN_NETDEV_TX_MAX) {
		spin_unlock_irqrestore(&wn->tx_lock, flags);
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	skb_queue_tail(&wn->txq, skb);
	spin_unlock_irqrestore(&wn->tx_lock, flags);
	schedule_work(&wn->tx_work);
	return NETDEV_TX_OK;
}

static void wlan_netdev_rx(const uint8_t *frame, size_t len, void *arg)
{
	struct net_device *dev = arg;
	struct sk_buff *skb;

	skb = dev_alloc_skb(len + 2);
	if (skb == NULL) {
		dev->stats.rx_dropped++;
		return;
	}
	skb_reserve(skb, 2);
	memcpy(skb_put(skb, len), frame, len);
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;
	netif_rx(skb);
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
	INIT_WORK(&wn->tx_work, wlan_netdev_tx_work);
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
	cancel_work_sync(&wn->tx_work);
	while ((skb = skb_dequeue(&wn->txq)) != NULL) {
		dev_kfree_skb(skb);
	}
	free_netdev(dev);
}
