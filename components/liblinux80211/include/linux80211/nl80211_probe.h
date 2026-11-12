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
struct lkpi_ap_sta_add;

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

/* The WPA2 form: also carries the supplicant's RSN/WPA IE and, when privacy
 * is non-zero, NL80211_ATTR_PRIVACY.  Without those attrs cfg80211 would look
 * for an open BSS and mac80211 would send an association request without the
 * RSN IE.  `ssid_len` is used so a non-NUL SSID byte array is safe. */
int lkpi_nl80211_connect_ex(struct net_device *dev, const char *ssid,
			    unsigned int ssid_len,
			    const unsigned char bssid[6],
			    unsigned int freq_mhz, const void *ie,
			    unsigned int ie_len, int privacy);

/* Copy one BSS's IEs and metadata for the supplicant's scan-result list.
 * Returns 0, -ENOENT, or -EMSGSIZE when `ie_max` is too small. */
int lkpi_nl80211_bss_get(struct wiphy *wiphy, const char *ssid,
			 unsigned int ssid_len, unsigned char bssid[6],
			 int *freq, int *signal, int *beacon_int,
			 int *capability, unsigned char *ie,
			 unsigned int ie_max, unsigned int *ie_len);

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
			  const unsigned char *tail, unsigned int tail_len,
			  int privacy);

/* --- the WPA2 key path (2026-11-12, docs/HANDOFF-AP-WPA2.md) -------------
 *
 * hostapd's set_key() arrives here: NL80211_CMD_NEW_KEY for the key itself,
 * a following NL80211_CMD_SET_KEY when the key is to become the default
 * (the AP's GTK), and NL80211_CMD_DEL_KEY when wpa_auth removes one.  The
 * commands run through the same real handler chain as everything above.
 */
struct lkpi_nl80211_key {
	unsigned int		cipher;		/* WLAN_CIPHER_SUITE_* */
	int			key_idx;
	const unsigned char	*mac;		/* NULL for a group key */
	const void		*key;		/* NULL only for delete */
	unsigned int		key_len;
	const void		*seq;		/* optional */
	unsigned int		seq_len;
};

int lkpi_nl80211_new_key(struct net_device *dev,
			 const struct lkpi_nl80211_key *key);
int lkpi_nl80211_set_default_key(struct net_device *dev, int key_idx,
				 int multicast);
int lkpi_nl80211_del_key(struct net_device *dev, int key_idx,
			 const unsigned char *mac);

/* Stop NL80211_CMD_STOP_AP. */
int lkpi_nl80211_stop_ap(struct net_device *dev);

/* The AP's station table, through mac80211's atomic iterator (no driver op).
 * Copies up to `max` MACs and sets *count to the total the iterator saw. */
int lkpi_nl80211_sta_count(struct net_device *dev,
			   unsigned char macs[][6], unsigned int max,
			   unsigned int *count);

/* The interface's nl80211 type (NL80211_IFTYPE_*), or a negative errno. */
int lkpi_nl80211_iftype(struct net_device *dev);

/* The frequency the interface is operating on right now, in MHz: the AP's
 * chandef for an AP interface, the connected BSS's channel for a station.
 * 0 when the interface is down/idle, a negative errno on bad input.  The STA
 * 5 GHz gate needs this: `connected` and `state=COMPLETED` say nothing about
 * which band the association landed on, and a 5 GHz attempt that silently
 * fell back to 2.4 would otherwise read as a pass. */
int lkpi_nl80211_link_freq(struct net_device *dev);

/* Where an association stopped: bit0 the target BSS is in the scan table,
 * bit1 authenticated (mac80211's current_bss), bit2 cfg80211-connected.  The
 * connect seam answers synchronously, so the intermediate steps are what says
 * which stage of cfg80211's SME stalled. */
int lkpi_nl80211_assoc_progress(struct net_device *dev, const char *ssid,
				unsigned int ssid_len);	/* ssid may be NULL */

/* --- the AP MLME's three actions (2026-11-12) ----------------------------
 *
 * hostapd's side of the AP: send one management frame, create the client, open
 * its controlled port.  They are the same real handler chain as the calls
 * above; what is absent is the netlink socket (ADR 2026-10-11 D1).
 */

/* Send one complete 802.11 management frame (NL80211_CMD_FRAME) on the AP's
 * current channel.  `no_cck` sets NL80211_ATTR_TX_NO_CCK_RATE. */
int lkpi_nl80211_mgmt_tx(struct net_device *dev, const void *frame,
			 unsigned int len, int no_cck);

/* NL80211_CMD_NEW_STATION with the association request's parameters. */
int lkpi_nl80211_new_station(struct net_device *dev,
			     const struct lkpi_ap_sta_add *sta);

/* NL80211_CMD_SET_STATION with the AUTHORIZED flag (the controlled port). */
int lkpi_nl80211_sta_authorized(struct net_device *dev,
				const unsigned char mac[6]);

/* NL80211_CMD_SET_STATION with AUTHORIZED set or cleared: hostapd opens the
 * port after association and closes it on deauth (open and WPA2 alike). */
int lkpi_nl80211_sta_set_authorized(struct net_device *dev,
				    const unsigned char mac[6], int authorized);

/* NL80211_CMD_DEL_STATION (hostapd's sta_remove / deauth). */
int lkpi_nl80211_del_station(struct net_device *dev,
			     const unsigned char mac[6]);

/*
 * The assoc request's parameters for NL80211_CMD_NEW_STATION.  The pointers
 * point into the request frame and are valid only for the call.
 */
struct lkpi_ap_sta_add {
	unsigned char mac[6];
	unsigned short aid;
	unsigned short listen_interval;
	unsigned short capab;
	const unsigned char *rates;	/* IE 1 followed by IE 50, raw */
	unsigned int rates_len;
	const unsigned char *ht_capa;	/* IE 45 body (26 B) or NULL */
	const unsigned char *wmm;	/* non-NULL when the station advertised WMM */
};

#endif /* LINUX80211_NL80211_PROBE_H */
