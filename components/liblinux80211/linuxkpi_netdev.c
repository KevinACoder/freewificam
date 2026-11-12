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

#include "linux80211/diag.h"
#include "linux80211/net.h"

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

static void lkpi_netdev_notify(unsigned long event, struct net_device *dev);

/* [LNX] net/core/dev.c: dev_new_index().  The port's registry is a small
 * array, so a monotonic counter is the index; zero is "unset" and the lookup
 * already compares the stored value.  nl80211 resolves an interface by
 * NL80211_ATTR_IFINDEX, so a registered device must have a non-zero one. */
static unsigned int lkpi_netdev_ifindex;

int
register_netdev(struct net_device *dev)
{

	if (dev == NULL)
		return -EINVAL;
	if (dev->ifindex == 0)
		dev->ifindex = (int)++lkpi_netdev_ifindex;
	lkpi_netdev_notify(NETDEV_POST_INIT, dev);
	if (!lkpi_netdev_track(dev))
		return 0;		/* already findable by name */
	/* the device is present; the link's carrier is the driver's to raise */
	set_bit(__LINK_STATE_PRESENT, &dev->state);
	lkpi_netdev_notify(NETDEV_REGISTER, dev);
	lkpi_printf(ULOG_INFO_LEVEL, "lkpi netdev: registered %s\n", dev->name);
	return 0;
}

