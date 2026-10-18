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

/* just the skb pool's occupancy line, so a measurement window can sample the
 * pool while it runs (a wedge inside a phase otherwise leaves no pool state) */
void	wlan8189_census_pool_line(void);

/* The stall watchdog: start it once at the top of the dataplane case, then bump
 * the heartbeat on every window step. When neither the heartbeat nor any
 * data-path counter has moved for three seconds it prints one state line (which
 * counters advanced and which did not), so an intermittent freeze names its own
 * side instead of leaving a transcript that just stops. */
void	wlan8189_census_watchdog_start(void);
void	wlan8189_census_hb(void);
unsigned long	wlan8189_census_stall_count(void);

/*
 * The receive-stall episode recorder, driven by the same thread. Arm it at the
 * start of a traffic window (the phase and its aggregate readback travel with
 * it) and disarm it at the end. A second without data-path progress opens an
 * episode: `rxstall begin` prints the three layers (what the card offered, what
 * the host read, what the stack saw), `hold` repeats every two seconds and
 * `end` closes it. `progress` excludes the pump's poll count and the sheet
 * heartbeat, so a pump spinning over an empty card is a stall, not progress.
 */
void	wlan8189_census_stall_arm(int on, unsigned int phase, int treatment,
	    unsigned int agg);
unsigned long	wlan8189_census_rxstall_count(void);
unsigned long	wlan8189_census_rxstall_ms_total(void);
unsigned long	wlan8189_census_rxstall_ms_max(void);

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
/* one direct 16-bit write of the whole register, the driver's own shape
 * (HalRxAggr8188FSdio): no read, no read-modify-write. The B/C arms use it to
 * separate the RMW read and the two-write sequence from the write event. */
void	wlan8189_agg_write(unsigned int value);
unsigned int	wlan8189_agg_get(void);

/* the host controller's completion-poll step, in microseconds (0 leaves it
 * alone); show prints the step in effect. The data step is the same knob
 * restricted to the data-stage waits (see dwc_mmc.h) and is the round's lever:
 * the sheet sets it per phase and the phase line reports it back. */
void	wlan8189_dwc_show(void);
unsigned int	wlan8189_dwc_poll_set(unsigned int us);
unsigned int	wlan8189_dwc_data_set(unsigned int us);

/* the SDIO pump's cadence policy: "fixed" holds the idle period between cycles,
 * "adaptive" services a busy card back-to-back and backs off when it is idle;
 * show prints the effective policy and the controller's current period */
void	wlan8189_pump_show(void);
int	wlan8189_pump_set(const char *policy);
const char *wlan8189_pump_policy_get(void);

/* the pump's fairness yield (see lkpi_sdio.h): when on, a pump cycle stands
 * aside for a driver thread already parked on the bus claim */
void	wlan8189_pump_yield_set(int on);
int	wlan8189_pump_yield_get(void);

/* the wake-chain knobs (the align round's second arm): prio moves the pump
 * into or out of the driver threads' normal band; drain keeps a cycle
 * dispatching while the card still reports pending work */
int	wlan8189_pump_prio_set(int normal);
int	wlan8189_pump_prio_get(void);
int	wlan8189_pump_drain_set(int on);
int	wlan8189_pump_drain_get(void);

/* the card-interrupt mode (the align round's lever): on means the card's own
 * DAT1 events wake the pump and the period is a backstop; off is the polling
 * cadence. set returns the effective state, or a negative seam error when the
 * backend carries no card-interrupt path; the notify/wake counters print in
 * the census (D pumpirq) and the controller's entry counters in A dwc_cardirq */
int	wlan8189_irq_set(int on);
int	wlan8189_irq_get(void);
void	wlan8189_irq_show(void);

/* the console sink's line accounting (counted in main.c, read by the census) */
void	wlan8189_census_log_get(unsigned long *in_lines, unsigned long *in_bytes,
	    unsigned long *out_lines, unsigned long *out_bytes);

/* The phase plan: one runtime-set single-variable comparison the four-phase
 * sheet runs instead of its compiled-in lever. The sheet's phase routine
 * re-asserts the full baseline every phase, so a bare runtime command would be
 * overwritten before the measurement window - the plan is applied by the same
 * routine, at the same boundary, and read back in the same phase line. A boot
 * without a plan behaves exactly as the compiled lever says; the plan exists so
 * one boot can sweep a knob the compile-time levers never carried.
 *
 * key: plan_agg_timeout / plan_agg_len / plan_poll_us / plan_data_us /
 *      plan_pump (0 = fixed, 1 = adaptive) / plan_yield / plan_prio.
 * set(key, base, treat): the base value is what the control phases run, the
 * treatment value what the treatment phases run (the sheet picks which phases
 * those are, exactly as for a compiled lever). Returns 0 on acceptance. */
#define	WLAN8189_PLAN_AGG_TIMEOUT	1
#define	WLAN8189_PLAN_AGG_LEN		2
#define	WLAN8189_PLAN_POLL_US		3
#define	WLAN8189_PLAN_DATA_US		4
#define	WLAN8189_PLAN_PUMP		5
#define	WLAN8189_PLAN_YIELD		6
#define	WLAN8189_PLAN_PRIO		7
int	wlan8189_plan_set(int key, unsigned int base, unsigned int treat);
void	wlan8189_plan_clear(void);
/* the per-phase application: treatment != 0 applies the plan's treatment
 * value, otherwise its base. Returns the plan key actually applied, or 0 when
 * no plan is set (the caller then uses its compiled lever). */
int	wlan8189_plan_apply(int treatment);

#endif /* WLAN8189_CENSUS_H */
