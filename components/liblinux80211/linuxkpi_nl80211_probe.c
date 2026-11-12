/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   linuxkpi_nl80211_probe.c
 * @brief  The bench's calls into the real nl80211 handler chain.
 *
 * The image has no netlink socket (ADR D1), but nl80211's family is registered
 * and its handlers are real.  This file builds the attribute streams a
 * userspace `iw` would send, parses them with the kernel's own parser, and
 * dispatches them through the family's pre_doit / doit / post_doit exactly as
 * genetlink would.  What is absent is the socket, the portid and the receive
 * queue - nothing else.
 *
 * C4 needed NL80211_CMD_TRIGGER_SCAN.  C5 adds NL80211_CMD_CONNECT and
 * NL80211_CMD_DISCONNECT for the open bench AP, plus a lock-protected read of
 * the station's connected state.  The same dispatch runs the commands a
 * supplicant would issue in the WPA2 round, so it is written as one table
 * lookup rather than one function per command.
 */
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/net_namespace.h>

#include "linuxkpi_core.h"
#include "linux80211/nl80211_probe.h"

static int
nl80211_doit(const struct genl_family *family, struct net_device *dev,
	     struct sk_buff *skb, const struct nlmsghdr *nlh,
	     struct genlmsghdr *ghdr, u8 cmd)
{
	struct nlattr *attrs[NL80211_ATTR_MAX + 1];
	struct genl_split_ops op;
	struct genl_info info;
	bool have_op = false;
	int rc;

	memset(attrs, 0, sizeof(attrs));
	rc = nlmsg_parse_deprecated(nlh, GENL_HDRLEN, attrs,
				    NL80211_ATTR_MAX, family->policy, NULL);
	if (rc < 0)
		return rc;

	memset(&op, 0, sizeof(op));
	if (family->small_ops != NULL) {
		unsigned int i;

		for (i = 0; i < family->n_small_ops; i++) {
			const struct genl_small_ops *s = &family->small_ops[i];

			if (s->cmd != cmd)
				continue;
			op.doit = s->doit;
			op.cmd = s->cmd;
			op.internal_flags = s->internal_flags;
			op.flags = s->flags;
			op.validate = s->validate;
			have_op = op.doit != NULL;
			break;
		}
	}
	if (!have_op && family->ops != NULL) {
		unsigned int i;

		for (i = 0; i < family->n_ops; i++) {
			const struct genl_ops *o = &family->ops[i];

			if (o->cmd != cmd)
				continue;
			op.doit = o->doit;
			op.cmd = o->cmd;
			op.internal_flags = o->internal_flags;
			op.flags = o->flags;
			op.validate = o->validate;
			have_op = op.doit != NULL;
			break;
		}
	}
	if (!have_op)
		return -EOPNOTSUPP;

	memset(&info, 0, sizeof(info));
	info.family = family;
	info.attrs = attrs;
	info.nlhdr = nlh;
	info.genlhdr = ghdr;
	info.snd_portid = 0;
	info.snd_seq = 1;
	genl_info_net_set(&info, &init_net);

	if (family->pre_doit != NULL) {
		rc = family->pre_doit(&op, skb, &info);
		if (rc != 0)
			return rc;
	}

	rc = op.doit(skb, &info);

	if (family->post_doit != NULL)
		family->post_doit(&op, skb, &info);
	return rc;
}

int
lkpi_nl80211_trigger_scan(struct net_device *dev, const char *ssid,
			  unsigned int freq_mhz)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	struct nlattr *nest;
	int rc;

	if (dev == NULL)
		return -EINVAL;
	/* ssid == NULL means a wildcard scan (no SSIDS attribute); freq_mhz == 0
	 * means every channel the wiphy supports (no FREQUENCIES attribute). */
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(256, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_TRIGGER_SCAN;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;

	if (ssid != NULL) {
		nest = nla_nest_start_noflag(skb, NL80211_ATTR_SCAN_SSIDS);
		if (nest == NULL) {
			rc = -EMSGSIZE;
			goto out;
		}
		rc = nla_put(skb, 0, (int)strlen(ssid), ssid);
		if (rc < 0)
			goto out;
		nla_nest_end(skb, nest);
	}

	if (freq_mhz != 0) {
		nest = nla_nest_start_noflag(skb,
					     NL80211_ATTR_SCAN_FREQUENCIES);
		if (nest == NULL) {
			rc = -EMSGSIZE;
			goto out;
		}
		rc = nla_put_u32(skb, 0, freq_mhz);
		if (rc < 0)
			goto out;
		nla_nest_end(skb, nest);
	}

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr,
			  NL80211_CMD_TRIGGER_SCAN);

