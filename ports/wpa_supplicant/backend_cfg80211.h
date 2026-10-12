/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    backend_cfg80211.h
 * @brief   The cfg80211-backed wpa_supplicant backend's face: registration and
 *          the counters the board cases read.
 *
 * @design adapter / anticorruption layer, driver-side instance (the contract
 *         header states the rule: no hostap type crosses it; this side owns the
 *         translation to the Linux-compatibility face). The shape follows the
 *         instance whose board runs proved it
 *         (ports/net80211/bridge/wlan_wpa_vendor.h:1-30).
 * @date    12.10.2026
 */

#ifndef BACKEND_CFG80211_H
#define BACKEND_CFG80211_H

#include <stdint.h>

/* Register the backend with the supplicant's driver slot.  Called by the image's
 * start-up before the supplicant runs. */
int	lkpi_wpa_cfg80211_init(void);

/* Bind the presentation layer's link steps: from here the driver's connect and
 * disconnect notifications forward to them, so the lwIP interface's link state
 * follows the association.  All scalars - the netif world passes its own pointer
 * and two step functions (wlan_wpa_netif_bind's shape). */
void	lkpi_wpa_cfg80211_netif_bind(void *netif, void (*on_up)(void *),
	    void (*on_down)(void *));

/* (Re-)register the backend as the substitute's consumer.  ensure_up calls this,
 * and so does a presentation layer that registered its own consumer afterwards;
 * the rule is that the backend owns the slot and forwards, so one call puts the
 * chain back in that shape (wlan_wpa_vendor_events_arm's shape). */
void	lkpi_wpa_cfg80211_events_arm(void);

/* Diagnostics for the console and the board runs. */
void	lkpi_wpa_cfg80211_stats(unsigned int *eapol_rx, unsigned int *badframe,
	    unsigned int *bss_seen);

#endif /* BACKEND_CFG80211_H */