void
unregister_netdev(struct net_device *dev)
{

	if (dev != NULL) {
		lkpi_netdev_notify(NETDEV_UNREGISTER, dev);
		lkpi_netdev_untrack(dev);
	}
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
 * Registration delivers the two events the interface's own core delivers:
 * NETDEV_POST_INIT (cfg80211's notifier sets wdev->netdev there - without it a
 * registered wlan0 has a wireless_dev whose netdev is NULL, and nl80211's
 * ifindex lookup answers -ENODEV) and NETDEV_REGISTER.  Events this port has no
 * producer for (CHANGE, UP/DOWN, ...) are not invented; the scan path does not
 * need them.
 */
static void
lkpi_netdev_notify(unsigned long event, struct net_device *dev)
{
	struct netdev_notifier_info info;
	unsigned int i;

	memset(&info, 0, sizeof(info));
	info.dev = dev;
	for (i = 0; i < CONFIG_LKPI_NOTIFIER_MAX; i++)
		if (lkpi_netdev_notifiers[i] != NULL &&
		    lkpi_netdev_notifiers[i]->notifier_call != NULL)
			(void)lkpi_netdev_notifiers[i]->notifier_call(
				lkpi_netdev_notifiers[i], event, &info);
}
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
	/* [LNX] net/ethernet/eth.c:eth_type_trans: the interface records the
	 * MAC header before pulling it.  Without this line skb_mac_header()
	 * falls back to skb->data, and a caller that rewrites the destination
	 * after the pull (mac80211's control-port delivery) writes the AP's
	 * address over the first six bytes of the EAPOL PDU.  That was the
	 * "frame too short for this IEEE 802.1X packet" on the AP. */
	skb_reset_mac_header(skb);
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

/* ------------------------------------------------------------------ */
/* the monitor sink (2026-11-12, docs/HANDOFF-AC7260-MONITOR.md P3)     */
/* ------------------------------------------------------------------ */

/*
 * mac80211 delivers a monitor interface's frames through netif_receive_skb()
 * -> netif_rx() exactly like data, with skb->dev pointing at the monitor
 * sub-ifdata whose type is ARPHRD_IEEE80211_RADIOTAP (iface.c:1947).  lwIP's
 * adapter would classify such a frame as foreign and free it (lkpinetif.c
 * g_dev mismatch), so the sink has to sit here: the frame's consumer is a
 * counter, not a stack.  The branch is inert unless a case enables it.
 */
static int lkpi_mon_enabled;
static struct lkpi_net_mon_diag lkpi_mon;

int
lkpi_net_monitor_enable(int enable)
{
	int prev = lkpi_mon_enabled;

	lkpi_mon_enabled = enable != 0;
	return prev;
}

int
lkpi_net_monitor_enabled(void)
{

	return lkpi_mon_enabled;
}

void
lkpi_net_monitor_reset(void)
{

	memset(&lkpi_mon, 0, sizeof(lkpi_mon));
}

void
lkpi_net_monitor_diag(struct lkpi_net_mon_diag *out)
{
	unsigned long best = 0UL;
	unsigned int i;

	if (out == NULL)
		return;
	*out = lkpi_mon;
	/* The dominant channel, not the first: a monitor opened before its
	 * channel request lands sees the wiphy's default channel for a few
	 * frames, and the frame count is what makes 2432 the answer. */
	out->freq_mhz = 0u;
	for (i = 0u; i < LKPI_NET_MON_FREQS; i++) {
		if (out->freq[i].freq == 0u)
			continue;
		if (out->freq[i].count >= best) {
			best = out->freq[i].count;
			out->freq_mhz = out->freq[i].freq;
		}
	}
}

/*
 * The radiotap header starts at skb->data: iwlwifi's monitor path prepends it
 * (mvm/rxmq.c iwl_mvm_add_rtap_sniffer_config) and mac80211's
 * ieee80211_rx_monitor() prepends one when the driver did not.  The 802.11
 * frame-control word follows it; radiotap's own it_len is the only way to
 * find it, and a header that claims to be longer than the frame is a bug we
 * count rather than parse.
 *
 * The channel field is not at a fixed offset: radiotap fields are aligned and
 * present only when their presence bit is set, and iwlwifi sets TSFT, FLAGS,
 * RATE, CHANNEL, signal, antenna and an extended MCS field.  The walk below is
 * the standard one ([size, alignment] per present bit) with the two fields the
 * verdict needs; a field this table does not know ends the walk without a
 * frequency rather than guessing one.
 */
static const unsigned char lkpi_rtap_size[29] = {
	8, 1, 1, 4, 2, 1, 1, 2, 2, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 3,
	8, 12, 12, 12, 12, 6, 12, 1, 4
};
static const unsigned char lkpi_rtap_align[29] = {
	8, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1,
	8, 2, 8, 2, 2, 2, 2, 1, 2
};

static unsigned int
lkpi_rtap_align_up(unsigned int off, unsigned int a)
{

	return (off + a - 1u) & ~(a - 1u);
}

/* Fills *freq with the radiotap channel frequency; returns the it_len, or 0
 * when the header cannot be walked. */
static unsigned int
lkpi_rtap_walk(const unsigned char *data, unsigned int len, unsigned int *freq)
{
	unsigned int it_len;
	unsigned int present;
	unsigned int off = 8u;
	unsigned int bit;

	if (len < 8u)
		return 0u;
	it_len = (unsigned int)data[2] | ((unsigned int)data[3] << 8);
	if (it_len < 8u || it_len > len)
		return 0u;
	present = (unsigned int)data[4] | ((unsigned int)data[5] << 8) |
		  ((unsigned int)data[6] << 16) | ((unsigned int)data[7] << 24);
	/*
	 * A presence word with bit 31 set is followed by another word, and the
	 * fixed fields begin after the last one (iwlwifi's monitor frames carry
	 * three: the standard set, its antenna pair, and a namespace word -
	 * 12 bytes of bitmap before TSFT).  Walking the standard fields of the
	 * first word from the wrong base misreads the channel, which is how the
	 * first sink version reported 8 MHz for a 2432 MHz capture.
	 */
	while ((present & 0x80000000u) != 0u) {
		if (off + 4u > it_len)
			return 0u;
		present = (unsigned int)data[off] |
			  ((unsigned int)data[off + 1u] << 8) |
			  ((unsigned int)data[off + 2u] << 16) |
			  ((unsigned int)data[off + 3u] << 24);
		off += 4u;
	}
	/* The standard fields are the first word's bits 0..28 (the namespace
	 * words' fields are vendor data and are not walked). */
	present = (unsigned int)data[4] | ((unsigned int)data[5] << 8) |
		  ((unsigned int)data[6] << 16) | ((unsigned int)data[7] << 24);
	present &= 0x1fffffffu;
	for (bit = 0u; bit < 29u; bit++) {
		if ((present & (1u << bit)) == 0u)
			continue;
		off = lkpi_rtap_align_up(off, lkpi_rtap_align[bit]);
		if (off + lkpi_rtap_size[bit] > it_len)
			return it_len;
		if (bit == 3u) {	/* CHANNEL: u16 freq, u16 flags */
			*freq = (unsigned int)data[off] |
				((unsigned int)data[off + 1u] << 8);
		}
		off += lkpi_rtap_size[bit];
	}
	return it_len;
}

static void
lkpi_mon_absorb(struct sk_buff *skb)
{
	struct lkpi_net_mon_diag *d = &lkpi_mon;
	unsigned int len = skb->len;
	unsigned int it_len;
	unsigned int freq = 0u;
	unsigned int fc;
	unsigned int type;
	unsigned int subtype;

	it_len = lkpi_rtap_walk(skb->data, len, &freq);
	if (it_len < 8u) {
		d->bad_radiotap++;
		return;
	}
	if (freq != 0u) {
		unsigned int i;

		for (i = 0u; i < LKPI_NET_MON_FREQS; i++) {
			if (d->freq[i].freq == freq) {
				d->freq[i].count++;
				break;
			}
			if (d->freq[i].freq == 0u) {
				d->freq[i].freq = freq;
				d->freq[i].count = 1;
				break;
			}
		}
	}
	if (d->it_len_min == 0u || it_len < d->it_len_min)
		d->it_len_min = it_len;
	if (it_len > d->it_len_max)
		d->it_len_max = it_len;
	if (it_len + 2u > len) {
		d->too_short++;
		return;
	}
	fc = (unsigned int)skb->data[it_len] |
	     ((unsigned int)skb->data[it_len + 1u] << 8);
	type = (fc >> 2) & 3u;
	subtype = (fc >> 4) & 0xfu;

	d->frames++;
	d->bytes += len;
	switch (type) {
	case 0:	/* management */
		switch (subtype) {
		case 8:  d->beacons++; break;
		case 4:  d->probes++; break;
		case 5:  d->probes++; d->probe_resp++; break;
		case 11: d->auth++; break;
		case 0:
		case 1:
		case 2:
		case 3:  d->assoc++; break;
		default: d->other++; break;
		}
		break;
	case 1:
		d->ctrl++;
		break;
	case 2:
		d->data++;
		break;
	default:
		d->other++;
		break;
	}
	if (d->kept < LKPI_NET_MON_KEEP) {
		struct lkpi_net_mon_frame *k = &d->keep[d->kept];
		unsigned int n = len < LKPI_NET_MON_KEEP_BYTES ?
				 len : LKPI_NET_MON_KEEP_BYTES;

		memset(k, 0, sizeof(*k));
		k->len = len;
		k->it_len = it_len;
		k->fc = fc;
		memcpy(k->head, skb->data, n);
		d->kept++;
	}
}

int
netif_rx(struct sk_buff *skb)
{

	if (skb == NULL)
		return NET_RX_DROP;
	if (lkpi_mon_enabled && skb->dev != NULL &&
	    skb->dev->type == ARPHRD_IEEE80211_RADIOTAP) {
		lkpi_mon_absorb(skb);
		kfree_skb(skb);
		return NET_RX_SUCCESS;
	}
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
 * The A4 round's first observation: how many times a poll returned the whole
 * budget.  On Linux the receive softirq re-polls until a poll returns less
 * than its weight, and only that poll re-enables the device interrupt
 * (iwlwifi's iwl_pcie_napi_poll()).  This port runs the poll inline in the
 * threaded interrupt and never re-arms it, so a hit is the difference between
 * "the device went quiet" and "the host stopped asking" - the 2026-11-12
 * stall's first candidate.  Counters only: the scheduling semantics are not
 * changed here.
 */
static struct lkpi_napi_health lkpi_napi_health;

void
lkpi_napi_health_get(struct lkpi_napi_health *out)
{

	if (out != NULL)
		*out = lkpi_napi_health;
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
	lkpi_napi_health.completes++;
	lkpi_napi_health.last_complete_jiffies = jiffies;
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

/* ------------------------------------------------------------------ */
/* the generic netdev allocator and the address helpers                */
/* ------------------------------------------------------------------ */

/*
 * [LNX] net/core/dev.c: dev_set_rx_mode() -> __dev_set_rx_mode() ->
 * ndo_set_rx_mode().  The port has no net core to run the helper, so this is
 * the one-call form the bench uses to make mac80211 re-program the receive
 * filter (ieee80211_set_multicast_list queues local->reconfig_filter, whose
 * ieee80211_configure_filter is the only caller of the driver's
 * configure_filter on the station path).
 */
void
dev_set_rx_mode(struct net_device *dev)
{

	if (dev == NULL || dev->netdev_ops == NULL)
		return;
	if (dev->netdev_ops->ndo_set_rx_mode != NULL)
		dev->netdev_ops->ndo_set_rx_mode(dev);
}

/*
 * [LNX] net/core/dev.c: `ether_setup()` gives a device the Ethernet shape.
 * This port's net_device carries the fields the compiled set reads; the ones
 * the interface sets but this record does not have (hard_header_len, header
 * ops) are not carried, and nothing in the set reads them.
 */
void
ether_setup(struct net_device *dev)
{

	if (dev == NULL)
		return;
	dev->type = ARPHRD_ETHER;
	dev->addr_len = ETH_ALEN;
	dev->min_mtu = 68;	/* ETH_MIN_MTU (uapi/linux/if_ether.h) */
	dev->max_mtu = ETH_DATA_LEN;
	if (dev->dev_addr == NULL) {
		unsigned char *addr = kzalloc(ETH_ALEN, GFP_KERNEL);

		if (addr != NULL) {
			dev->dev_addr = addr;
			dev->perm_addr = addr;
		}
	}
}

/*
 * [LNX] net/core/dev.c: `alloc_netdev_mqs()` allocates the record, the driver's
 * private tail, the address storage and the TX-queue array, then calls the
 * caller's setup().  `alloc_etherdev_mqs()` (above) is the same object with
 * ether_setup; keeping this one general is what mac80211's
 * `alloc_netdev_mqs(size, "wlan%d", ..., ieee80211_if_setup, 1, 1)` needs.
 */
struct net_device *
alloc_netdev_mqs(int sizeof_priv, const char *name,
		 unsigned char name_assign_type, void (*setup)(struct net_device *),
		 unsigned int txqs, unsigned int rxqs)
{
	struct net_device *dev;
	unsigned int i;

	(void)name_assign_type;
	(void)rxqs;
	if (sizeof_priv < 0)
		return NULL;
	dev = kzalloc(sizeof(*dev) + (size_t)sizeof_priv, GFP_KERNEL);
	if (dev == NULL)
		return NULL;
	if (name != NULL)
		snprintf(dev->name, IFNAMSIZ, "%s", name);
	if (setup != NULL)
		setup(dev);
	else
		ether_setup(dev);
	if (dev->dev_addr == NULL) {
		/* a setup() that did not give the device an address: the port's
		 * allocator still needs storage for eth_hw_addr_set()/perm_addr */
		unsigned char *addr = kzalloc(ETH_ALEN, GFP_KERNEL);

		if (addr == NULL) {
			kfree(dev);
			return NULL;
		}
		dev->dev_addr = addr;
		dev->perm_addr = addr;
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

/* [LNX] net/core/dev.c: the ndo_set_mac_address default. */
bool
eth_mac_addr(struct net_device *dev, void *p)
{

	if (dev == NULL || p == NULL)
		return false;
	dev_addr_mod(dev, 0, p, ETH_ALEN);
	return true;
}

void
netdev_set_default_ethtool_ops(struct net_device *dev,
			       const struct ethtool_ops *ops)
{

	/* this net_device record has no ethtool_ops slot (the face's note); the
	 * default only matters to a userspace ioctl path this port does not have */
	(void)dev;
	(void)ops;
}

u32
ethtool_op_get_link(struct net_device *dev)
{

	(void)dev;
	/* no carrier state is modelled at the ethtool level; the netdev's own
	 * IFF_UP/operstate is the observable, and it is not this call's */
	return 1;
}

/* ------------------------------------------------------------------ */
/* the operations cfg80211's netdev path names                          */
/* ------------------------------------------------------------------ */

struct net_device *
__dev_get_by_index(struct net *net, int ifindex)
{
	unsigned int i;

	(void)net;
	for (i = 0; i < CONFIG_LKPI_NETDEV_MAX; i++) {
		struct net_device *dev = lkpi_netdevs[i];

		if (dev != NULL && dev->ifindex == ifindex)
			return dev;
	}
	return NULL;
}

struct net_device *
dev_get_by_index(struct net *net, int ifindex)
{
	return __dev_get_by_index(net, ifindex);
}

void
dev_hold(struct net_device *dev)
{
	(void)dev;
}

int
dev_change_net_namespace(struct net_device *dev, struct net *net, const char *pat)
{
	(void)dev;
	(void)net;
	(void)pat;
	return 0;
}

/* [LNX] net/core/dev.c: open/close the device through its ndo pair.  The
 * interface's open path is what calls drv_start on a mac80211 driver, so the
 * store of IFF_UP and the return value of ndo_open are the observable.
 *
 * NETDEV_UP and NETDEV_DOWN are part of the interface, not an extra: the net
 * core fires UP after __dev_open() succeeds and DOWN after __dev_close()
 * (net/core/dev.c:1752, :1829), and cfg80211's notifier is the only writer of
 * rdev->num_running_ifaces / num_running_monitor_ifaces (wireless/core.c:1775,
 * :1797, :1497).  Without the pair those counters stay zero, and every
 * cfg80211 operation that asks "is this wiphy monitors-only and running" - the
 * monitor channel set is the first - refuses with -EBUSY even though the
 * interface is up.  That was the 2026-11-12 monitor round's M1 failure; the
 * fix is the interface's event pair, not a stack change.
 *
 * GOING_DOWN is deliberately not invented here: it runs cfg80211_leave(), and
 * the port's teardown already runs the driver's stop path.  A case that needs
 * a graceful leave (a station's deauth-on-close) has to call it itself, and
 * that is stated where the case lives.
 */
int
dev_open(struct net_device *dev)
{
	int r = 0;

	if (dev == NULL)
		return -EINVAL;
	if (dev->flags & IFF_UP)
		return 0;
	if (dev->netdev_ops != NULL && dev->netdev_ops->ndo_open != NULL) {
		r = dev->netdev_ops->ndo_open(dev);
		if (r != 0)
			return r;
	}
	/* [LNX] net/core/dev.c __dev_open(): netif_running() (and therefore
	 * cfg80211's wdev_running()) reads this bit, not IFF_UP.  Without it the
	 * interface is "up" to dev_open() and "down" to nl80211, which is the
	 * shape C4's scan trigger hits first. */
	set_bit(__LINK_STATE_START, &dev->state);
	dev->flags |= IFF_UP;
	lkpi_netdev_notify(NETDEV_UP, dev);
	return 0;
}

void
dev_close(struct net_device *dev)
{

	if (dev == NULL)
		return;
	if (!(dev->flags & IFF_UP))
		return;
	if (dev->netdev_ops != NULL && dev->netdev_ops->ndo_stop != NULL)
		(void)dev->netdev_ops->ndo_stop(dev);
	clear_bit(__LINK_STATE_START, &dev->state);
	dev->flags &= ~IFF_UP;
	lkpi_netdev_notify(NETDEV_DOWN, dev);
}

int
dev_queue_xmit(struct sk_buff *skb)
{

	/* no egress adapter is attached in the C2/C3 images; a frame that
	 * reaches here is consumed and reported as dropped */
	kfree_skb(skb);
	return 1;	/* NET_XMIT_DROP: the interface's "dropped" answer */
}

gro_result_t
napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	/* no GRO engine on this port: hand the frame to the same seam the
	 * non-NAPI path uses (ieee80211_rx_napi reaches here for every data
	 * frame, because the driver passes its napi instance). */
	(void)napi;
	(void)netif_rx(skb);
	return GRO_CONSUMED;
}

/*
 * The receive entries mac80211 actually calls.  They must funnel into the
 * same presentation seam as netif_rx(): an image with a netif feed delivers
 * there, an image without one drops the frame (netif_rx's own no-hook path).
 * Freeing here instead made every received data frame disappear: the ARP
 * reply that should answer the first ping was consumed by this stub
 * (2026-11-11 C5-open, C6: rx_frames=0 with the association up).
 */
int
netif_receive_skb(struct sk_buff *skb)
{

	return netif_rx(skb);
}

void
netif_receive_skb_list(struct list_head *head)
{
	struct sk_buff *skb;
	struct sk_buff *tmp;

	if (head == NULL)
		return;
	list_for_each_entry_safe(skb, tmp, head, list) {
		list_del_init(&skb->list);
		(void)netif_rx(skb);
	}
}

/* ------------------------------------------------------------------ */
/* multicast address lists (empty in these images)                     */
/* ------------------------------------------------------------------ */

void
__hw_addr_init(struct netdev_hw_addr_list *list)
{

	if (list == NULL)
		return;
	INIT_LIST_HEAD(&list->list);
	list->count = 0;
}

void
__hw_addr_sync(struct netdev_hw_addr_list *to_list,
	       struct netdev_hw_addr_list *from_list, int addr_len)
{

	(void)to_list;
	(void)from_list;
	(void)addr_len;
}

void
__hw_addr_unsync(struct netdev_hw_addr_list *to_list,
		 struct netdev_hw_addr_list *from_list, int addr_len)
{

	(void)to_list;
	(void)from_list;
	(void)addr_len;
}

/* ------------------------------------------------------------------ */
/* NAPI scheduling: this port's poll runs in the caller's context      */
/* ------------------------------------------------------------------ */

/*
 * [LNX] net/core/dev.c: the scheduling pair.  The interface hands the instance
 * to a softirq poller; this port has no softirq vector of its own, so the poll
 * runs in whoever scheduled it (the PCI threaded IRQ handler, or a driver's
 * work item).  The contract that matters is preserved: prep refuses a second
 * schedule (SCHED is the one-bit latch), the poll runs with the driver's own
 * weight, and the latch clears afterwards whether or not the poll completed
 * through napi_complete_done().  A future packet-rate path that needs the poll
 * off the IRQ context must move it to a work item here, not change the callers.
 */
/* The NAPI poll runs in the caller's context (the PCI threaded IRQ); the stack
 * asks in_softirq() and warns once if it is false.  This depth is that context's
 * honest answer: the poll is a deferred receive context, not a task's direct
 * call.  See lkpi_in_softirq() in linuxkpi_core.c. */
unsigned int lkpi_napi_poll_depth;

/* The driver's iTXQ dequeue is the transmit counterpart: mac80211's
 * ieee80211_tx_dequeue() is called from iwlwifi's wake_tx_queue path, which on
 * this port is a driver work/IRQ context, not a task's direct call.  The depth
 * is incremented by the --wrap shim in tx_dequeue_context.c around the real
 * dequeue; lkpi_in_softirq() reads it for the same reason it reads the NAPI
 * depth. */
unsigned int lkpi_tx_dequeue_depth;

bool
napi_schedule_prep(struct napi_struct *n)
{

	if (n == NULL)
		return false;
	/* test_and_set: false when SCHED was already set (already scheduled) */
	return !test_and_set_bit(NAPI_STATE_SCHED, &n->state);
}

/*
 * The re-poll bound.  The receive ring is 256 entries (RX_QUEUE_SIZE), and one
 * poll consumes at most `weight - 1` = 63 of them, so five rounds drain a full
 * ring; sixteen leaves the counter room to prove a drain that is not making
 * progress instead of spinning forever in the interrupt thread.
 */
#define LKPI_NAPI_ROUNDS_MAX	16u

void
__napi_schedule(struct napi_struct *n)
{
	int ret = 0;
	unsigned int weight, round;

	if (n == NULL || n->poll == NULL)
		return;
	if (test_bit(NAPI_STATE_NPSVC, &n->state)) {
		clear_bit(NAPI_STATE_SCHED, &n->state);
		return;
	}
	weight = (n->weight != 0) ? (unsigned int)n->weight : 64u;
	lkpi_napi_health.schedules++;
	lkpi_napi_health.last_schedule_jiffies = jiffies;
	lkpi_napi_health.last_weight = (unsigned long)weight;
	/*
	 * Linux's receive softirq polls until a poll returns less than its
	 * budget; only that poll re-enables the device interrupt
	 * (iwlwifi's iwl_pcie_napi_poll() writes CSR_INT_MASK from inside it).
	 * This port runs the poll inline in the threaded interrupt, so the
	 * repeat has to be here: a single poll that returned the whole budget
	 * left CSR_INT_MASK at zero and nothing scheduled another one - the
	 * 2026-11-12 AP stall's exact shape (napi_ret=64, napi_comp one short
	 * of napi_polls, IRQ thread returned, hard count frozen).
	 */
	for (round = 0u; round < LKPI_NAPI_ROUNDS_MAX; round++) {
		lkpi_napi_poll_depth++;
		ret = n->poll(n, (int)weight);
		lkpi_napi_poll_depth--;
		lkpi_napi_health.polls++;
		lkpi_napi_health.last_ret = (unsigned long)ret;
		if (ret < (int)weight)
			break;
		lkpi_napi_health.budget_hits++;
	}
	if (ret >= (int)weight)
		lkpi_napi_health.rounds_exhausted++;
	clear_bit(NAPI_STATE_SCHED, &n->state);
}
