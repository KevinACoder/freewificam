#ifndef _LINUXKPI_NET_IEEE80211_RADIOTAP_H_
#define _LINUXKPI_NET_IEEE80211_RADIOTAP_H_
/* [S6] B8: include-only stub (osdep funnel; monitor mode is not in the
 * W1-W5 slice) - the minimal header shape so the include resolves. */
struct ieee80211_radiotap_header {
	unsigned char	it_version;
	unsigned char	it_pad;
	unsigned short	it_len;
	unsigned int	it_present;
} __packed;

#define	IEEE80211_RADIOTAP_TSFT		0
#define	IEEE80211_RADIOTAP_FLAGS	1
#define	IEEE80211_RADIOTAP_RATE		2
#define	IEEE80211_RADIOTAP_CHANNEL	3
#define	IEEE80211_RADIOTAP_LOCK_QUALITY	5
#define	IEEE80211_RADIOTAP_RX_FLAGS	14
#endif
