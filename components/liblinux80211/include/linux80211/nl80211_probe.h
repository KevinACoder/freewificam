/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   nl80211_probe.h
 * @brief  The C4 bench's two calls into the real cfg80211/nl80211 path.
 *
 * The trigger injection is the bench seam: there is no netlink socket in this
 * image (ADR 2026-11-10-nl80211-scope, D1), so the case hands the helper a
 * *message it built with the real nla_put* byte stream, and the helper runs
 * nl80211's own pre_doit/doit/post_doit.  The scan, the driver command and the
 * BSS table are the stack's; only the socket/portid is absent.
 */
#ifndef LINUX80211_NL80211_PROBE_H
#define LINUX80211_NL80211_PROBE_H

#include <linux/types.h>

struct net_device;
struct wiphy;

/* Trigger NL80211_CMD_TRIGGER_SCAN for one SSID on one channel (MHz).
 * Returns the handler's return code (0 = scan started). */
int lkpi_nl80211_trigger_scan(struct net_device *dev, const char *ssid,
			      unsigned int freq_mhz);

/* Look up a BSS by SSID in the wiphy's scan results.  Returns 0 and fills the
 * outputs, or -ENOENT when the scan has not produced that BSS yet. */
int lkpi_nl80211_bss_find(struct wiphy *wiphy, const char *ssid,
			  unsigned char bssid[6], int *freq, int *signal);

/* Start NL80211_CMD_CONNECT (open AP) through the real handler.  Returns the
 * handler's code; completion is observed with lkpi_nl80211_connected(). */
int lkpi_nl80211_connect(struct net_device *dev, const char *ssid,
			 const unsigned char bssid[6], unsigned int freq_mhz);

/* Start NL80211_CMD_DISCONNECT through the real handler. */
int lkpi_nl80211_disconnect(struct net_device *dev, unsigned int reason);

/* 1 when the station is connected, 0 when not, negative on a bad argument.
 * Fills bssid when connected and bssid is non-NULL. */
int lkpi_nl80211_connected(struct net_device *dev, unsigned char bssid[6]);

/* --- the AP round (2026-11-11, docs/HANDOFF-AP-MODE.md) -------------------
 *
 * The four calls the AP case needs beyond scan/connect.  The type change goes
 * through NL80211_CMD_SET_INTERFACE rather than DEL_INTERFACE + NEW_INTERFACE:
 * with the device down mac80211's ieee80211_if_change_type() tears the sdata
 * down and sets it up as the new type without a driver op (the same path
 * `iw dev wlan0 set type ap` takes), so the default STA interface becomes the
 * AP under the same name and ifindex - and the lwIP netif keeps its "wlan0"
 * bind.
 */

/* Change the interface's nl80211 type (NL80211_IFTYPE_*).  Returns the
 * handler's code; the device must be down. */
int lkpi_nl80211_set_interface(struct net_device *dev, unsigned int type);

/* Start NL80211_CMD_START_AP.  `head` is the 36-byte 802.11 beacon head
 * (header + timestamp/interval/capability); `tail` is the IE list after the
 * TIM, which mac80211 inserts itself.  `width` is NL80211_CHAN_WIDTH_*.
 * Returns the handler's code. */
int lkpi_nl80211_start_ap(struct net_device *dev, const char *ssid,
			  unsigned int freq_mhz, unsigned int width,
			  unsigned int beacon_interval, unsigned int dtim,
			  const unsigned char *head, unsigned int head_len,
			  const unsigned char *tail, unsigned int tail_len);

/* Stop NL80211_CMD_STOP_AP. */
int lkpi_nl80211_stop_ap(struct net_device *dev);

/* The AP's station table, through mac80211's atomic iterator (no driver op).
 * Copies up to `max` MACs and sets *count to the total the iterator saw. */
int lkpi_nl80211_sta_count(struct net_device *dev,
			   unsigned char macs[][6], unsigned int max,
			   unsigned int *count);

/* The interface's nl80211 type (NL80211_IFTYPE_*), or a negative errno. */
int lkpi_nl80211_iftype(struct net_device *dev);

#endif /* LINUX80211_NL80211_PROBE_H */
