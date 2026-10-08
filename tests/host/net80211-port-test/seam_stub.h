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

/* The scan-group ordering the seam contract states: the receive filter's
 * promiscuous bit must be programmed before the sweep begins and cleared once
 * it is over. Both read back what the vtable actually received. */
int wlan_seam_stub_filter_at_scan_start(void);
int wlan_seam_stub_filter_cleared_after_scan(void);

#endif /* NET80211_PORT_SEAM_STUB_H_ */
