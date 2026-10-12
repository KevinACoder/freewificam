/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan8189_wpa_obs.h
 * @brief   The supplicant message observer's face (see wlan8189_wpa_obs.c).
 * @date    12.10.2026
 */

#ifndef WLAN8189_WPA_OBS_H
#define WLAN8189_WPA_OBS_H

/* register on the supplicant's message callback (once, before any connect) */
void	wlan8189_wpa_obs_init(void);

/* latched anchors */
int	wlan8189_wpa_obs_negotiation_completed(void);
int	wlan8189_wpa_obs_connected(void);
int	wlan8189_wpa_obs_assoc_seen(void);

/* the BSSID string from the CONNECTED message, "?" when absent */
const char *wlan8189_wpa_obs_bssid_txt(void);

#endif /* WLAN8189_WPA_OBS_H */
