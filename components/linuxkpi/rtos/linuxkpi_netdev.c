/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_netdev.c
 * @brief   The net-device bodies: the device registry, the allocator that owns
 *          the object (name, address storage, private area, transmit queues), the
 *          link/queue state helpers, the receive entry point, the ethernet receive
 *          classifier a vendored driver's data path calls, the NAPI lifecycle a
 *          PCIe receive path drives, and the dummy-device allocator that carries a
 *          NAPI context.
 *
 * @design registry + adapter, the same shape the cfg80211 substitute uses
 *         (components/linuxkpi/linuxkpi_cfg80211.c:1-46). The net-device face
 *         (components/linuxkpi/include/linux/netdevice.h) declares the object and
 *         its operations; this file is the body behind the declarations the
 *         compiled set actually names. The port has no queueing discipline, no
 *         NAPI poller and no rtnetlink core, so what is implemented is exactly
 *         the closure the driver's probe/transmit/receive paths reach, and each
 *         body states the driver call site it serves. The device lookup a driver
 *         makes by name (dev_get_by_name, custom_gpio_linux.c:312) and the
 *         registry the substitute's own scan path uses (lkpi_netdev_by_wiphy) are
 *         the same table.
 *
 * Object ownership: alloc_etherdev_mqs allocates one zeroed block for the device
 * record plus the driver's private area (the record's `priv[]` tail, which
 * netdev_priv() returns), plus the transmit-queue array and the address storage
 * the record points at; free_netdev releases all three. The driver allocates its
 * own adapter separately and hangs it off the private indicator record, exactly
 * as on Linux (osdep_service.c:2441).
 *
 * Reference for the shape: components/linuxkpi/linuxkpi_cfg80211.c:1-46.
 * @date    12.10.2026
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>

/* ------------------------------------------------------------------ */
/* the device registry                                                 */
/* ------------------------------------------------------------------ */

#ifndef CONFIG_LKPI_NETDEV_MAX
#define CONFIG_LKPI_NETDEV_MAX	4
#endif

/*
 * The registered devices, in registration order. The port has one interface per
 * driver in practice; the bound keeps a leak or a double registration visible
 * rather than unbounded. No reference counting is modelled: a device is owned by
 * its driver, which frees it with free_netdev, so dev_get_by_name/dev_put are the
 * lookup and an inert drop.
 */
static struct net_device *lkpi_netdevs[CONFIG_LKPI_NETDEV_MAX];

static bool
lkpi_netdev_track(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI_NETDEV_MAX; i++)
		if (lkpi_netdevs[i] == dev)
			return false;	/* already registered */
	for (i = 0; i < CONFIG_LKPI_NETDEV_MAX; i++)
		if (lkpi_netdevs[i] == NULL) {
			lkpi_netdevs[i] = dev;
			return true;
		}
	lkpi_printf(ULOG_ERROR_LEVEL, "lkpi netdev: registry full (%u)\n",
	    (unsigned)CONFIG_LKPI_NETDEV_MAX);
	return false;
}

static void
lkpi_netdev_untrack(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI_NETDEV_MAX; i++)
		if (lkpi_netdevs[i] == dev)
			lkpi_netdevs[i] = NULL;
}

static bool
lkpi_netdev_name_in_use(const char *name)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI_NETDEV_MAX; i++)
		if (lkpi_netdevs[i] != NULL &&
		    strcmp(lkpi_netdevs[i]->name, name) == 0)
			return true;
	return false;
}

/*
 * The device the driver registered for a wiphy, found through the wireless-dev
 * record it hung on the net device (rtw_wdev_alloc sets
 * pnetdev->ieee80211_ptr = wdev, ioctl_cfg80211.c:9987). The cfg80211
 * substitute's scan path calls this (linuxkpi_cfg80211.c).
 */
