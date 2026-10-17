/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_wpa_obs.h
 * @brief   The supplicant message observer's face (see wlan_wpa_obs.c).
 * @date    09.10.2026
 */

#ifndef WLAN_WPA_OBS_H
#define WLAN_WPA_OBS_H

/* register on the supplicant's message callback (once, before any connect) */
void	wlan_wpa_obs_init(void);

/* clear the latched anchors without touching the callback registration: a
 * case that waits for a fresh connect must not inherit the previous one's */
void	wlan_wpa_obs_reset(void);

/* latched anchors */
int	wlan_wpa_obs_negotiation_completed(void);
int	wlan_wpa_obs_connected(void);
int	wlan_wpa_obs_assoc_seen(void);

/* the BSSID string from the CONNECTED message, "?" when absent */
const char *wlan_wpa_obs_bssid_txt(void);

#endif /* WLAN_WPA_OBS_H */
