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
