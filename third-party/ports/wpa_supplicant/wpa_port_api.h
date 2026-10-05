/*
 * @file
 * @brief Public control API of the wpa_supplicant port (any thread).
 *
 * Ported from the prior project's wpa_port_api.h ([S7a] §3.3.1: no
 * rewrite); wlan_supp_ensure_up became wpa_port_ensure_up - the chip
 * bring-up now routes through the registered vendor backend instead
 * of a compile-time net80211 glue symbol ([S7a] FR-8).
 */

#ifndef WPA_PORT_API_H_
#define WPA_PORT_API_H_

/* spawn the supplicant thread; idempotent */
int wpa_port_start(void);
int wpa_port_started(void);

/* WPA2-PSK: the request is marshalled to the supplicant thread and
 * this returns once it was picked up (or timed out). */
int wpa_port_connect(const char *ssid, const char *psk);
/* same, with a locked BSSID (6 bytes); NULL bssid == unconstrained */
int wpa_port_connect_bssid(const char *ssid, const char *psk,
	const unsigned char *bssid);
int wpa_port_disconnect(void);
int wpa_port_status(void);

/* bring the wlan interface up on the calling thread (never the
 * supplicant thread: the chip init can block on the USB workers).
 * With no vendor backend registered this is a no-op success, which
 * keeps the smoke image alive. */
int wpa_port_ensure_up(void);

/* bounded millisecond sleep on the CMSIS tick. The argument is
 * milliseconds; the osDelay unit is ticks - the conversion is not
 * optional even at a 1000 Hz tick where it is the identity ([S7a]
 * §3.3.2). */
void wpa_msleep(unsigned int ms);

#endif /* WPA_PORT_API_H_ */
