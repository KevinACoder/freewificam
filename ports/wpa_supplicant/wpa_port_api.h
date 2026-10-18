/*
 * @file    wpa_port_api.h
 * @brief   Public control API of the wpa_supplicant port (callable from any
 *          thread).
 *
 * @design facade over the supplicant's control face (DESIGN.md section 1,
 *         "control" row): the caller's request is marshalled to the
 *         supplicant thread as a job, so the supplicant's structures are only
 *         ever touched on that thread. The chip bring-up routes through the
 *         registered vendor backend instead of a compile-time driver symbol.
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

/* Whether the last console request asked for an open network (no PSK).  The
 * join case uses it to accept an association that legitimately has no 4-way
 * handshake; it never turns a keyed request into an open one. */
int wpa_port_open_configured(void);

/* bring the wlan interface up on the calling thread (never the
 * supplicant thread: the chip init can block on the USB workers).
 * With no vendor backend registered this is a no-op success, which
 * keeps a backend-less image alive. */
int wpa_port_ensure_up(void);

/* bounded millisecond sleep on the CMSIS tick. The argument is
 * milliseconds; the osDelay unit is ticks - the conversion is not
 * optional even at a 1000 Hz tick where it is the identity. */
void wpa_msleep(unsigned int ms);

#endif /* WPA_PORT_API_H_ */