struct net_device *
lkpi_netdev_by_wiphy(struct wiphy *wiphy)
{
	unsigned int i;

	if (wiphy == NULL)
		return NULL;
	for (i = 0; i < CONFIG_LKPI_NETDEV_MAX; i++) {
		struct net_device *dev = lkpi_netdevs[i];

		if (dev != NULL && dev->ieee80211_ptr != NULL &&
		    dev->ieee80211_ptr->wiphy == wiphy)
			return dev;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* allocation (driver: osdep_service.c:2441 rtw_alloc_etherdev)         */
/* ------------------------------------------------------------------ */

struct net_device *
alloc_etherdev_mqs(int sizeof_priv, unsigned int txqs, unsigned int rxqs)
{
	struct net_device *dev;
	unsigned int i;

	(void)rxqs;	/* this port has no per-queue receive ring */
	if (sizeof_priv < 0)
		return NULL;

	/*
	 * One block for the record plus the driver's private area. The private
	 * area is the record's `priv[]` tail, so it starts exactly at
	 * netdev_priv(dev) and is sizeof_priv bytes long.
	 */
	dev = kzalloc(sizeof(*dev) + (size_t)sizeof_priv, GFP_KERNEL);
	if (dev == NULL)
		return NULL;

	/*
	 * ether_setup's substance for this port (the record carries no mtu or
	 * hard_header_len member): an Ethernet device with its own six-byte
	 * address storage, which dev_addr_mod()/eth_hw_addr_set() write through.
	 */
	dev->type = ARPHRD_ETHER;
	dev->addr_len = ETH_ALEN;
	dev->dev_addr = kzalloc(ETH_ALEN, GFP_KERNEL);
	if (dev->dev_addr == NULL) {
		kfree(dev);
		return NULL;
	}

	dev->num_tx_queues = txqs;
	dev->real_num_tx_queues = txqs;
	if (txqs > 0) {
		dev->_tx = kcalloc(txqs, sizeof(struct netdev_queue), GFP_KERNEL);
		if (dev->_tx == NULL) {
			kfree((void *)dev->dev_addr);
			kfree(dev);
			return NULL;
		}
		for (i = 0; i < txqs; i++)
			dev->_tx[i].dev = dev;
	}
	return dev;
}

void
free_netdev(struct net_device *dev)
{

	if (dev == NULL)
		return;
	lkpi_netdev_untrack(dev);
	if (dev->dev_addr != NULL)
		kfree((void *)dev->dev_addr);
	kfree(dev->_tx);
	kfree(dev);
}

/*
 * [LNX] include/linux/netdevice.h:5034: `struct net_device *alloc_netdev_dummy(int
 * sizeof_priv)`. The interface allocates a net device with no name, no address and
 * no transmit queues (dummy_setup), used as the carrier for a NAPI context that is
 * not itself an interface. That is exactly the PCIe port's use
 * (pcie/gen1_2/trans.c:3879): the returned device is never registered, and its
 * private area holds a back-pointer (rx.c:1002 reads it back through netdev_priv).
 * One block for the record plus the private tail, zeroed - the same object shape
 * alloc_etherdev_mqs uses, without the ethernet setup this device does not want.
 */
struct net_device *
alloc_netdev_dummy(int sizeof_priv)
{
	struct net_device *dev;

	if (sizeof_priv < 0)
		return NULL;
	dev = kzalloc(sizeof(*dev) + (size_t)sizeof_priv, GFP_KERNEL);
	if (dev == NULL)
		return NULL;
	/* the private area is the record's `priv[]` tail (netdev_priv) */
	return dev;
}

/* ------------------------------------------------------------------ */
/* registration and lookup (driver: os_intfs.c:1845 register_netdev,    */
/* :1847 register_netdevice, custom_gpio_linux.c:312 dev_get_by_name)   */
/* ------------------------------------------------------------------ */

int
register_netdev(struct net_device *dev)
{

	if (dev == NULL)
		return -EINVAL;
	if (!lkpi_netdev_track(dev))
		return 0;		/* already findable by name */
	/* the device is present; the link's carrier is the driver's to raise */
	set_bit(__LINK_STATE_PRESENT, &dev->state);
	lkpi_printf(ULOG_INFO_LEVEL, "lkpi netdev: registered %s\n", dev->name);
	return 0;
}

void
unregister_netdev(struct net_device *dev)
{

	if (dev != NULL)
		lkpi_netdev_untrack(dev);
}

/*
 * register_netdevice()/unregister_netdevice() are the locked forms of the pair
 * above on Linux. This port has no rtnetlink lock (see rtnl_is_locked below), so
 * the two are the same registration; a driver picks one or the other from
 * rtw_rtnl_lock_needed() and both must leave the device findable.
 */
int
register_netdevice(struct net_device *dev)
{

	return register_netdev(dev);
}

void
unregister_netdevice(struct net_device *dev)
{

	unregister_netdev(dev);
}

struct net_device *
dev_get_by_name(struct net *net, const char *name)
{
	unsigned int i;

	(void)net;	/* this port has one namespace, init_net */
	if (name == NULL)
		return NULL;
	for (i = 0; i < CONFIG_LKPI_NETDEV_MAX; i++)
		if (lkpi_netdevs[i] != NULL &&
		    strcmp(lkpi_netdevs[i]->name, name) == 0)
			return lkpi_netdevs[i];
	return NULL;
}

/*
 * dev_put() drops a lookup's reference. The registry models no references (a
 * device lives until its driver frees it), so the drop is inert; the driver
 * calls it unconditionally after dev_get_by_name.
 */
void
dev_put(struct net_device *dev)
{

	(void)dev;
}

/* ------------------------------------------------------------------ */
/* the notifier chains (driver: os_intfs.c:1591, :3264)                 */
/* ------------------------------------------------------------------ */

#ifndef CONFIG_LKPI_NOTIFIER_MAX
#define CONFIG_LKPI_NOTIFIER_MAX	4
#endif

static struct notifier_block *lkpi_netdev_notifiers[CONFIG_LKPI_NOTIFIER_MAX];
static struct notifier_block *lkpi_inetaddr_notifiers[CONFIG_LKPI_NOTIFIER_MAX];

static int
lkpi_notifier_add(struct notifier_block **chain, struct notifier_block *nb)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI_NOTIFIER_MAX; i++)
		if (chain[i] == nb)
			return 0;
	for (i = 0; i < CONFIG_LKPI_NOTIFIER_MAX; i++)
		if (chain[i] == NULL) {
			chain[i] = nb;
			return 0;
		}
	return -ENOSPC;
}

