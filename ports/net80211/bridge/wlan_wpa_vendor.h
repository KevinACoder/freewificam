/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_wpa_vendor.h
 * @brief   The bridge's wpa_supplicant backend: registration entry and the
 *          EAPOL counters, as scalars so any world may read them.
 * @date    09.10.2026
 */

#ifndef WLAN_WPA_VENDOR_H
#define WLAN_WPA_VENDOR_H

/* register the nine-operation backend with the supplicant port layer.
 * Returns 0 or a negative error. */
int	wlan_wpa_vendor_init(void);

/*
 * Bind the presentation layer's association steps: the backend owns the port's
 * single event slot and forwards ASSOC/DISASSOC here, so the netif's link
 * state follows the association in an image that also runs the supplicant.
 * Scalars only (the netif pointer is opaque to this world).
 */
void	wlan_wpa_netif_bind(void *netif, void (*on_assoc)(void *),
	    void (*on_disassoc)(void *));

/* (re-)take the port's single event slot for the backend's sink; call after
 * any later registration (the netif's own add registers one) */
void	wlan_wpa_vendor_events_arm(void);

/* EAPOL frames the port's receive seam routed to the supplicant, and frames
 * the seam rejected as malformed before the supplicant saw them. */
void	wlan_wpa_vendor_stats(unsigned int *eapol_rx, unsigned int *badframe);

#endif /* WLAN_WPA_VENDOR_H */
