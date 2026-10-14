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

/* the card's own receive account (REG_RXERR_RPT full-drop counter delta and the
 * raw HISR word, both read at phase boundaries) */
void	wlan8189_card_stats_show(void);

/* the card's RX aggregation policy, both bytes of REG_RXDMA_AGG_PG_TH: STOCK is
 * the pair the driver's init wrote (timeout 1, length 15 KB); LOW is the round's
 * smaller-length treatment; the timeout byte is the round's other lever. get
 * reads the register back so a phase can report what actually took effect */
#define	WLAN8189_AGG_STOCK_KB	15u
#define	WLAN8189_AGG_LOW_KB	4u
#define	WLAN8189_AGG_TIMEOUT_STOCK	1u
void	wlan8189_agg_set(unsigned int size_kb);
void	wlan8189_agg_timeout_set(unsigned int timeout_units);
unsigned int	wlan8189_agg_get(void);

/* the host controller's completion-poll step, in microseconds (0 leaves it
 * alone); show prints the step in effect */
void	wlan8189_dwc_show(void);
unsigned int	wlan8189_dwc_poll_set(unsigned int us);

/* the SDIO pump's cadence policy: "fixed" holds the idle period between cycles,
 * "adaptive" services a busy card back-to-back and backs off when it is idle;
 * show prints the effective policy and the controller's current period */
void	wlan8189_pump_show(void);
int	wlan8189_pump_set(const char *policy);

/* the pump's fairness yield (see lkpi_sdio.h): when on, a pump cycle stands
 * aside for a driver thread already parked on the bus claim */
void	wlan8189_pump_yield_set(int on);
int	wlan8189_pump_yield_get(void);

/* the console sink's line accounting (counted in main.c, read by the census) */
void	wlan8189_census_log_get(unsigned long *in_lines, unsigned long *in_bytes,
	    unsigned long *out_lines, unsigned long *out_bytes);

#endif /* WLAN8189_CENSUS_H */