out:
	kfree_skb(skb);
	return rc;
}

int
lkpi_nl80211_bss_find(struct wiphy *wiphy, const char *ssid,
		      unsigned char bssid[6], int *freq, int *signal)
{
	struct cfg80211_bss *bss;
	size_t len;

	if (wiphy == NULL || ssid == NULL || bssid == NULL)
		return -EINVAL;
	len = strlen(ssid);
	bss = cfg80211_get_bss(wiphy, NULL, NULL, (const u8 *)ssid, len,
			       IEEE80211_BSS_TYPE_ESS, IEEE80211_PRIVACY_ANY);
	if (bss == NULL)
		return -ENOENT;

	memcpy(bssid, bss->bssid, 6);
	if (freq != NULL && bss->channel != NULL)
		*freq = bss->channel->center_freq;
	if (signal != NULL)
		*signal = bss->signal;
	cfg80211_put_bss(wiphy, bss);
	return 0;
}

/*
 * NL80211_CMD_CONNECT for the open bench AP: the same attribute stream `iw
 * connect` would send.  The handler starts mac80211's auth/assoc; completion is
 * asynchronous and is observed through lkpi_nl80211_connected().
 */
int
lkpi_nl80211_connect(struct net_device *dev, const char *ssid,
		     const unsigned char bssid[6], unsigned int freq_mhz)
{
	if (ssid == NULL)
		return -EINVAL;
	return lkpi_nl80211_connect_ex(dev, ssid, (unsigned int)strlen(ssid),
				       bssid, freq_mhz, NULL, 0, 0);
}

int
lkpi_nl80211_connect_ex(struct net_device *dev, const char *ssid,
			unsigned int ssid_len,
			const unsigned char bssid[6],
			unsigned int freq_mhz, const void *ie,
			unsigned int ie_len, int privacy)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	int rc;

	if (dev == NULL || ssid == NULL || ssid_len == 0u || ssid_len > 32u)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(512 + (int)ie_len, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_CONNECT;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	rc = nla_put(skb, NL80211_ATTR_SSID, (int)ssid_len, ssid);
	if (rc < 0)
		goto out;
	if (bssid != NULL) {
		rc = nla_put(skb, NL80211_ATTR_MAC, 6, bssid);
		if (rc < 0)
			goto out;
	}
	if (freq_mhz != 0u) {
		rc = nla_put_u32(skb, NL80211_ATTR_WIPHY_FREQ, freq_mhz);
		if (rc < 0)
			goto out;
	}
	rc = nla_put_u32(skb, NL80211_ATTR_AUTH_TYPE,
			 NL80211_AUTHTYPE_OPEN_SYSTEM);
	if (rc < 0)
		goto out;
	if (ie != NULL && ie_len != 0u) {
		rc = nla_put(skb, NL80211_ATTR_IE, (int)ie_len, ie);
		if (rc < 0)
			goto out;
	}
	if (privacy != 0) {
		rc = nla_put_flag(skb, NL80211_ATTR_PRIVACY);
		if (rc < 0)
			goto out;
	}

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr, NL80211_CMD_CONNECT);

out:
	kfree_skb(skb);
	return rc;
}

/*
 * The supplicant's scan-result IE source.  `bss->ies` is the probe-response set
 * when one arrived and the beacon set otherwise; reading it under RCU and
 * copying before returning keeps the supplicant world free of cfg80211
 * references and of the BSS lock.
 */
