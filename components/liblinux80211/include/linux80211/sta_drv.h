/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   sta_drv.h
 * @brief  Plain-C seam between the imported supplicant's driver table and this
 *         port's cfg80211/nl80211 surface.
 *
 * The supplicant world and the Linux world disagree about names and types, so
 * the driver table only calls these functions; hostap_nl80211.c implements them
 * with the real wiphy/net_device and linuxkpi_nl80211_probe.c builds the
 * attribute streams.
 */
#ifndef LINUX80211_STA_DRV_H
#define LINUX80211_STA_DRV_H

#include <stddef.h>

struct lkpi_sta_connect {
	const char		*ssid;
	unsigned int		 ssid_len;
	const unsigned char	*bssid;		/* optional, 6 bytes */
	unsigned int		 freq_mhz;	/* 0 = any */
	const void		*ie;		/* RSN/WPA IE for the assoc request */
	unsigned int		 ie_len;
	int			 privacy;	/* NL80211_ATTR_PRIVACY */
};

struct lkpi_sta_bss {
	unsigned char	bssid[6];
	int		freq;
	int		signal;		/* dBm */
	int		beacon_int;
	int		capability;
};

int lkpi_sta_drv_scan(const char *ssid, unsigned int ssid_len,
		      unsigned int freq_mhz);
int lkpi_sta_drv_connect(const struct lkpi_sta_connect *c);
int lkpi_sta_drv_connected(unsigned char bssid[6]);
int lkpi_sta_drv_disconnect(unsigned int reason);
int lkpi_sta_drv_mac(unsigned char mac[6]);
int lkpi_sta_drv_eapol_tx(const void *frame, size_t len);

/* Copy the target BSS's IEs into `ie` (up to `ie_max`) and fill the metadata.
 * Returns 0 on success, -ENOENT when the scan has no such BSS, -EMSGSIZE when
 * the caller's IE buffer is too small. */
int lkpi_sta_drv_bss_get(const char *ssid, unsigned int ssid_len,
			 struct lkpi_sta_bss *out,
			 unsigned char *ie, unsigned int ie_max,
			 unsigned int *ie_len);

#endif /* LINUX80211_STA_DRV_H */