static void
lkpi_notifier_del(struct notifier_block **chain, struct notifier_block *nb)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI_NOTIFIER_MAX; i++)
		if (chain[i] == nb)
			chain[i] = NULL;
}

/*
 * The chains are recorded so registration succeeds and unregistration is exact.
 * No event is delivered: nothing on this port raises NETDEV_* (there is no
 * rtnetlink core) and the driver's own notifier acts only on NETDEV_CHANGENAME /
 * NETDEV_PRE_UP (os_intfs.c:1565-1578), neither of which the probe or scan path
 * produces. Delivering a synthetic event would be inventing state.
 */
int
register_netdevice_notifier(struct notifier_block *nb)
{

	return lkpi_notifier_add(lkpi_netdev_notifiers, nb);
}

int
unregister_netdevice_notifier(struct notifier_block *nb)
{

	lkpi_notifier_del(lkpi_netdev_notifiers, nb);
	return 0;
}

int
register_inetaddr_notifier(struct notifier_block *nb)
{

	return lkpi_notifier_add(lkpi_inetaddr_notifiers, nb);
}

int
unregister_inetaddr_notifier(struct notifier_block *nb)
{

	lkpi_notifier_del(lkpi_inetaddr_notifiers, nb);
	return 0;
}

/* ------------------------------------------------------------------ */
/* name and address (driver: os_intfs.c:1667 dev_alloc_name,            */
/* :1392/:1840 eth_hw_addr_set -> dev_addr_mod)                         */
/* ------------------------------------------------------------------ */