int
lkpi_nl80211_bss_get(struct wiphy *wiphy, const char *ssid,
		     unsigned int ssid_len, unsigned char bssid[6],
		     int *freq, int *signal, int *beacon_int,
		     int *capability, unsigned char *ie,
		     unsigned int ie_max, unsigned int *ie_len)
{
	struct cfg80211_bss *bss;
	const struct cfg80211_bss_ies *ies;
	int rc = 0;

	if (wiphy == NULL || ssid == NULL || ssid_len == 0u || bssid == NULL)
		return -EINVAL;
	bss = cfg80211_get_bss(wiphy, NULL, NULL, (const u8 *)ssid, ssid_len,
			       IEEE80211_BSS_TYPE_ESS, IEEE80211_PRIVACY_ANY);
	if (bss == NULL)
		return -ENOENT;

	memcpy(bssid, bss->bssid, 6);
	if (freq != NULL && bss->channel != NULL)
		*freq = bss->channel->center_freq;
	if (signal != NULL)
		*signal = bss->signal / 100;
	if (beacon_int != NULL)
		*beacon_int = bss->beacon_interval;
	if (capability != NULL)
		*capability = bss->capability;

	rcu_read_lock();
	ies = rcu_dereference(bss->ies);
	if (ies == NULL)
		ies = rcu_dereference(bss->beacon_ies);
	if (ies == NULL) {
		rc = -ENOENT;
	} else if (ie != NULL && ie_max != 0u) {
		if ((unsigned int)ies->len > ie_max) {
			rc = -EMSGSIZE;
		} else {
			memcpy(ie, ies->data, (size_t)ies->len);
			if (ie_len != NULL)
				*ie_len = (unsigned int)ies->len;
		}
	} else if (ie_len != NULL) {
		*ie_len = (unsigned int)ies->len;
	}
	rcu_read_unlock();

	cfg80211_put_bss(wiphy, bss);
	return rc;
}

int
lkpi_nl80211_disconnect(struct net_device *dev, unsigned int reason)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	int rc;

	if (dev == NULL)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(256, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_DISCONNECT;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	rc = nla_put_u16(skb, NL80211_ATTR_REASON_CODE, (u16)reason);
	if (rc < 0)
		goto out;

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr, NL80211_CMD_DISCONNECT);

out:
	kfree_skb(skb);
	return rc;
}

/*
 * The completion the connect handler does not return: mac80211's auth/assoc
 * runs asynchronously and cfg80211 marks the station connected when the assoc
 * response arrives.  The read is under the wiphy lock because that is the lock
 * the writer (the stack's completion path) holds.
 */
int
lkpi_nl80211_connected(struct net_device *dev, unsigned char bssid[6])
{
	struct wireless_dev *wdev;
	int connected;

	if (dev == NULL || dev->ieee80211_ptr == NULL)
		return -EINVAL;
	wdev = dev->ieee80211_ptr;
	if (wdev->wiphy == NULL)
		return -EINVAL;

	wiphy_lock(wdev->wiphy);
	connected = wdev->connected ? 1 : 0;
	if (connected && bssid != NULL)
		memcpy(bssid, wdev->u.client.connected_addr, 6);
	wiphy_unlock(wdev->wiphy);
	return connected;
}

/*
 * Where an association stopped.  The port's connect seam answers the driver
 * synchronously, so everything after it happens inside cfg80211's own SME
 * (scan -> auth -> assoc); the case's wait loop can only see the last step.
 * This returns the intermediate steps as bits so one line says which one never
 * happened:
 *   bit0  the target BSS is in cfg80211's scan table (so a scan found it)
 *   bit1  the station authenticated (mac80211's current_bss is set)
 *   bit2  cfg80211 considers the station connected (assoc + assoc-resp done)
 * A zero is "nothing happened yet", not an error; a negative errno is bad input.
 */
