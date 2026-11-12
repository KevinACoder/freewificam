/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    hostap_drv.h
 * @brief   The seam between the imported hostap world and this port's Linux
 *          world: plain C types only, so one translation unit can hold the
 *          hostapd driver_ops table and another the nl80211 calls.
 *
 * The imported hostapd tree (third-party/wpa_supplicant-rtos) has its own namespace
 * (struct ieee80211_hdr, WLAN_EID_*, hostapd structs).  The port's Linux face
 * (net/cfg80211.h, net/mac80211.h) defines the same names differently, so the
 * two must never meet in one translation unit.  This header is the only
 * contact: the numbers below mirror hostapd's own enums (driver.h) and are
 * passed as plain ints, and the buffers are (pointer, length) pairs.
 */
#ifndef LINUX80211_HOSTAP_DRV_H
#define LINUX80211_HOSTAP_DRV_H

#include <stddef.h>

/* mirrors enum hostapd_hw_mode (src/common/defs.h, this hostap pin):
 * 11B=0, 11G=1, 11A=2, 11AD=3, ANY=4.  The G value is 1, not 2 - the older
 * order (A before G) is a different hostap generation and does not apply. */
#define LKPI_HOSTAP_MODE_11B	0
#define LKPI_HOSTAP_MODE_11G	1
#define LKPI_HOSTAP_MODE_11A	2

/* mirrors enum hostapd_chan_width_attr */
#define LKPI_HOSTAP_CHAN_WIDTH_20	(1u << 1)
#define LKPI_HOSTAP_CHAN_WIDTH_40P	(1u << 2)
#define LKPI_HOSTAP_CHAN_WIDTH_40M	(1u << 3)

/* RSN cipher suite: CCMP-128 (the only cipher this round installs) */
#define LKPI_HOSTAP_CIPHER_CCMP		0x000fac04u

/* mirrors HOSTAPD_CHAN_* flags */
#define LKPI_HOSTAP_CHAN_DISABLED	0x00000001
#define LKPI_HOSTAP_CHAN_NO_IR		0x00000002
#define LKPI_HOSTAP_CHAN_RADAR		0x00000008

/* 40 = the 5 GHz band's 25 channels plus the 2.4 GHz band's 14, with room to
 * spare: the two modes share this one fixed array and hostapd walks both. */
#define LKPI_HOSTAP_MAX_CHAN	40
#define LKPI_HOSTAP_MAX_RATES	16

struct lkpi_hostap_chan {
	int		chan;
	int		freq;
	int		flags;
	unsigned int	allowed_bw;
};

struct lkpi_hostap_mode {
	int			mode;
	int			num_channels;
	struct lkpi_hostap_chan	channels[LKPI_HOSTAP_MAX_CHAN];
	int			num_rates;
	int			rates[LKPI_HOSTAP_MAX_RATES];
	unsigned short		ht_capab;
	unsigned char		mcs_set[16];
	unsigned char		a_mpdu_params;
	int			ht_supported;
};

struct lkpi_hostap_ap_params {
	const void	*head;
	size_t		 head_len;
	const void	*tail;
	size_t		 tail_len;
	const void	*ssid;
	size_t		 ssid_len;
	int		 freq;
	int		 bw_mhz;		/* hostapd's bandwidth unit: 20/40/80/160 */
	int		 beacon_int;
	int		 dtim;
	int		 privacy;		/* WPA2: the beacon's privacy bit */
};

/* hostapd's set_key(), reduced to what this port maps onto nl80211.  The
 * cipher is the RSN cipher-suite value (0x000fac04 for CCMP); addr is NULL
 * for a group key and the station's address for a pairwise key. */
struct lkpi_hostap_key {
	const unsigned char	*addr;		/* NULL or 6 bytes */
	int			 key_idx;
	unsigned int		 cipher;
	const void		*key;
	size_t			 key_len;
	const void		*seq;		/* optional */
	size_t			 seq_len;
	int			 pairwise;
	int			 default_key;	/* the AP's GTK becomes default */
};

struct lkpi_hostap_sta_add {
	const unsigned char	*addr;		/* 6 bytes */
	unsigned short		 aid;
	unsigned short		 capability;
	int			 listen_interval;
	const void		*rates;		/* 802.11 rates IE bytes */
	size_t			 rates_len;
	const void		*ht_capa;	/* 26 bytes, or NULL */
	size_t			 ht_capa_len;
	int			 wmm;
};

/* bind the driver to the interface ("wlan0"); 0 or a negative errno */
int lkpi_hostap_drv_open(const char *ifname);
void lkpi_hostap_drv_close(void);

/* the interface's own address (6 bytes), for hostapd's own_addr */
const unsigned char *lkpi_hostap_drv_mac(void);

/* the wiphy's 2.4 GHz band, translated; returns the number of modes filled */
int lkpi_hostap_drv_modes(struct lkpi_hostap_mode *modes, int max_modes);

/* cfg80211's own frequency -> channel number, exposed because the hostap side
 * of the seam has no Linux header to reach it through (a channel number is a
 * plain int, so this stays on the seam contract). */
int lkpi_hostap_drv_chan_of(unsigned int freq_mhz);

int lkpi_hostap_drv_set_ap(const struct lkpi_hostap_ap_params *params);
int lkpi_hostap_drv_send_mlme(const void *frame, size_t len, int noack);
int lkpi_hostap_drv_sta_add(const struct lkpi_hostap_sta_add *sta);
int lkpi_hostap_drv_sta_del(const unsigned char *addr);
int lkpi_hostap_drv_sta_authorized(const unsigned char *addr, int authorized);

/* one complete EAPOL frame (Ethernet header included) on its way to the air */
int lkpi_hostap_drv_eapol_tx(const void *frame, size_t len);

int lkpi_hostap_drv_set_key(const struct lkpi_hostap_key *key);
int lkpi_hostap_drv_del_key(const unsigned char *addr, int key_idx);

#endif /* LINUX80211_HOSTAP_DRV_H */
