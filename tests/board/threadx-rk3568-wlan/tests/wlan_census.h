/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_census.h
 * @brief   The WLAN image's census face: the snapshot calls the dataplane case
 *          and the console command share.
 * @date    19.10.2026
 */

#ifndef WLAN_CENSUS_H
#define WLAN_CENSUS_H

/* The census block: reset zeroes every instrumented layer's counters for a
 * fresh phase, print emits their `perf census:` lines.  The USB face's two
 * headline histograms partition the phase - `svc` is the time a transfer spent
 * armed (the device's share), `gap` the time the endpoint sat idle with work
 * queued behind it (the host's share) - and the sum of the two is the window
 * the transfer path was asked to cover. */
void	wlan_census_reset(void);
void	wlan_census_print(void);

#endif /* WLAN_CENSUS_H */