int
lkpi_nl80211_assoc_progress(struct net_device *dev, const char *ssid,
			    unsigned int ssid_len)
{
	struct wireless_dev *wdev;
	struct cfg80211_bss *bss;
	int progress = 0;

	if (dev == NULL || dev->ieee80211_ptr == NULL)
		return -EINVAL;
	wdev = dev->ieee80211_ptr;
	if (wdev->wiphy == NULL)
		return -EINVAL;

	if (ssid != NULL && ssid_len != 0u) {
		bss = cfg80211_get_bss(wdev->wiphy, NULL, NULL,
				       (const u8 *)ssid, ssid_len,
				       IEEE80211_BSS_TYPE_ESS,
				       IEEE80211_PRIVACY_ANY);
		if (bss != NULL) {
			progress |= 0x1;
			cfg80211_put_bss(wdev->wiphy, bss);
		}
	}
	wiphy_lock(wdev->wiphy);
	if (wdev->links[0].client.current_bss != NULL)
		progress |= 0x2;
	if (wdev->connected)
		progress |= 0x4;
	wiphy_unlock(wdev->wiphy);
	return progress;
}

/*
 * The operating frequency, in MHz: the AP's chandef for an AP interface, the
 * connected BSS's channel for a station.  `connected` alone cannot answer
 * "which band", and a 5 GHz attempt that silently fell back to 2.4 GHz would
 * otherwise read as a pass.  The station's lookup goes through cfg80211's own
 * public BSS reference (the same call the supplicant's scan-result path uses),
 * so the port never reaches into the internal BSS record.
 */
int
lkpi_nl80211_link_freq(struct net_device *dev)
{
	struct wireless_dev *wdev;
	int freq = 0;
	u8 bssid[ETH_ALEN];
	int connected;

	if (dev == NULL || dev->ieee80211_ptr == NULL)
		return -EINVAL;
	wdev = dev->ieee80211_ptr;
	if (wdev->wiphy == NULL)
		return -EINVAL;

	wiphy_lock(wdev->wiphy);
	if (wdev->iftype == NL80211_IFTYPE_AP ||
	    wdev->iftype == NL80211_IFTYPE_P2P_GO) {
		if (wdev->links[0].ap.chandef.chan != NULL)
			freq = (int)wdev->links[0].ap.chandef.chan->center_freq;
	}
	connected = wdev->connected ? 1 : 0;
	if (connected)
		memcpy(bssid, wdev->u.client.connected_addr, ETH_ALEN);
	wiphy_unlock(wdev->wiphy);

	if (!connected)
		return freq;
	{
		struct cfg80211_bss *bss;

		bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid, NULL, 0,
				       IEEE80211_BSS_TYPE_ESS,
				       IEEE80211_PRIVACY_ANY);
		if (bss != NULL) {
			if (bss->channel != NULL)
				freq = (int)bss->channel->center_freq;
			cfg80211_put_bss(wdev->wiphy, bss);
		}
	}
	return freq;
}

/* --- the AP round (2026-11-11, docs/HANDOFF-AP-MODE.md) ------------------ */

/*
 * NL80211_CMD_SET_INTERFACE.  The AP case changes the default STA interface
 * into the AP under the same name: with the device down mac80211's
 * ieee80211_if_change_type() tears the sdata down and sets it up as the new
 * type without a driver op (iface.c:2072-2096, the same path `iw dev wlan0 set
 * type ap` takes).  Nothing here has to know the wiphy index or create a
 * second netdev.
 */
int
lkpi_nl80211_set_interface(struct net_device *dev, unsigned int type)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	int rc;

	if (dev == NULL)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(256, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_SET_INTERFACE;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	rc = nla_put_u32(skb, NL80211_ATTR_IFTYPE, type);
	if (rc < 0)
		goto out;

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr,
			  NL80211_CMD_SET_INTERFACE);

out:
	kfree_skb(skb);
	return rc;
}

/*
 * NL80211_CMD_START_AP.  The hostapd attribute stream, minus what this AP does
 * not use: BEACON_HEAD is the 36-byte 802.11 head (header + fixed fields) and
 * BEACON_TAIL the IEs *after* the TIM, which mac80211 inserts itself between
 * the two (tx.c:5626-5652).  The HT capability comes from the tail's HT Cap IE
 * (nl80211.c:6966-6976), so no separate capability attribute is sent; the
 * channel is the new-API WIDTH/CENTER_FREQ1 pair, as hostapd does.
 */
