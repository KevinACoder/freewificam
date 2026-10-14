/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan8189_census.h
 * @brief   The RTL8189FTV image's census face: the snapshot calls and the
 *          driver-world readouts (power policy, rate/BA state) the cases and
 *          the console command share.
 * @date    14.10.2026
 */

#ifndef WLAN8189_CENSUS_H
#define WLAN8189_CENSUS_H

/* the census block: reset zeroes every module's counters for a fresh phase,
 * print emits the `perf census:` lines of groups A-D plus the driver, rate and
 * power readouts */
void	wlan8189_census_reset(void);
void	wlan8189_census_print(void);

/* the power policy (the driver's own setters; mode 0 = PS_MODE_ACTIVE leaves
 * power save, 2 = PS_MODE_MAX is the built-in default) */
int	wlan8189_ps_get(void);
int	wlan8189_ps_set(int mode);
int	wlan8189_ps_ips_set(int mode);
void	wlan8189_ps_show(void);

/* the rate / bandwidth / BA readout (the driver's own station dump) */
void	wlan8189_rate_show(void);

/* the console sink's line accounting (counted in main.c, read by the census) */
void	wlan8189_census_log_get(unsigned long *in_lines, unsigned long *in_bytes,
	    unsigned long *out_lines, unsigned long *out_bytes);

#endif /* WLAN8189_CENSUS_H */
