#ifndef _LINUXKPI_LINUX_IEEE80211_H_
#define _LINUXKPI_LINUX_IEEE80211_H_
/* [S6] B8: include-only stub - the vendor 802.11 shapes come from its own
 * include/ieee80211.h; the kernel funnel (osdep >= 2.6.24) must resolve
 * with zero symbols consumed by the compile set. */

/* frame control bits (only what a compiled unit might touch) */
#define	IEEE80211_FCTL_VERS		0x0003
#define	IEEE80211_FCTL_FTYPE		0x000c
#define	IEEE80211_FCTL_STYPE		0x00f0
#define	IEEE80211_FCTL_TODS		0x0100
#define	IEEE80211_FCTL_FROMDS		0x0200
#define	IEEE80211_FCTL_PROTECTED	0x4000

#define	IEEE80211_FTYPE_MGMT		0x0000
#define	IEEE80211_FTYPE_CTL		0x0004
#define	IEEE80211_FTYPE_DATA		0x0008

#define	IEEE80211_STYPE_ASSOC_REQ	0x0000
#define	IEEE80211_STYPE_ASSOC_RESP	0x0010
#define	IEEE80211_STYPE_REASSOC_REQ	0x0020
#define	IEEE80211_STYPE_REASSOC_RESP	0x0030
#define	IEEE80211_STYPE_PROBE_REQ	0x0040
#define	IEEE80211_STYPE_PROBE_RESP	0x0050
#define	IEEE80211_STYPE_BEACON		0x0080
#define	IEEE80211_STYPE_AUTH		0x00b0
#define	IEEE80211_STYPE_DEAUTH		0x00c0
#define	IEEE80211_STYPE_ACTION		0x00d0

#define	WLAN_EID_SSID			0
#define	WLAN_EID_SUPP_RATES		1
#define	WLAN_EID_DS_PARAMS		3
#define	WLAN_EID_TIM			5
#define	WLAN_EID_COUNTRY		7
#define	WLAN_EID_EXT_SUPP_RATES		50
#define	WLAN_EID_VENDOR_SPECIFIC	221

#define	WLAN_CAP_ESS			0x0001
#define	WLAN_CAP_IBSS			0x0002
#define	WLAN_CAP_PRIVACY		0x0010

#define	WLAN_PMKID_LEN			16

/* [S5b-W1] rtl8851bu nl80211 band/channel face (os_dep/linux/os_ch_utils.h
 * compiles the regulatory channel tables against the kernel shapes at
 * LINUX_VERSION_CODE >= 4.7): enum nl80211_band + struct ieee80211_channel
 * with the fields the CHAN2G/CHAN5G/CHAN6G initializers set. */
enum nl80211_band {
	NL80211_BAND_2GHZ,
	NL80211_BAND_5GHZ,
	NL80211_BAND_60GHZ,
	NL80211_BAND_6GHZ,
};

#define	NUM_NL80211_BANDS		4

struct ieee80211_channel {
	enum nl80211_band	band;
	u16			center_freq;
	u16			hw_value;
	u32			flags;
	int			max_antenna_gain;
	int			max_power;
	int			max_reg_power;
};

/* [S5b-W1] the 802.11 header face: the hostap-lineage crypto units
 * (core/crypto/*.c - ccmp/gcmp decrypt) read frame_control/addr1-3/
 * seq_ctrl off the kernel shape. */
#include <linux/types.h>

struct ieee80211_hdr {
	__le16			frame_control;
	__le16			duration_id;
	u8			addr1[6];
	u8			addr2[6];
	u8			addr3[6];
	__le16			seq_ctrl;
	u8			addr4[6];
} __attribute__((packed));
#endif