int
lkpi_nl80211_start_ap(struct net_device *dev, const char *ssid,
		      unsigned int freq_mhz, unsigned int width,
		      unsigned int beacon_interval, unsigned int dtim,
		      const unsigned char *head, unsigned int head_len,
		      const unsigned char *tail, unsigned int tail_len,
		      int privacy)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	int rc;

	if (dev == NULL || ssid == NULL || head == NULL || head_len == 0)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(256 + head_len + tail_len, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_START_AP;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	rc = nla_put(skb, NL80211_ATTR_SSID, (int)strlen(ssid), ssid);
	if (rc < 0)
		goto out;
	rc = nla_put_u32(skb, NL80211_ATTR_BEACON_INTERVAL, beacon_interval);
	if (rc < 0)
		goto out;
	rc = nla_put_u32(skb, NL80211_ATTR_DTIM_PERIOD, dtim);
	if (rc < 0)
		goto out;
	rc = nla_put(skb, NL80211_ATTR_BEACON_HEAD, (int)head_len, head);
	if (rc < 0)
		goto out;
	if (tail != NULL && tail_len != 0) {
		rc = nla_put(skb, NL80211_ATTR_BEACON_TAIL, (int)tail_len,
			     tail);
		if (rc < 0)
			goto out;
	}
	rc = nla_put_u32(skb, NL80211_ATTR_HIDDEN_SSID,
			 NL80211_HIDDEN_SSID_NOT_IN_USE);
	if (rc < 0)
		goto out;
	rc = nla_put_u32(skb, NL80211_ATTR_AUTH_TYPE,
			 NL80211_AUTHTYPE_OPEN_SYSTEM);
	if (rc < 0)
		goto out;
	rc = nla_put_u32(skb, NL80211_ATTR_WIPHY_FREQ, freq_mhz);
	if (rc < 0)
		goto out;
	rc = nla_put_u32(skb, NL80211_ATTR_CHANNEL_WIDTH, width);
	if (rc < 0)
		goto out;
	rc = nla_put_u32(skb, NL80211_ATTR_CENTER_FREQ1, freq_mhz);
	if (rc < 0)
		goto out;
	if (privacy != 0) {
		rc = nla_put_flag(skb, NL80211_ATTR_PRIVACY);
		if (rc < 0)
			goto out;
	}
	/* The handler defaults this to ETH_P_PAE when absent; state it so the
	 * control-port classification does not depend on that default. */
	rc = nla_put_u16(skb, NL80211_ATTR_CONTROL_PORT_ETHERTYPE,
			 ETH_P_PAE);
	if (rc < 0)
		goto out;

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr, NL80211_CMD_START_AP);

out:
	kfree_skb(skb);
	return rc;
}

int
lkpi_nl80211_stop_ap(struct net_device *dev)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	int rc;

	if (dev == NULL)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(256, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_STOP_AP;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr, NL80211_CMD_STOP_AP);

out:
	kfree_skb(skb);
	return rc;
}

struct lkpi_sta_iter {
	unsigned char (*macs)[6];
	unsigned int max;
	unsigned int seen;
};

static void
lkpi_sta_iter_cb(void *data, struct ieee80211_sta *sta)
{
	struct lkpi_sta_iter *it = data;

	if (sta == NULL)
		return;
	if (it->macs != NULL && it->seen < it->max)
		memcpy(it->macs[it->seen], sta->addr, 6);
	it->seen++;
}

/*
 * The AP's station table, through mac80211's atomic iterator: this is the
 * read-side answer for A2, and it needs no driver op and no netlink reply.
 * The image has one vif (the AP) by the time this runs, so every uploaded
 * station belongs to it.
 */
int
lkpi_nl80211_sta_count(struct net_device *dev, unsigned char macs[][6],
		       unsigned int max, unsigned int *count)
{
	struct wireless_dev *wdev;
	struct ieee80211_hw *hw;
	struct lkpi_sta_iter it;

	if (dev == NULL || dev->ieee80211_ptr == NULL)
		return -EINVAL;
	wdev = dev->ieee80211_ptr;
	if (wdev->wiphy == NULL)
		return -EINVAL;

	/* struct ieee80211_local embeds struct ieee80211_hw first
	 * (stack/mac80211/ieee80211_i.h:1448-1452); wiphy_priv() is the same
	 * answer the stack's own cfg80211 ops use. */
	hw = (struct ieee80211_hw *)wiphy_priv(wdev->wiphy);
	memset(&it, 0, sizeof(it));
	it.macs = macs;
	it.max = max;
	ieee80211_iterate_stations_atomic(hw, lkpi_sta_iter_cb, &it);
	if (count != NULL)
		*count = it.seen;
	return 0;
}

