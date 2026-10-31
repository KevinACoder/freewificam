/* SPDX-License-Identifier: GPL-2.0 */
/*
 * @file wlan_netdev.h
 * @brief The netdev's private state, shared by the data-plane file and the
 *        WEXT face (the WEXT handlers reach the WEXT state through
 *        netdev_priv()).
 */

#ifndef WLAN_NETDEV_H
#define WLAN_NETDEV_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "wlan_wext.h"

#define WLAN_NETDEV_TX_MAX	256

struct wlan_netdev {
	struct net_device	*dev;
	struct sk_buff_head	txq;
	spinlock_t		tx_lock;
	struct work_struct	tx_work;
	struct wireless_dev	wdev;	/* the cfg80211 WEXT path (wiphy->wext) */
	struct wlan_wext	wext;
};

/*
 * Called from the port's deferred queue wake (process context): the driver
 * refilled its tx-urb pool, so reopen the device queue and pull the next
 * frames through the bridge.  No-op before the device exists.
 */
void wlan_netdev_tx_kick(void);

#endif /* WLAN_NETDEV_H */
