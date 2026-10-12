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

#endif /* WLAN8189_CASES_H */