int
lkpi_nl80211_iftype(struct net_device *dev)
{
	struct wireless_dev *wdev;
	int type;

	if (dev == NULL || dev->ieee80211_ptr == NULL)
		return -EINVAL;
	wdev = dev->ieee80211_ptr;
	if (wdev->wiphy == NULL)
		return -EINVAL;

	wiphy_lock(wdev->wiphy);
	type = (int)wdev->iftype;
	wiphy_unlock(wdev->wiphy);
	return type;
}

/* --- the AP MLME's three actions (2026-11-12) ----------------------------
 *
 * These are the commands hostapd issues after it has parsed the management
 * frame: NL80211_CMD_FRAME for the response, NEW_STATION to create the client,
 * SET_STATION to open the controlled port.  They go through the same real
 * handler chain as the rest of this file (the socket is the only absence).
 */

/*
 * NL80211_CMD_FRAME.  DONT_WAIT_FOR_ACK is what makes this path socket-free:
 * with it, nl80211_tx_mgmt() skips the NL80211_CMD_FRAME reply it would
 * otherwise unicast to a portid (stack/wireless/nl80211.c:14838-14847).
 * mac80211 then sends the frame with IEEE80211_TX_CTL_NO_ACK on the AP's
 * current channel; the rate is the driver's lowest basic rate, as hostapd's
 * frames were (1.0 Mb/s on ch11).
 */
int
lkpi_nl80211_mgmt_tx(struct net_device *dev, const void *frame,
		     unsigned int len, int no_cck)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	int rc;

	if (dev == NULL || frame == NULL || len < 24u || len > 1600u)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(256 + (int)len, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_FRAME;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	rc = nla_put(skb, NL80211_ATTR_FRAME, (int)len, frame);
	if (rc < 0)
		goto out;
	rc = nla_put_flag(skb, NL80211_ATTR_DONT_WAIT_FOR_ACK);
	if (rc < 0)
		goto out;
	if (no_cck) {
		rc = nla_put_flag(skb, NL80211_ATTR_TX_NO_CCK_RATE);
		if (rc < 0)
			goto out;
	}

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr, NL80211_CMD_FRAME);

out:
	kfree_skb(skb);
	return rc;
}

/*
 * NL80211_CMD_NEW_STATION.  The attribute set is hostapd's nl80211_sta_add():
 * MAC, AID, listen interval, the station's supported rates (IE 1 followed by
 * IE 50), its capability field and its HT capability when it sent one.
 *
 * STA_FLAGS2 carries WME only when the station advertised WMM: without the WME
 * flag nl80211 drops the HT capability by design (nl80211.c:9512-9527), so a
 * non-WMM station must not be given HT.  AUTHENTICATED/ASSOCIATED are
 * deliberately *not* in the mask: iwlwifi does not set
 * NL80211_FEATURE_FULL_AP_CLIENT_STATE and nl80211 rejects those bits in a
 * mask, then adds them itself for compatibility (nl80211.c:9546-9558).
 */
