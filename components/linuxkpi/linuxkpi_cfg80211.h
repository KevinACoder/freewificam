/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_cfg80211.h
 * @brief   The seam between the cfg80211 substitute body and its consumer: the
 *          board test that drives a scan, and the vendor driver's own runtime
 *          notifications coming back.
 *
 * @design ports & adapters, the same shape as the mac80211 substitute's seam
 *         (components/linuxkpi/linuxkpi_80211.h:34-77): the substitute body
 *         (linuxkpi_cfg80211.c) is the adapter-neutral core that implements the
 *         cfg80211 face the driver was written against, and the consumer is the
 *         adapter that knows what to do with a scan result.  The seam carries
 *         only what those two must exchange - five upward notifications (a
 *         wireless device appeared, a scan finished, an access point was
 *         reported, a link came up, a link went down) and the downward calls
 *         that let the consumer drive the driver through its ops table.
 *
 * Why the driver's own ops are called from here rather than by the consumer
 * directly: the ops signatures are cfg80211 types (struct wiphy, struct
 * wireless_dev, struct cfg80211_scan_request).  Keeping those types on this
 * side of the seam means the consumer never holds a driver-world struct in its
 * own control flow - it asks for an action by name (scan, connect, disconnect,
 * key) and this file performs the corresponding ops call, so the driver-call
 * order is one readable list in one place.
 *
 * Reference for the shape: components/linuxkpi/linuxkpi_80211.h:1-33.
 * @date    12.10.2026
 */

#ifndef LINUXKPI_CFG80211_H
#define LINUXKPI_CFG80211_H

#include "linuxkpi_core.h"

struct wiphy;

/* One access point as the substitute holds it, filled from the management frame
 * the driver handed to cfg80211_inform_bss_frame().  The fields are the ones a
 * scan consumer compares against ground truth: the identity (bssid), the name
 * (ssid), where it was heard (freq) and how well (signal), plus the information
 * elements the frame carried. */
struct lkpicfg_bss {
	u8	bssid[6];
	u8	ssid[33];
	u8	ssid_len;
	u32	freq;		/* MHz, the channel's centre frequency */
	int	signal;		/* dBm */
	u16	capability;
	/* The frame's element stream, borrowed from the table entry: a consumer
	 * that needs the security elements (a supplicant matching an RSN/WPA
	 * network) reads them during the call and copies what it keeps.  The
	 * pointer is valid until the substitute's table changes. */
	const u8 *ies;
	size_t	ies_len;
};

/* ------------------------------------------------------------------ */
/* upward: what the substitute tells its consumer                      */
/* ------------------------------------------------------------------ */

struct lkpicfg_consumer_ops {
	/* Called from wiphy_register(): the driver has a wireless device. */
	void	(*attached)(struct wiphy *wiphy);
	/* Called from cfg80211_scan_done(): the driver's sweep finished. */
	void	(*scan_done)(struct wiphy *wiphy);
	/* One candidate, as it was reported (ownership stays with the
	 * substitute: the record is only valid for the call). */
	void	(*bss)(struct wiphy *wiphy, const struct lkpicfg_bss *bss);
	/* Called from cfg80211_connect_result()/cfg80211_roamed(). */
	void	(*connected)(struct wiphy *wiphy, const u8 bssid[6], u16 status);
	/* Called from cfg80211_disconnected(). */
	void	(*disconnected)(struct wiphy *wiphy, u16 reason);
};

/* Install the consumer.  Without one the substitute still records everything;
 * it just has nobody to tell. */
int	lkpicfg_consumer_register(const struct lkpicfg_consumer_ops *ops,
	    void *arg);

/* ------------------------------------------------------------------ */
/* downward: the consumer drives the driver through these              */
/* ------------------------------------------------------------------ */

/* The wireless device the driver registered, or NULL before it registers one. */
struct wiphy *lkpicfg_wiphy(void);

/* Build a scan request (the SSID list and the channel list the device
 * advertises) and call the driver's scan op.  Returns 0 when the driver took
 * the request, a negative error otherwise. */
int	lkpicfg_scan_start(struct wiphy *wiphy);

/* The table the substitute collected from the driver's own reports. */
unsigned int	lkpicfg_bss_count(struct wiphy *wiphy);
int	lkpicfg_bss_at(struct wiphy *wiphy, unsigned int idx,
	    struct lkpicfg_bss *out);
void	lkpicfg_bss_clear(struct wiphy *wiphy);

/* The device's hardware address (wiphy->perm_addr, which the driver fills from
 * the chip's eFuse before it registers). */
int	lkpicfg_get_hwaddr(struct wiphy *wiphy, u8 addr[6]);

/* Bring the driver's network interface up (the driver's own ndo_open path).
 * Returns 0 on success. */
int	lkpicfg_netdev_up(struct wiphy *wiphy);

/* ------------------------------------------------------------------ */
/* downward: the MLME calls a supplicant consumer drives               */
/* ------------------------------------------------------------------ */

/* Join a network.  `ssid`/`ssid_len` are required; `bssid` pins the access point
 * (NULL joins by SSID).  `ie` is the association element stream the caller built
 * - for a PSK network its RSN/WPA element is where this call reads the security
 * selection (version, group cipher, pairwise cipher, AKM), because the driver's
 * connect op takes those as separate fields and refuses to guess.  A NULL/empty
 * `ie` is an open network.  Returns the driver's own connect result. */
int	lkpicfg_connect(struct wiphy *wiphy, const u8 *ssid, size_t ssid_len,
	    const u8 *bssid, const u8 *ie, size_t ie_len);

/* Leave the network (the driver's deauthenticate path). */
int	lkpicfg_disconnect(struct wiphy *wiphy, u16 reason);

/* Install a key.  `pairwise` distinguishes the PTK (with the peer address in
 * `mac_addr`) from the GTK (`mac_addr` NULL); `seq` is the transmit packet
 * number as carried, and a NULL/none is accepted.  The cipher is CCMP - the one
 * this port's supplicant and this driver agree on (the driver's add_key takes
 * WLAN_CIPHER_SUITE_CCMP or nothing) - and an empty key is refused here rather
 * than handed on (the driver would mark it installed and program stale
 * material). */
int	lkpicfg_set_key(struct wiphy *wiphy, unsigned int key_idx, int pairwise,
	    const u8 *mac_addr, const u8 *key, size_t key_len,
	    const u8 *seq, size_t seq_len);

/* The network interface the driver registered, for a consumer that wants to
 * name it (NULL before registration). */
struct net_device *lkpicfg_netdev(struct wiphy *wiphy);

#endif /* LINUXKPI_CFG80211_H */
