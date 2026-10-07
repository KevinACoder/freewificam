/*
 * @file    seam_stub.h
 * @brief   The stub driver's face for the smoke main: run it, read its counts.
 */

#ifndef NET80211_PORT_SEAM_STUB_H_
#define NET80211_PORT_SEAM_STUB_H_

/* Drive the net80211-side asks through the seam vtable. */
void wlan_seam_stub_run(void);

/* Callbacks served since start. */
unsigned wlan_seam_stub_calls(void);

/* Length of the last frame handed to the transmit callback. */
unsigned wlan_seam_stub_tx_len(void);

#endif /* NET80211_PORT_SEAM_STUB_H_ */