int
lkpi_nl80211_new_station(struct net_device *dev,
			 const struct lkpi_ap_sta_add *sta)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	struct nl80211_sta_flag_update flags;
	int rc;

	if (dev == NULL || sta == NULL || sta->rates == NULL ||
	    sta->rates_len == 0u)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(512 + (int)sta->rates_len, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_NEW_STATION;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	rc = nla_put(skb, NL80211_ATTR_MAC, 6, sta->mac);
	if (rc < 0)
		goto out;
	rc = nla_put_u16(skb, NL80211_ATTR_STA_AID, sta->aid);
	if (rc < 0)
		goto out;
	rc = nla_put_u16(skb, NL80211_ATTR_STA_LISTEN_INTERVAL,
			 sta->listen_interval);
	if (rc < 0)
		goto out;
	rc = nla_put(skb, NL80211_ATTR_STA_SUPPORTED_RATES,
		     (int)sta->rates_len, sta->rates);
	if (rc < 0)
		goto out;
	rc = nla_put_u16(skb, NL80211_ATTR_STA_CAPABILITY, sta->capab);
	if (rc < 0)
		goto out;
	if (sta->ht_capa != NULL) {
		rc = nla_put(skb, NL80211_ATTR_HT_CAPABILITY, 26,
			     sta->ht_capa);
		if (rc < 0)
			goto out;
	}

	memset(&flags, 0, sizeof(flags));
	if (sta->wmm != NULL)
		flags.mask = flags.set = 1u << NL80211_STA_FLAG_WME;
	rc = nla_put(skb, NL80211_ATTR_STA_FLAGS2, (int)sizeof(flags), &flags);
	if (rc < 0)
		goto out;

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr,
			  NL80211_CMD_NEW_STATION);

out:
	kfree_skb(skb);
	return rc;
}

/*
 * NL80211_CMD_SET_STATION with AUTHORIZED.  Both directions need it: RX drops
 * every non-EAPOL frame from an unauthorised station
 * (stack/mac80211/rx.c:2532-2538) and TX drops unicast data to one
 * (stack/mac80211/tx.c:3953-3962).  hostapd opens the port after association
 * in open mode; so do we.
 */
int
lkpi_nl80211_sta_authorized(struct net_device *dev,
			    const unsigned char mac[6])
{
	return lkpi_nl80211_sta_set_authorized(dev, mac, 1);
}

/*
 * NL80211_CMD_SET_STATION with the AUTHORIZED flag set or cleared.  Both
 * directions need the set: RX drops every non-EAPOL frame from an unauthorised
 * station (stack/mac80211/rx.c:2532-2538) and TX drops unicast data to one
 * (stack/mac80211/tx.c:3953-3962).  hostapd sets it when the port opens (for an
 * open AP right after association, for WPA2 after the 4-way handshake) and
 * clears it on deauth.
 */
int
lkpi_nl80211_sta_set_authorized(struct net_device *dev,
				const unsigned char mac[6], int authorized)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	struct nl80211_sta_flag_update flags;
	int rc;

	if (dev == NULL || mac == NULL)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(256, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_SET_STATION;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	rc = nla_put(skb, NL80211_ATTR_MAC, 6, mac);
	if (rc < 0)
		goto out;
	memset(&flags, 0, sizeof(flags));
	flags.mask = 1u << NL80211_STA_FLAG_AUTHORIZED;
	flags.set = authorized ? (1u << NL80211_STA_FLAG_AUTHORIZED) : 0u;
	rc = nla_put(skb, NL80211_ATTR_STA_FLAGS2, (int)sizeof(flags), &flags);
	if (rc < 0)
		goto out;

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr,
			  NL80211_CMD_SET_STATION);

out:
	kfree_skb(skb);
	return rc;
}

/*
 * NL80211_CMD_DEL_STATION: hostapd's sta_remove() path (deauth/disassoc and the
 * removal of an association that never completed).  The MAC is all this AP
 * needs; the reason code stays the kernel's default.
 */
int
lkpi_nl80211_del_station(struct net_device *dev, const unsigned char mac[6])
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	int rc;

	if (dev == NULL || mac == NULL)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(256, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_DEL_STATION;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	rc = nla_put(skb, NL80211_ATTR_MAC, 6, mac);
	if (rc < 0)
		goto out;

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr,
			  NL80211_CMD_DEL_STATION);

out:
	kfree_skb(skb);
	return rc;
}

/* --- the WPA2 key path (2026-11-12) -------------------------------------- */

