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

#endif /* LINUX80211_NL80211_PROBE_H */
