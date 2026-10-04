#ifndef _LINUXKPI_NET_CFG80211_H_
#define _LINUXKPI_NET_CFG80211_H_
/* [S5b-W1] rtl8851bu cfg80211 stub face - STRUCTURAL, not optional: the
 * vendor core hard-depends on cfg80211 shapes with no CONFIG_IOCTL_CFG80211
 * gate (drv_types.h wdev_data member + adapter_wdev_data() consumers in
 * rtw_scan/rtw_p2p/rtw_mlme_ext/rtw_mi - the W0-B Linux-lane fact, verified
 * for the RTOS compile set).  ioctl_cfg80211.c itself stays out of the
 * compile set; this header only keeps the types parseable.  Functions the
 * vendor calls from ungated code get first-party no-op definitions in the
 * adapter seam tier (wlan8851_seam.c lineage), not here. */

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/ieee80211.h>

/* wiphy: allocation-free shell - the RTOS build never registers one; the
 * wiphy_to_* macros in the vendor's ioctl_cfg80211.h cast the priv slot
 * straight to their own data. */
struct wiphy {
	void	*priv;
	char	name[16];
};

#define	wiphy_priv(wiphy)	((wiphy)->priv)
#define	wiphy_name(wiphy)	((wiphy)->name)

/* the wireless_dev shell: the adapter carries one pointer per iface; no
 * RTOS consumer dereferences beyond the vendor's own wrappers. */
struct wireless_dev {
	struct wiphy		*wiphy;
	struct net_device	*netdev;
};

/* scan/connect requests: pointer slots in the vendor's rtw_wdev_priv; the
 * RTOS build never issues one through the cfg80211 face. */
struct cfg80211_scan_request {
	struct wireless_dev	*wdev;
};

struct cfg80211_connect_params {
	u8	*bssid;
	u8	*ssid;
	size_t	ssid_len;
};

/* kernel-shaped ssid face (rtw_wdev_invit_info embeds one by value) */
struct cfg80211_ssid {
	u8	ssid[32];	/* IEEE80211_MAX_SSID_LEN */
	size_t	ssid_len;
};

/* chan-def face (P2P du_wdev slot); the channel/band primitives live in
 * linux/ieee80211.h ([S5b-W1] nl80211 face) */
enum nl80211_chan_width {
	NL80211_CHAN_WIDTH_20_NOHT,
	NL80211_CHAN_WIDTH_20,
	NL80211_CHAN_WIDTH_40,
	NL80211_CHAN_WIDTH_80,
	NL80211_CHAN_WIDTH_160,
};

enum nl80211_channel_type {
	NL80211_CHAN_NO_HT,
	NL80211_CHAN_HT20,
	NL80211_CHAN_HT40PLUS,
	NL80211_CHAN_HT40MINUS,
};

struct cfg80211_chan_def {
	struct ieee80211_channel *chan;
	enum nl80211_chan_width width;
	u32	center_freq1;
	u32	center_freq2;
};

/* FT roam event face (rtw_ft.h embeds one by value; core/rtw_ft.c fills
 * ies/target_ap/ric_ies) */
struct cfg80211_ft_event_params {
	const u8	*ies;
	size_t		ies_len;
	const u8	*target_ap;
	const u8	*ric_ies;
	size_t		ric_ies_len;
};

struct cfg80211_update_ft_ies_params {
	u16		mdie;
	size_t		ie_len;
	const u8	*ie;
};

/* AP/CSA faces: the AP-mode faces compile-dormant ([S5a] AP/P2P accounting);
 * rtw_ap.c/rtw_csa.c read the beacon tail/head pieces. */
struct cfg80211_beacon {
	const u8	*head;
	size_t		head_len;
	const u8	*tail;
	size_t		tail_len;
	const u8	*beacon_ies;		/* kernel: beacon_ies/beacon_ies_len */
	size_t		beacon_ies_len;
	const u8	*proberesp_ies;
	size_t		proberesp_ies_len;
	const u8	*assocresp_ies;
	size_t		assocresp_ies_len;
};

struct cfg80211_ap_settings {
	struct cfg80211_beacon	beacon;
};

/* [S5b-W1] rfkill face (core/rtw_phl.c power-mgnt path): no radio-state
 * consumer in this image */
static inline void wiphy_rfkill_set_hw_state(struct wiphy *w, bool blocked)
{
	(void)w;
	(void)blocked;
}

struct cfg80211_csa_settings {
	struct cfg80211_chan_def chandef;
	struct cfg80211_beacon beacon_csa;
	u8	count;
};

#endif /* _LINUXKPI_NET_CFG80211_H_ */