int
dev_alloc_name(struct net_device *dev, const char *name)
{
	char buf[IFNAMSIZ];
	unsigned int i;
	bool has_pct = false;

	if (dev == NULL || name == NULL)
		return -EINVAL;

	/* a literal name (no '%d') is taken as-is */
	for (i = 0; name[i] != '\0'; i++)
		if (name[i] == '%')
			has_pct = true;

	if (!has_pct) {
		(void)snprintf(dev->name, IFNAMSIZ, "%s", name);
		return 0;
	}

	/*
	 * The interface's own rule: substitute the first index that is not
	 * already taken by a registered device ("wlan%d" -> "wlan0", the name
	 * the board test expects).
	 */
	for (i = 0; i < 1000u; i++) {
		(void)snprintf(buf, sizeof(buf), name, (int)i);
		if (!lkpi_netdev_name_in_use(buf)) {
			(void)snprintf(dev->name, IFNAMSIZ, "%s", buf);
			return (int)i;
		}
	}
	return -ENOMEM;
}

void
dev_addr_mod(struct net_device *dev, unsigned int offset, const void *addr,
    size_t len)
{

	if (dev == NULL || dev->dev_addr == NULL)
		return;
	/* the address storage this file allocated; eth_hw_addr_set writes at 0 */
	memcpy((void *)(dev->dev_addr + offset), addr, len);
}

/* ------------------------------------------------------------------ */
/* link and queue state (driver: os_intfs.c:1670 netif_carrier_off)     */
/* ------------------------------------------------------------------ */

void
netif_carrier_on(struct net_device *dev)
{

	if (dev != NULL)
		clear_bit(__LINK_STATE_NOCARRIER, &dev->state);
}

void
netif_carrier_off(struct net_device *dev)
{

	if (dev != NULL)
		set_bit(__LINK_STATE_NOCARRIER, &dev->state);
}

void
netif_tx_stop_all_queues(struct net_device *dev)
{
	unsigned int i;

	if (dev == NULL)
		return;
	for (i = 0; i < dev->num_tx_queues; i++)
		netif_tx_stop_queue(netdev_get_tx_queue(dev, i));
}

/* ------------------------------------------------------------------ */
/* the receive seam (driver: recv_linux.c:515 eth_type_trans,           */
/* :545 netif_rx)                                                       */
/* ------------------------------------------------------------------ */

__be16
eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct ethhdr *eth;

	if (skb == NULL)
		return 0;
	skb->dev = dev;
	eth = (struct ethhdr *)skb->data;

	/*
	 * The receive classification the interface makes from the destination
	 * address (net/ethernet/eth.c:eth_type_trans): broadcast, other
	 * multicast, to us, or to someone else.
	 */
	if (is_multicast_ether_addr(eth->h_dest))
		skb->pkt_type = is_broadcast_ether_addr(eth->h_dest) ?
		    PACKET_BROADCAST : PACKET_MULTICAST;
	else if (dev != NULL && ether_addr_equal(eth->h_dest, dev->dev_addr))
		skb->pkt_type = PACKET_HOST;
	else
		skb->pkt_type = PACKET_OTHERHOST;

	skb_pull(skb, ETH_HLEN);

	/*
	 * A protocol field at or above 0x0600 is an EtherType; below it the field
	 * is a length and the frame is 802.2 LLC (ETH_P_802_2 = 0x0004).
	 */
	if (ntohs(eth->h_proto) >= 0x0600)
		skb->protocol = eth->h_proto;
	else
		skb->protocol = htons(0x0004);
	return skb->protocol;
}

/*
 * The presentation layer's receive entry. It is weak on purpose: an image that
 * carries a netif/lwIP feed defines it, an image that does not (this board test)
 * leaves it undefined, and netif_rx then drops the frame rather than leaking it.
 * The hook is the seam the port's data plane plugs into; no framework is added.
 */
extern void lkpi_netdev_rx(struct sk_buff *skb) __attribute__((weak));

int
netif_rx(struct sk_buff *skb)
{

	if (skb == NULL)
		return NET_RX_DROP;
	if (lkpi_netdev_rx != NULL) {
		lkpi_netdev_rx(skb);
		return NET_RX_SUCCESS;
	}
	/* no presentation layer registered: the frame is consumed here */
	kfree_skb(skb);
	return NET_RX_DROP;
}

