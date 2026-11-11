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
	const struct genl_family *family;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct genlmsghdr *ghdr;
	int rc;

	if (dev == NULL || ssid == NULL)
		return -EINVAL;
	family = lkpi_genl_family_get(NL80211_GENL_NAME);
	if (family == NULL)
		return -ENOENT;

	skb = alloc_skb(512, GFP_KERNEL);
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
	rc = nla_put(skb, NL80211_ATTR_SSID, (int)strlen(ssid), ssid);
	if (rc < 0)
		goto out;
	if (bssid != NULL) {
		rc = nla_put(skb, NL80211_ATTR_MAC, 6, bssid);
		if (rc < 0)
			goto out;
	}
	if (freq_mhz != 0) {
		rc = nla_put_u32(skb, NL80211_ATTR_WIPHY_FREQ, freq_mhz);
		if (rc < 0)
			goto out;
	}
	rc = nla_put_u32(skb, NL80211_ATTR_AUTH_TYPE,
			 NL80211_AUTHTYPE_OPEN_SYSTEM);
	if (rc < 0)
		goto out;

	nlmsg_end(skb, nlh);
	rc = nl80211_doit(family, dev, skb, nlh, ghdr, NL80211_CMD_CONNECT);

out:
	kfree_skb(skb);
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
