/**
 * @file    wlan8189_cases.h
 * @brief   The case chains this suite's files share (declarations only).
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  the suite's case table (tests/board/common/board_suite.h) declares
 *          each case with BOARD_TEST in its own file; the console command file
 *          re-runs the same chains, so it needs their names. Same shape as the
 *          SDIO suite's tests/sdio_cases.h.
 */

#ifndef WLAN8189_CASES_H
#define WLAN8189_CASES_H

void wlan8189_probe_chain( void );
void wlan8189_scan_chain( void );
void wlan8189_join_chain( void );
void wlan8189_dataplane_chain( void );
void wlan8189_ap_chain( void );

/* The dataplane case's netif (opaque here: this header is also read by the
 * console file, which lives outside lwIP's world). The AP case runs after the
 * dataplane case in the same boot and puts the access point's static address on
 * the same interface. NULL before the dataplane case has assembled it. */
void *wlan8189_dataplane_netif( void );

/* One uplink window (the board as the iperf client toward `peer`), the same
 * machinery the dataplane sheet's uplink phases run: the AP case uses it to
 * measure the reverse direction while its peer is associated. */
int wlan8189_uplink_window( unsigned int peer, unsigned int phase );

/* The access point, for the console's manual re-run path: assemble the beacon
 * (the capture-derived template) and drive the driver's start/stop. Returns the
 * driver's own result. */
int wlan8189_ap_start( const char *ssid, unsigned int channel );
int wlan8189_ap_stop( void );

/*
 * The uplink stage's far end: the console command `wlan8189 uplink <a.b.c.d>`
 * stores the IPv4 address (host order: a<<24|b<<16|c<<8|d) of the peer the
 * board's own iperf client will send to, and the dataplane case's uplink stage
 * waits for it before it runs. A plain u32 rather than an lwIP address type,
 * so the console file stays out of lwIP's header world.
 */
void wlan8189_uplink_peer_set( unsigned int peer );
int wlan8189_uplink_peer_get( unsigned int *peer );

#endif /* WLAN8189_CASES_H */