/* ------------------------------------------------------------------ */
/* the NAPI context (driver: pcie/gen1_2/rx.c:1136 netif_napi_add,      */
/* :1138 napi_enable, :1236 napi_disable, :1237 netif_napi_del,         */
/* :1026/:1058 napi_complete_done)                                      */
/* ------------------------------------------------------------------ */

/*
 * The port's NAPI has no softirq poller: the scheduling pair that hands an
 * instance to the poller (napi_schedule_prep/__napi_schedule) already lives with
 * the mac80211 substitute (linuxkpi_80211_helpers.c:506-540) and runs the poll in
 * the caller's context. What is defined here is the lifecycle around it - add,
 * enable, disable, complete, delete - so the driver's own pairing (add then
 * enable; disable then del) is coherent. The state transitions are the
 * interface's (net/core/dev.c:7612 netif_napi_add_weight_locked, :7658
 * napi_disable_locked, :7719 napi_enable_locked): an instance starts SCHED|NPSVC
 * (added but not schedulable), napi_enable() clears them, and napi_disable()
 * sets them again. The interface's wait loops are absent because nothing here
 * polls concurrently - the poll, when it runs, runs inline in the caller.
 */
void
netif_napi_add(struct net_device *dev, struct napi_struct *napi,
    int (*poll)(struct napi_struct *, int))
{

	if (napi == NULL)
		return;
	napi->dev = dev;
	napi->poll = poll;
	napi->weight = NAPI_POLL_WEIGHT;
	INIT_LIST_HEAD(&napi->poll_list);
	set_bit(NAPI_STATE_SCHED, &napi->state);
	set_bit(NAPI_STATE_NPSVC, &napi->state);
}

void
netif_napi_del(struct napi_struct *napi)
{

	if (napi == NULL)
		return;
	napi->poll = NULL;
	napi->dev = NULL;
}

void
napi_enable(struct napi_struct *n)
{

	if (n == NULL)
		return;
	clear_bit(NAPI_STATE_SCHED, &n->state);
	clear_bit(NAPI_STATE_NPSVC, &n->state);
}

void
napi_disable(struct napi_struct *n)
{

	if (n == NULL)
		return;
	set_bit(NAPI_STATE_SCHED, &n->state);
	set_bit(NAPI_STATE_NPSVC, &n->state);
}

/*
 * [LNX] include/linux/netdevice.h:592: `bool napi_complete_done(struct
 * napi_struct *n, int work_done)` returns false when the device should avoid
 * rearming interrupts. This port has no interrupt to rearm (the poll runs inline
 * in __napi_schedule, which clears SCHED on return), so the answer is always
 * "complete, safe to rearm" - true - and the SCHED clear is idempotent with the
 * scheduler's.
 */
bool
napi_complete_done(struct napi_struct *n, int work_done)
{

	(void)work_done;
	if (n == NULL)
		return false;
	clear_bit(NAPI_STATE_SCHED, &n->state);
	return true;
}

/*
 * [LNX] include/linux/netdevice.h:5325: the interface copies out of a per-boot
 * random RSS key; this port has no such key store, so the caller's buffer is
 * filled from the port's random source. The driver seeds a firmware command's
 * secret key with it (mvm/fw.c:67), which wants fresh random bytes either way.
 */
void
netdev_rss_key_fill(void *buffer, size_t len)
{

	if (buffer == NULL || len == 0)
		return;
	get_random_bytes(buffer, len);
}

/* ------------------------------------------------------------------ */
/* the rtnetlink lock query (driver: os_intfs.c:2412)                   */
/* ------------------------------------------------------------------ */

int
rtnl_is_locked(void)
{

	/*
	 * This port has no rtnetlink core and so no rtnl mutex. The driver asks
	 * from the interface add/del/change paths, which on Linux run with the
	 * lock held; here those paths are serialized by the driver's own device
	 * mutex (hw_init_mutex), so "the serializing lock is held" is the true
	 * answer and keeps the driver's rtw_rtnl_lock_needed() bookkeeping in the
	 * state it expects. Minimal on purpose: no lock object is created.
	 */
	return 1;
}