int
lkpi_nl80211_new_key(struct net_device *dev,
		     const struct lkpi_nl80211_key *key)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	struct nlattr *nest;
	int rc;

	if (dev == NULL || key == NULL || key->key == NULL ||
	    key->key_len == 0u || key->cipher == 0u)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;
	skb = alloc_skb(512 + (int)key->key_len, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;
	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_NEW_KEY;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	if (key->mac != NULL && !is_broadcast_ether_addr(key->mac)) {
		rc = nla_put(skb, NL80211_ATTR_MAC, 6, key->mac);
		if (rc < 0)
			goto out;
	}
	nest = nla_nest_start_noflag(skb, NL80211_ATTR_KEY);
	if (nest == NULL) {
		rc = -EMSGSIZE;
		goto out;
	}
	rc = nla_put(skb, NL80211_KEY_DATA, (int)key->key_len, key->key);
	if (rc < 0)
		goto out;
	rc = nla_put_u32(skb, NL80211_KEY_CIPHER, key->cipher);
	if (rc < 0)
		goto out;
	rc = nla_put_u8(skb, NL80211_KEY_IDX, (u8)key->key_idx);
	if (rc < 0)
		goto out;
	if (key->seq != NULL && key->seq_len != 0u) {
		rc = nla_put(skb, NL80211_KEY_SEQ, (int)key->seq_len,
			     key->seq);
		if (rc < 0)
			goto out;
	}
	nla_nest_end(skb, nest);
	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr,
			  NL80211_CMD_NEW_KEY);
out:
	kfree_skb(skb);
	return rc;
}

int
lkpi_nl80211_set_default_key(struct net_device *dev, int key_idx,
			     int multicast)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	struct nlattr *nest;
	struct nlattr *types;
	int rc;

	if (dev == NULL || key_idx < 0)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;
	skb = alloc_skb(256, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;
	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_SET_KEY;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	nest = nla_nest_start_noflag(skb, NL80211_ATTR_KEY);
	if (nest == NULL) {
		rc = -EMSGSIZE;
		goto out;
	}
	rc = nla_put_u8(skb, NL80211_KEY_IDX, (u8)key_idx);
	if (rc < 0)
		goto out;
	rc = nla_put_flag(skb, NL80211_KEY_DEFAULT);
	if (rc < 0)
		goto out;
	types = nla_nest_start_noflag(skb, NL80211_KEY_DEFAULT_TYPES);
	if (types == NULL) {
		rc = -EMSGSIZE;
		goto out;
	}
	rc = nla_put_flag(skb, multicast ?
			  NL80211_KEY_DEFAULT_TYPE_MULTICAST :
			  NL80211_KEY_DEFAULT_TYPE_UNICAST);
	if (rc < 0)
		goto out;
	nla_nest_end(skb, types);
	nla_nest_end(skb, nest);
	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr,
			  NL80211_CMD_SET_KEY);
out:
	kfree_skb(skb);
	return rc;
}

int
lkpi_nl80211_del_key(struct net_device *dev, int key_idx,
		     const unsigned char *mac)
{
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	struct nlattr *nest;
	int rc;

	if (dev == NULL || key_idx < 0)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;
	skb = alloc_skb(256, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;
	nlh = nlmsg_put(skb, 0, 1, family->id, GENL_HDRLEN, 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	ghdr = nlmsg_data(nlh);
	memset(ghdr, 0, sizeof(*ghdr));
	ghdr->cmd = NL80211_CMD_DEL_KEY;
	ghdr->version = family->version;

	rc = nla_put_u32(skb, NL80211_ATTR_IFINDEX, (u32)dev->ifindex);
	if (rc < 0)
		goto out;
	if (mac != NULL && !is_broadcast_ether_addr(mac)) {
		rc = nla_put(skb, NL80211_ATTR_MAC, 6, mac);
		if (rc < 0)
			goto out;
	}
	nest = nla_nest_start_noflag(skb, NL80211_ATTR_KEY);
	if (nest == NULL) {
		rc = -EMSGSIZE;
		goto out;
	}
	rc = nla_put_u8(skb, NL80211_KEY_IDX, (u8)key_idx);
	if (rc < 0)
		goto out;
	nla_nest_end(skb, nest);
	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr,
			  NL80211_CMD_DEL_KEY);
out:
	kfree_skb(skb);
	return rc;
}
