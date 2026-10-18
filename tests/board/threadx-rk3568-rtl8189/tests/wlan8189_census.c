/**
 * @file    wlan8189_census.c
 * @brief   The image's census face, driver world: the census clock's board-side
 *          implementation, the driver-counters/rate/power readouts, and the
 *          snapshot print the cases and the console command call.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  console command row + reuse of the driver's own readout paths.
 *          Two pieces live here rather than in a component:
 *          - the census clock (lkpi_census_now_ns): the timestamps come from
 *            the architectural counter, and the portable linuxkpi layers must
 *            not touch aarch64 registers, so the implementation sits with the
 *            image that defines CONFIG_LKPI_CENSUS;
 *          - the driver-world readouts: the vendored driver is a FullMAC stack
 *            compiled in whole, so its counters (receive/transmit drops,
 *            reorder events), its station record (the BA bitmap, the rate state
 *            through the driver's own dump) and its power-policy setters are
 *            read through the driver's own accessors - no patch to the
 *            vendored tree (its debug paths read the same fields the same way).
 *
 *          This translation unit is driver-world: it includes the vendored
 *          headers and never an lwIP one. The lwIP half of the census (the
 *          lwip_stats snapshot) prints from the dataplane case, which owns that
 *          header world.
 */

#include <stdint.h>

#include "generic_timer.h"
#include "log.h"

#include "drv_types.h"
#include "hal/hal_dm.h"

#include "dwc_mmc.h"
#include "lkpinetif_seam.h"
#include "lkpi_census.h"
#include "linuxkpi_sdio.h"

#include "os/cmsis2/cmsis2.h"

#include "wlan8189_census.h"

/* the substitute's device lookup, reached by name: this file is driver-world
 * and the substitute's own header is the linuxkpi face's */
struct wiphy;
struct net_device;
struct wiphy *lkpicfg_wiphy(void);
struct net_device *lkpicfg_netdev(struct wiphy *wiphy);

/* --- the census clock (the image's board side) ---------------------------- */

uint64_t
lkpi_census_now_ns(void)
{
	uint64_t freq = (uint64_t)gtimer_freq_get();

	if (freq == 0u)
		return 0u;
	return (gtimer_count_get() * 1000000000ULL) / freq;
}

/* --- the driver handle chain ---------------------------------------------- */

static _adapter *
census_adapter(void)
{
	struct wiphy *wiphy = lkpicfg_wiphy();
	struct net_device *dev;

	dev = (wiphy != NULL) ? lkpicfg_netdev(wiphy) : NULL;
	return (dev != NULL) ? (_adapter *)rtw_netdev_priv(dev) : NULL;
}

/* --- the power policy (the A/B/A lever) ----------------------------------- */

int
wlan8189_ps_get(void)
{
	_adapter *adapter = census_adapter();

	return (adapter != NULL) ? (int)adapter_to_pwrctl(adapter)->power_mgnt : -1;
}

int
wlan8189_ps_set(int mode)
{
	_adapter *adapter = census_adapter();

	if (adapter == NULL)
		return -1;
	/* the driver's own setter: PS_MODE_ACTIVE leaves power save at once, the
	 * other modes set the leisure flag and let the firmware entry logic run */
	return rtw_pm_set_lps(adapter, (u8)mode);
}

int
wlan8189_ps_ips_set(int mode)
{
	_adapter *adapter = census_adapter();

	if (adapter == NULL)
		return -1;
	return rtw_pm_set_ips(adapter, (u8)mode);
}

void
wlan8189_ps_show(void)
{
	_adapter *adapter = census_adapter();
	struct pwrctrl_priv *p;

	if (adapter == NULL) {
		printk("wlan8189: ps adapter=absent\n");
		return;
	}
	p = adapter_to_pwrctl(adapter);
	/* the effective-state readback the judgment stands on: the requested mode,
	 * the leisure flag it drives, the firmware's current state and the
	 * leave/enter counts */
	printk("wlan8189: ps lps=%u leisure=%u fw_ps=%u cpwm=%u enter=%u leave=%u\n",
	    (unsigned)p->power_mgnt, (unsigned)p->bLeisurePs,
	    (unsigned)p->bFwCurrentInPSMode, (unsigned)p->cpwm,
	    (unsigned)p->lps_enter_cnts, (unsigned)p->lps_leave_cnts);
}

/* --- the rate / BA readout ------------------------------------------------ */

void
wlan8189_rate_show(void)
{
	_adapter *adapter = census_adapter();
	struct sta_info *psta;
	u8 *bssid;

	if (adapter == NULL) {
		printk("wlan8189: rate adapter=absent\n");
		return;
	}
	bssid = get_bssid(&adapter->mlmepriv);
	psta = rtw_get_stainfo(&adapter->stapriv, bssid);
	if (psta == NULL) {
		printk("wlan8189: rate sta=absent bssid=" MAC_FMT "\n",
		    MAC_ARG(bssid));
		return;
	}
	printk("wlan8189: rate bssid=" MAC_FMT
	    " bitmap=0x%02x ramask=0x%016llx bw=%d sgi=%u\n",
	    MAC_ARG(psta->cmn.mac_addr),
	    (unsigned)psta->htpriv.agg_enable_bitmap,
	    (unsigned long long)psta->cmn.ra_info.ramask,
	    (int)psta->cmn.ra_info.curr_tx_bw,
	    (unsigned)psta->cmn.ra_info.is_support_sgi);
	/* the driver's own station dump, the same one its join path prints once */
	dump_sta_info(RTW_DBGDUMP, psta);
}

/* --- the pump cadence (the round-2 lever) --------------------------------- */

/*
 * The engine's cadence knob, read back as the image's own line: the policy the
 * phase ran under is part of the measurement's identity, so it prints next to
 * the numbers it shaped.
 */
void
wlan8189_pump_show(void)
{
	unsigned int mode = lkpi_sdio_pump_mode_get();
	struct lkpi_sdio_bus_info bus;
	_adapter *adapter = census_adapter();

	lkpi_sdio_bus_info_get(&bus);
	printk("wlan8189: pump policy=%s period_ms=%u yield=%u prio=%s drain=%u clk_khz=%u bus=%u blk=%u\n",
	    (mode == LKPI_SDIO_PUMP_MODE_ADAPTIVE) ? "adaptive" : "fixed",
	    lkpi_sdio_pump_period_get(), lkpi_sdio_pump_yield_get() ? 1u : 0u,
	    lkpi_sdio_pump_prio_get() ? "normal" : "below",
	    lkpi_sdio_pump_drain_get() ? 1u : 0u,
	    bus.clock_hz / 1000u, bus.bus_width, bus.blk_size);
	/*
	 * The counting semaphores' addresses, so the wake-latency table keyed by
	 * semaphore address can be read: they are heap objects, so a symbol map
	 * cannot name them, and the driver's own structures can. Printed next to
	 * the cadence line because both are "what the phase ran on".
	 */
	if (adapter != NULL) {
		printk("wlan8189: sem addr xmit_sema=%lx sdio_xmit_sema=%lx xmitbuf_sema=%lx\n",
		    (unsigned long)&adapter->xmitpriv.xmit_sema,
		    (unsigned long)&adapter->xmitpriv.SdioXmitSema,
		    (unsigned long)&adapter->xmitpriv.sdio_free_xmitbuf_sema);
	}
}

const char *
wlan8189_pump_policy_get(void)
{

	return (lkpi_sdio_pump_mode_get() == LKPI_SDIO_PUMP_MODE_ADAPTIVE) ?
	    "adaptive" : "fixed";
}

int
wlan8189_pump_set(const char *policy)
{

	if (strcmp(policy, "fixed") == 0)
		lkpi_sdio_pump_mode_set(LKPI_SDIO_PUMP_MODE_FIXED);
	else if (strcmp(policy, "adaptive") == 0)
		lkpi_sdio_pump_mode_set(LKPI_SDIO_PUMP_MODE_ADAPTIVE);
	else if (strcmp(policy, "yield") == 0)
		lkpi_sdio_pump_yield_set(true);
	else if (strcmp(policy, "noyield") == 0)
		lkpi_sdio_pump_yield_set(false);
	else if (strcmp(policy, "prio-normal") == 0)
		lkpi_sdio_pump_prio_set(true);
	else if (strcmp(policy, "prio-below") == 0)
		lkpi_sdio_pump_prio_set(false);
	else if (strcmp(policy, "drain") == 0)
		lkpi_sdio_pump_drain_set(true);
	else if (strcmp(policy, "nodrain") == 0)
		lkpi_sdio_pump_drain_set(false);
	else
		return -1;
	return (int)lkpi_sdio_pump_mode_get();
}

void
wlan8189_pump_yield_set(int on)
{

	lkpi_sdio_pump_yield_set(on != 0);
}

int
wlan8189_pump_yield_get(void)
{

	return lkpi_sdio_pump_yield_get() ? 1 : 0;
}

int
wlan8189_pump_prio_set(int normal)
{

	lkpi_sdio_pump_prio_set(normal != 0);
	return lkpi_sdio_pump_prio_get() ? 1 : 0;
}

int
wlan8189_pump_prio_get(void)
{

	return lkpi_sdio_pump_prio_get() ? 1 : 0;
}

int
wlan8189_pump_drain_set(int on)
{

	lkpi_sdio_pump_drain_set(on != 0);
	return lkpi_sdio_pump_drain_get() ? 1 : 0;
}

int
wlan8189_pump_drain_get(void)
{

	return lkpi_sdio_pump_drain_get() ? 1 : 0;
}

/* --- the card-interrupt mode (the align round's lever) -------------------- */

/*
 * The engine's card-interrupt switch, read back the way the cadence is: the
 * mode a phase ran under is part of the measurement's identity. The evidence
 * that the mechanism actually ran lives in the census (D pumpirq notify/wake,
 * A dwc_cardirq ent/spur), so this line stays the policy statement.
 */
void
wlan8189_irq_show(void)
{

	printk("wlan8189: irq mode=%s\n", lkpi_sdio_pump_irq_get() ? "on" : "off");
}

int
wlan8189_irq_set(int on)
{
	int st = lkpi_sdio_pump_irq_set(on != 0);

	if (st != 0) {
		printk("wlan8189: irq set failed (%d)\n", st);
		return st;
	}
	return lkpi_sdio_pump_irq_get() ? 1 : 0;
}

int
wlan8189_irq_get(void)
{

	return lkpi_sdio_pump_irq_get() ? 1 : 0;
}

/* --- the runtime phase plan ----------------------------------------------- */

/*
 * One runtime-set single-variable comparison for the four-phase sheet (see
 * wlan8189_census.h). The sheet's phase routine re-asserts its full baseline at
 * every phase boundary, so a console command issued between phases would be
 * overwritten before the window; this plan is applied by that same routine, so
 * the command's value reaches the window through the one boundary the sheet
 * already has. Everything else in the routine stays baseline, so the plan is a
 * single-variable comparison exactly like a compiled lever.
 */
static struct {
	int		key;
	unsigned int	base;
	unsigned int	treat;
} wlan8189_plan;

int
wlan8189_plan_set(int key, unsigned int base, unsigned int treat)
{

	switch (key) {
	case WLAN8189_PLAN_AGG_TIMEOUT:
	case WLAN8189_PLAN_AGG_LEN:
	case WLAN8189_PLAN_POLL_US:
	case WLAN8189_PLAN_DATA_US:
		break;
	case WLAN8189_PLAN_PUMP:
	case WLAN8189_PLAN_YIELD:
	case WLAN8189_PLAN_PRIO:
		/* boolean-shaped keys: reject a value that is not 0/1 */
		if (base > 1u || treat > 1u)
			return -1;
		break;
	default:
		return -1;
	}
	wlan8189_plan.key = key;
	wlan8189_plan.base = base;
	wlan8189_plan.treat = treat;
	return 0;
}

void
wlan8189_plan_clear(void)
{

	wlan8189_plan.key = 0;
}

int
wlan8189_plan_apply(int treatment)
{
	unsigned int v = treatment ? wlan8189_plan.treat : wlan8189_plan.base;

	switch (wlan8189_plan.key) {
	case WLAN8189_PLAN_AGG_TIMEOUT:
		wlan8189_agg_timeout_set(v);
		break;
	case WLAN8189_PLAN_AGG_LEN:
		wlan8189_agg_set(v);
		break;
	case WLAN8189_PLAN_POLL_US:
		(void)wlan8189_dwc_poll_set(v);
		break;
	case WLAN8189_PLAN_DATA_US:
		(void)wlan8189_dwc_data_set(v);
		break;
	case WLAN8189_PLAN_PUMP:
		(void)wlan8189_pump_set(v ? "adaptive" : "fixed");
		break;
	case WLAN8189_PLAN_YIELD:
		wlan8189_pump_yield_set((int)v);
		break;
	case WLAN8189_PLAN_PRIO:
		(void)wlan8189_pump_prio_set((int)v);
		break;
	default:
		return 0;
	}
	return wlan8189_plan.key;
}

/* --- the card-side receiver account --------------------------------------- */

/*
 * The card's own receive error counter and the raw HISR word. The counter is
 * REG_RXERR_RPT (0x0664): the top nibble selects which internal counter the
 * register shows and the low twenty bits carry it (hal_com_reg.h: RXERR_RPT_SEL
 * / RXERR_COUNTER_MASK), and the read sequence is the driver's own MP-mode one
 * (core/rtw_mp.c GetPhyRxPktCounts: select, then read the 32-bit word). Type 15
 * is RX_FULL_DROP: frames the card dropped because its receive FIFO was full -
 * the number that separates "the host services too slowly" from "the card
 * overflows regardless". The HISR byte is read raw because its RXERR and RXFOVW
 * bits sit outside the driver's interrupt mask and therefore stay set once the
 * card raised them (a since-boot flag the delta cannot give).
 * The reads are plain card register accesses through the driver's own wrappers,
 * i.e. the bus claim is taken for the moment of the read - the caller reads at
 * phase boundaries, where no traffic is in flight.
 */
static u32 card_full_drop_base;
static u8 card_hisr_base;

static u32
card_full_drop_read(_adapter *adapter)
{

	rtw_write32(adapter, REG_RXERR_RPT,
	    _RXERR_RPT_SEL(RXERR_TYPE_RX_FULL_DROP));
	return rtw_read32(adapter, REG_RXERR_RPT) & RXERR_COUNTER_MASK;
}

static void
card_stats_note_base(_adapter *adapter)
{

	if (adapter == NULL)
		return;
	card_full_drop_base = card_full_drop_read(adapter);
	card_hisr_base = rtw_read8(adapter, SDIO_REG_HISR);
}

void
wlan8189_card_stats_show(void)
{
	_adapter *adapter = census_adapter();
	u32 now;
	u8 hisr;
	u32 agg = 0u;

	if (adapter == NULL) {
		printk("perf census: A card adapter=absent\n");
		return;
	}
	now = card_full_drop_read(adapter);
	hisr = rtw_read8(adapter, SDIO_REG_HISR);
	agg = rtw_read32(adapter, REG_RXDMA_AGG_PG_TH) & 0xffffu;
	/* the delta is masked to the counter's own width, so a wrap inside one
	 * phase still reads as a count rather than as a borrow */
	printk("perf census: A card full_drop=+%u hisr=0x%02x rxerr=%u rxfovw=%u agg=0x%04x (base=0x%02x)\n",
	    (unsigned)((now - card_full_drop_base) & RXERR_COUNTER_MASK),
	    (unsigned)hisr, (unsigned)((hisr >> 3) & 1u),
	    (unsigned)((hisr >> 5) & 1u), (unsigned)agg,
	    (unsigned)card_hisr_base);
}

/* --- the driver's receive-buffer free count (per pump cycle) --------------- */

/*
 * The number the shortage path turns on: how many of the driver's eight receive
 * buffers are free at the moment the pump is about to ask for one. The count is
 * taken by walking the free queue's list - the driver's own cache of that count
 * (free_recv_buf_queue_cnt) is only ever written at init and teardown, so it
 * cannot be read as a live value. The walk is bounded and runs on the pump
 * thread while the bottom half may move elements; a torn read costs one wrong
 * sample, which is why this is an instrument and not an invariant.
 */
static u32 card_free_min = ~0u;
static u32 card_free_zero;
static u32 card_free_sum;
static u32 card_free_n;

static u32
card_free_recvbuf_count(_adapter *adapter)
{
	const struct list_head *head = &adapter->recvpriv.free_recv_buf_queue.queue;
	const struct list_head *p = head->next;
	u32 n = 0u;

	if (head->next == NULL || head->prev == NULL)
		return 0u;
	while (p != head && n < 32u) {
		n++;
		p = p->next;
		if (p == NULL)
			break;
	}
	return n;
}

void
lkpi_census_pump_probe(void)
{
	_adapter *adapter = census_adapter();
	u32 free_n;

	if (adapter == NULL)
		return;
	free_n = card_free_recvbuf_count(adapter);
	card_free_n++;
	card_free_sum += free_n;
	if (free_n < card_free_min)
		card_free_min = free_n;
	if (free_n == 0u)
		card_free_zero++;
}

void
wlan8189_cardq_show(void)
{
	u32 avg_x10 = (card_free_n != 0u) ?
	    (u32)(((unsigned long long)card_free_sum * 10u) / card_free_n) : 0u;

	printk("perf census: A cardq free_min=%u free_avg_x10=%u zero=%u n=%u\n",
	    (unsigned)((card_free_min == ~0u) ? 0u : card_free_min),
	    (unsigned)avg_x10, (unsigned)card_free_zero, (unsigned)card_free_n);
}

/* --- the card's receive aggregation thresholds ---------------------------- */

/*
 * REG_RXDMA_AGG_PG_TH (0x0280): the timeout in the high byte, the length
 * threshold in the low byte in kilobytes (the driver's own clamp divides by
 * 1024 - hal/rtl8188f/sdio/sdio_halinit.c HalRxAggr8188FSdio). The card
 * releases a burst when either the accumulated length crosses the threshold or
 * the timeout expires; the driver writes the register once at init, and this
 * runtime knob re-writes either byte per phase so one boot can compare release
 * policies against the throughput.
 *
 * Every write here is a DIRECT 16-bit write, the driver's own shape
 * (HalRxAggr8188FSdio issues one rtw_write16). The byte the caller is not
 * setting comes from a shadow copy, never from reading 0x0280: the 2026-10-19
 * write-shape round observed the phase-4 receive-presentation collapse in the
 * every-phase read-modify-write arms (A: two 16-bit RMWs, p4 0.05), while the
 * direct-write arms were healthy (B: one write, p4 15.19; C: two writes, p4
 * 14.68). That makes the read-before-write pair the leading candidate
 * association, not a proven cause; width, write count and value are disfavored
 * as sole factors, not excluded. The phase-end agg_get read stays: it runs
 * after the write, and B/C were healthy with it.
 *
 * The measured card releases ~4 KB per burst against a 15 KB length threshold,
 * i.e. the timeout, not the length, is what ends most bursts. The timeout byte
 * is therefore its own lever, and it is the one this knob's second form moves.
 */
/* the harness baseline (timeout 0x20, length 15 KB); the setup write seeds it */
static u16 wlan8189_agg_shadow = (u16)((0x20u << 8) | WLAN8189_AGG_STOCK_KB);

void
wlan8189_agg_set(unsigned int size_kb)
{
	_adapter *adapter = census_adapter();

	if (adapter == NULL)
		return;
	wlan8189_agg_shadow = (u16)((wlan8189_agg_shadow & ~0xffu) |
	    (size_kb & 0xffu));
	rtw_write16(adapter, REG_RXDMA_AGG_PG_TH, wlan8189_agg_shadow);
}

void
wlan8189_agg_timeout_set(unsigned int timeout_units)
{
	_adapter *adapter = census_adapter();

	if (adapter == NULL)
		return;
	wlan8189_agg_shadow = (u16)((wlan8189_agg_shadow & ~0xff00u) |
	    ((timeout_units & 0xffu) << 8));
	rtw_write16(adapter, REG_RXDMA_AGG_PG_TH, wlan8189_agg_shadow);
}

unsigned int
wlan8189_agg_get(void)
{
	_adapter *adapter = census_adapter();

	if (adapter == NULL)
		return 0u;
	return (unsigned int)rtw_read16(adapter, REG_RXDMA_AGG_PG_TH);
}

/*
 * One direct 16-bit write of the whole register - the driver's own shape
 * (HalRxAggr8188FSdio). No read, no read-modify-write: the B arm uses it to
 * ask whether a single boundary write is safe, the C arm calls it twice to ask
 * whether the second write (rather than the RMW read) is what the card minds.
 * It also seeds the shadow the single-byte levers build on.
 */
void
wlan8189_agg_write(unsigned int value)
{
	_adapter *adapter = census_adapter();

	if (adapter == NULL)
		return;
	wlan8189_agg_shadow = (u16)(value & 0xffffu);
	rtw_write16(adapter, REG_RXDMA_AGG_PG_TH, wlan8189_agg_shadow);
}

/* --- the host controller's completion-poll step --------------------------- */

/*
 * The step the controller's completion waits sleep between status reads, in
 * microseconds. It is the round's lever for the question "is the 100 us step on
 * the critical path": every command's completion was quantised to one step, so
 * a finer step pays off exactly when the wait was consuming the step rather
 * than the hardware's own completion time. The readback goes out on the pump
 * line so a phase reports the step it ran at.
 *
 * The data-stage waits carry their own step (see dwc_mmc.h): the eMMC line's
 * controller port runs its data wait an order of magnitude finer than its
 * command wait, and this port ran both at one step until that comparison was
 * made - so the data step is the lever this round's sheet moves.
 */
void
wlan8189_dwc_show(void)
{

	printk("wlan8189: dwc poll_us=%u data_us=%u\n",
	    dwc_mmc_get_poll_us(), dwc_mmc_get_data_poll_us());
}

unsigned int
wlan8189_dwc_poll_set(unsigned int us)
{

	dwc_mmc_set_poll_us(us);
	return dwc_mmc_get_poll_us();
}

unsigned int
wlan8189_dwc_data_set(unsigned int us)
{

	dwc_mmc_set_data_poll_us(us);
	return dwc_mmc_get_data_poll_us();
}

/* --- the snapshot --------------------------------------------------------- */

void
wlan8189_census_reset(void)
{

	lkpi_sdio_census_reset();
	lkpi_core_census_reset();
	lkpi_osdep_census_reset();
	lkpi_fsl_census_reset();
	dwc_mmc_census_reset();
	card_stats_note_base(census_adapter());
	card_free_min = ~0u;
	card_free_zero = 0u;
	card_free_sum = 0u;
	card_free_n = 0u;
}

/*
 * The delta epoch of the cumulative groups: the driver's own counters (B) and
 * the netif handoff (C) are lifetime totals no reset touches, so a per-phase
 * value is this snapshot minus the previous one. The absolute lines above stay
 * byte-for-byte; these Bd/Cd lines carry the difference a per-phase account
 * needs, and the epoch marker reports when the chain restarted (the first
 * snapshot of a boot has no predecessor). Only monotone counters are
 * subtracted - summed latencies yes, the max fields no (a lifetime maximum
 * minus a lifetime maximum is not an interval maximum).
 */
struct wlan8189_delta_epoch {
	unsigned long long b_rx_pkts, b_rx_drop, b_rx_core, b_rx_ampdu_drop,
	    b_rx_forced, b_rx_loss, b_rx_shift;
	unsigned long long b_tx_pkts, b_tx_drop;
	struct lkpinetif_diag c;
	int valid;
};

static struct wlan8189_delta_epoch delta_epoch;

static void
census_print_deltas( _adapter *adapter, const struct lkpinetif_diag *d )
{

	if( delta_epoch.valid ) {
		if( adapter != NULL ) {
			printk("perf census: Bd rx pkt=%llu drop=%llu core=%llu ampdu_drop=%llu forced=%llu loss=%llu shift=%llu\n",
			    (unsigned long long)adapter->recvpriv.rx_pkts - delta_epoch.b_rx_pkts,
			    (unsigned long long)adapter->recvpriv.rx_drop - delta_epoch.b_rx_drop,
			    (unsigned long long)adapter->recvpriv.dbg_rx_drop_count - delta_epoch.b_rx_core,
			    (unsigned long long)adapter->recvpriv.dbg_rx_ampdu_drop_count - delta_epoch.b_rx_ampdu_drop,
			    (unsigned long long)adapter->recvpriv.dbg_rx_ampdu_forced_indicate_count - delta_epoch.b_rx_forced,
			    (unsigned long long)adapter->recvpriv.dbg_rx_ampdu_loss_count - delta_epoch.b_rx_loss,
			    (unsigned long long)adapter->recvpriv.dbg_rx_ampdu_window_shift_cnt - delta_epoch.b_rx_shift);
			printk("perf census: Bd tx pkt=%llu drop=%llu\n",
			    (unsigned long long)adapter->xmitpriv.tx_pkts - delta_epoch.b_tx_pkts,
			    (unsigned long long)adapter->xmitpriv.tx_drop - delta_epoch.b_tx_drop);
		}
		printk("perf census: Cd netif rx=%lu bytes=%lu eapol=%lu foreign=%lu nonetif=%lu infail=%lu tx=%lu fail=%lu tx_bytes=%lu\n",
		    d->rx_frames - delta_epoch.c.rx_frames,
		    d->rx_bytes - delta_epoch.c.rx_bytes,
		    d->rx_eapol - delta_epoch.c.rx_eapol,
		    d->rx_foreign - delta_epoch.c.rx_foreign,
		    d->rx_no_netif - delta_epoch.c.rx_no_netif,
		    d->rx_input_fail - delta_epoch.c.rx_input_fail,
		    d->tx_frames - delta_epoch.c.tx_frames,
		    d->tx_fail - delta_epoch.c.tx_fail,
		    d->tx_bytes - delta_epoch.c.tx_bytes);
		printk("perf census: Cd netif_lat rx_deliver_us=%llu tx_ndo_us=%llu\n",
		    (unsigned long long)(( d->rx_deliver_ns - delta_epoch.c.rx_deliver_ns ) / 1000u),
		    (unsigned long long)(( d->tx_ndo_ns - delta_epoch.c.tx_ndo_ns ) / 1000u));
	} else {
		printk("perf census: Bd epoch=first (no predecessor: deltas start at the next snapshot)\n");
	}
	if( adapter != NULL ) {
		delta_epoch.b_rx_pkts = adapter->recvpriv.rx_pkts;
		delta_epoch.b_rx_drop = adapter->recvpriv.rx_drop;
		delta_epoch.b_rx_core = adapter->recvpriv.dbg_rx_drop_count;
		delta_epoch.b_rx_ampdu_drop = adapter->recvpriv.dbg_rx_ampdu_drop_count;
		delta_epoch.b_rx_forced = adapter->recvpriv.dbg_rx_ampdu_forced_indicate_count;
		delta_epoch.b_rx_loss = adapter->recvpriv.dbg_rx_ampdu_loss_count;
		delta_epoch.b_rx_shift = adapter->recvpriv.dbg_rx_ampdu_window_shift_cnt;
		delta_epoch.b_tx_pkts = adapter->xmitpriv.tx_pkts;
		delta_epoch.b_tx_drop = adapter->xmitpriv.tx_drop;
	}
	delta_epoch.c = *d;
	delta_epoch.valid = 1;
}

/*
 * The skb pool's occupancy and high-water, on its own so a measurement window
 * can sample it while it runs and not only at its end: a wedge inside a phase
 * used to leave no pool state at all, which is exactly the state a leak is
 * read from. The damage counters (double free / stray / use-after-free /
 * fence) name a geometry problem instead of leaving it to be inferred.
 */
void
wlan8189_census_pool_line(void)
{
	struct lkpi_skb_pool_stats skb;

	lkpi_skb_pool_stats(&skb);
	printk("perf census: D skb used=%u high=%u data_used=%u data_high=%u dyalloc=%u ovf=%u+%u fail=%u dfree=%u stray=%u uaf=%u fence=%u\n",
	    skb.skb_used, skb.skb_high, skb.data_used, skb.data_high,
	    skb.dyalloc_live, skb.slot_overflow, skb.data_overflow,
	    skb.alloc_fail, skb.double_free, skb.stray_free,
	    skb.uaf_write, skb.fence_bad);
}

/* the episode totals, printed at the phase boundary (defined with the recorder) */
static void	wlan8189_census_rxstall_line(void);

void
wlan8189_census_print(void)
{
	_adapter *adapter = census_adapter();
	struct lkpinetif_diag d;
	struct dwc_mmc_census dwc;
	unsigned long log_in_lines, log_in_bytes, log_out_lines, log_out_bytes;

	lkpi_sdio_census_print();
	lkpi_core_census_print();
	lkpi_osdep_census_print();
	lkpi_fsl_census_print();

	dwc_mmc_census_get(&dwc);
	printk("perf census: A dwc transfers=%u data=%u errs=%u rx_bytes=%llu tx_bytes=%llu\n",
	    dwc.transfers, dwc.with_data, dwc.errors,
	    (unsigned long long)dwc.rx_bytes,
	    (unsigned long long)dwc.tx_bytes);
	printk("perf census: A dwc_wait cmd n=%u us=%llu max=%llu data n=%u us=%llu max=%llu idmac n=%u us=%llu max=%llu\n",
	    dwc.cmd_polls, (unsigned long long)(dwc.cmd_wait_ns / 1000u),
	    (unsigned long long)(dwc.cmd_wait_max_ns / 1000u),
	    dwc.data_polls, (unsigned long long)(dwc.data_wait_ns / 1000u),
	    (unsigned long long)(dwc.data_wait_max_ns / 1000u),
	    dwc.idmac_polls, (unsigned long long)(dwc.idmac_wait_ns / 1000u),
	    (unsigned long long)(dwc.idmac_wait_max_ns / 1000u));
	/* the controller's card-interrupt witness: entries that carried DAT1
	 * versus entries that carried nothing this driver owns (spurious). Zero
	 * entries on an irq-on phase means the line never reached the CPU - the
	 * number the mechanism's self-test reads. */
	printk("perf census: A dwc_cardirq ent=%u spur=%u\n",
	    dwc.card_irq_entries, dwc.card_irq_spurious);

	/* The suspend-guard probe (the wedge issue's ask, printed with every
	 * phase snapshot): the skb pools' occupancy and high-water, so a hang
	 * leaves the accumulation visible in the phase it started in, and the
	 * pool's own damage counters (double free / stray / use-after-free /
	 * fence) name a geometry problem instead of leaving it to be inferred. */
	wlan8189_census_pool_line();

	/* the driver's own progress account: what it dropped, reordered or forced
	 * out of its reorder window */
	if (adapter != NULL) {
		printk("perf census: B rx pkt=%llu drop=%llu core=%llu ampdu_drop=%llu forced=%llu loss=%llu shift=%llu\n",
		    (unsigned long long)adapter->recvpriv.rx_pkts,
		    (unsigned long long)adapter->recvpriv.rx_drop,
		    (unsigned long long)adapter->recvpriv.dbg_rx_drop_count,
		    (unsigned long long)adapter->recvpriv.dbg_rx_ampdu_drop_count,
		    (unsigned long long)adapter->recvpriv.dbg_rx_ampdu_forced_indicate_count,
		    (unsigned long long)adapter->recvpriv.dbg_rx_ampdu_loss_count,
		    (unsigned long long)adapter->recvpriv.dbg_rx_ampdu_window_shift_cnt);
		printk("perf census: B tx pkt=%llu drop=%llu\n",
		    (unsigned long long)adapter->xmitpriv.tx_pkts,
		    (unsigned long long)adapter->xmitpriv.tx_drop);
	} else {
		printk("perf census: B adapter=absent\n");
	}

	/* the handoff: what crossed the netif adapter and what the crossing cost */
	lkpinetif_get_diag(&d);
	printk("perf census: C netif rx=%lu bytes=%lu eapol=%lu foreign=%lu nonetif=%lu infail=%lu tx=%lu fail=%lu tx_bytes=%lu\n",
	    d.rx_frames, d.rx_bytes, d.rx_eapol, d.rx_foreign, d.rx_no_netif,
	    d.rx_input_fail, d.tx_frames, d.tx_fail, d.tx_bytes);
	printk("perf census: C netif_lat rx_deliver_us=%llu rx_max_us=%llu tx_ndo_us=%llu tx_max_us=%llu\n",
	    (unsigned long long)(d.rx_deliver_ns / 1000u),
	    (unsigned long long)(d.rx_deliver_max_ns / 1000u),
	    (unsigned long long)(d.tx_ndo_ns / 1000u),
	    (unsigned long long)(d.tx_ndo_max_ns / 1000u));
	/* the per-snapshot movement of the cumulative groups (B, C): the
	 * absolute lines above are the endpoints, these are the difference a
	 * per-phase account reads */
	census_print_deltas(adapter, &d);

	wlan8189_census_log_get(&log_in_lines, &log_in_bytes, &log_out_lines,
	    &log_out_bytes);
	printk("perf census: D log lines=%lu bytes=%lu emitted_lines=%lu emitted_bytes=%lu\n",
	    log_in_lines, log_in_bytes, log_out_lines, log_out_bytes);

	/* the round's question sheet asks about a net80211 account explicitly:
	 * this image carries no net80211 at all (the FullMAC driver's data path
	 * never crosses it), so the protocol-side account is the effective-state
	 * readout below, not an 802.11 input count */
	printk("perf census: N net80211 linked=0 frames=0 (FullMAC line: the data path never crosses net80211)\n");

	/* The episode totals survive a serial ring that drops the begin/end
	 * lines: one line per phase carries the whole phase's stall account. */
	wlan8189_census_rxstall_line();

	wlan8189_card_stats_show();
	wlan8189_cardq_show();
	wlan8189_rate_show();
	wlan8189_ps_show();
	wlan8189_pump_show();
}

/* --- the stall watchdog and the receive-stall episode recorder ------------- */

/*
 * Two readers, one clock:
 *
 * - The watchdog is the total-freeze backstop. When neither the sheet's own
 *   heartbeat nor any data-path counter has moved for three seconds, the whole
 *   machine (or the path the shell shares) has stopped, and one line says so.
 *   It never takes a data-path lock and never touches the bus.
 *
 * - The episode recorder is the receive-stall instrument. While a traffic
 *   window is armed, a second without data-path progress opens an episode and
 *   prints what the three layers said at that moment - what the card offered,
 *   what the host read, what the stack saw - repeats while the episode lasts,
 *   and closes it when progress resumes. A phase average hides a five-second
 *   stall; these lines do not.
 *
 * Progress deliberately excludes the pump's own poll count and the sheet
 * heartbeat: a pump spinning over an empty card is exactly the shape a stall
 * has, and counting its cycles would mask the stall (the 2026-10-18 rounds'
 * `watchdog_stalls=0` beside multi-second client windows).
 */
static volatile unsigned long census_hb;
static volatile unsigned long census_stalls;

void
wlan8189_census_hb(void)
{
	census_hb++;
}

unsigned long
wlan8189_census_stall_count(void)
{
	return census_stalls;
}

/* one progress snapshot: everything the recorder subtracts to describe a stall */
struct wlan8189_stall_base {
	unsigned long long	progress;
	uint64_t		t_ns;
	unsigned int		pump_polls;
	unsigned int		offer_reads;
	unsigned int		offer_zero;
	unsigned int		read_ok;
	unsigned int		read_fail;
	unsigned int		hisr_rxreq;
	unsigned int		hisr_aval;
	unsigned int		hisr_rxerr;
	unsigned int		hisr_rxfovw;
	unsigned long		netif_rx;
	unsigned long		netif_tx;
	unsigned long long	drv_rx;
	unsigned long long	drv_tx;
};

static volatile int stall_armed;
static volatile unsigned int stall_phase;
static volatile char stall_arm;
static volatile unsigned int stall_agg;
static int stall_started;	/* the phase moved data at least once */
static int stall_active;
static uint64_t stall_open_ns;
static uint64_t stall_last_print_ns;
static struct wlan8189_stall_base stall_base;
static unsigned long stall_episodes;
static unsigned long stall_ms_total;
static unsigned long stall_ms_max;

/*
 * The rate-collapse floor, bytes per second of netif receive. The silence
 * detector (no data-plane progress at all) misses a stall that trickles: the
 * 2026-10-19 constA-w phase 4 moved 16-30 KB/s for its whole window, never
 * silent for a full second, and read as `episodes=0`. Two consecutive seconds
 * below this floor open a `slow` episode with the same three-layer deltas.
 */
#define	WLAN8189_STALL_RATE_FLOOR	100000UL
static unsigned int stall_slow_ticks;
static unsigned long stall_last_rx;
static uint64_t stall_last_rx_ns;

static unsigned long long
stall_progress(void)
{
	struct lkpinetif_diag d;
	struct lkpi_sdio_rx_stats rx;
	_adapter *adapter = census_adapter();
	unsigned long long p = 0ULL;

	lkpinetif_get_diag(&d);
	lkpi_sdio_rx_stats_get(&rx);
	p += (unsigned long long)d.rx_bytes + (unsigned long long)d.tx_bytes;
	p += (unsigned long long)rx.read_ok;
	if (adapter != NULL) {
		p += (unsigned long long)adapter->recvpriv.rx_pkts;
		p += (unsigned long long)adapter->xmitpriv.tx_pkts;
	}
	return p;
}

static void
stall_snapshot(struct wlan8189_stall_base *b)
{
	struct lkpinetif_diag d;
	struct lkpi_sdio_rx_stats rx;
	struct lkpi_sdio_stats st;
	_adapter *adapter = census_adapter();

	lkpinetif_get_diag(&d);
	lkpi_sdio_rx_stats_get(&rx);
	lkpi_sdio_stats_get(&st);
	b->progress = stall_progress();
	b->t_ns = lkpi_census_now_ns();
	b->pump_polls = st.pump_polls;
	b->offer_reads = rx.offer_reads;
	b->offer_zero = rx.offer_zero;
	b->read_ok = rx.read_ok;
	b->read_fail = rx.read_fail;
	b->hisr_rxreq = rx.hisr_rxreq;
	b->hisr_aval = rx.hisr_aval;
	b->hisr_rxerr = rx.hisr_rxerr;
	b->hisr_rxfovw = rx.hisr_rxfovw;
	b->netif_rx = d.rx_bytes;
	b->netif_tx = d.tx_bytes;
	b->drv_rx = (adapter != NULL) ? adapter->recvpriv.rx_pkts : 0ULL;
	b->drv_tx = (adapter != NULL) ? adapter->xmitpriv.tx_pkts : 0ULL;
}

/* the episode's line: one shape for begin, hold and end, all deltas against
 * the snapshot taken at the last progress point. No bus access here - the
 * recorder must be able to speak while the pump is the thing that stopped. */
static void
stall_print(const char *what, unsigned int quiet_ms)
{
	struct lkpinetif_diag d;
	struct lkpi_sdio_rx_stats rx;
	struct lkpi_sdio_stats st;
	struct lkpi_skb_pool_stats skb;
	_adapter *adapter = census_adapter();
	uint64_t now = lkpi_census_now_ns();

	lkpinetif_get_diag(&d);
	lkpi_sdio_rx_stats_get(&rx);
	lkpi_sdio_stats_get(&st);
	lkpi_skb_pool_stats(&skb);
	LOG_ANCHOR("wlan8189: rxstall %s t_ms=%llu phase=%u arm=%c agg=0x%04x "
	    "quiet_ms=%u pump=%u offer=%u zero=%u read_ok=%u fail=%u "
	    "hisr=%u/%u netif_rx=%lu drv_rx=%llu claim=%s/%u core=%s/%lu "
	    "skb=%u/%u\n", what, (unsigned long long)(now / 1000000ULL),
	    stall_phase, stall_arm, stall_agg, quiet_ms,
	    st.pump_polls - stall_base.pump_polls,
	    rx.offer_reads - stall_base.offer_reads,
	    rx.offer_zero - stall_base.offer_zero,
	    rx.read_ok - stall_base.read_ok,
	    rx.read_fail - stall_base.read_fail,
	    rx.hisr_rxreq - stall_base.hisr_rxreq,
	    rx.hisr_aval - stall_base.hisr_aval,
	    (unsigned long)(d.rx_bytes - stall_base.netif_rx),
	    (unsigned long long)(((adapter != NULL) ?
	    adapter->recvpriv.rx_pkts : 0ULL) - stall_base.drv_rx),
	    lkpi_sdio_claim_owner(), lkpi_sdio_claim_held_ms(),
	    lkpinetif_core_lock_owner(), lkpinetif_core_lock_held_ms(),
	    skb.skb_used, skb.data_used);
}

/* arm at the traffic bracket's start, disarm at its end. The aggregate value is
 * the phase's own readback, so the line carries the arm without touching the
 * card from this thread. */
void
wlan8189_census_stall_arm(int on, unsigned int phase, int treatment,
    unsigned int agg)
{
	if (on) {
		struct lkpinetif_diag d;

		stall_phase = phase;
		stall_arm = treatment ? 'B' : 'A';
		stall_agg = agg;
		stall_started = 0;
		stall_active = 0;
		stall_slow_ticks = 0U;
		stall_snapshot(&stall_base);
		lkpinetif_get_diag(&d);
		stall_last_rx = d.rx_bytes;
		stall_last_rx_ns = lkpi_census_now_ns();
		stall_armed = 1;
	} else {
		if (stall_active) {
			uint64_t now = lkpi_census_now_ns();
			unsigned long dur = (unsigned long)((now - stall_open_ns) /
			    1000000ULL);

			stall_print("end", (unsigned int)dur);
			stall_ms_total += dur;
			if (dur > stall_ms_max)
				stall_ms_max = dur;
			stall_active = 0;
		}
		stall_armed = 0;
		stall_started = 0;
	}
}

unsigned long
wlan8189_census_rxstall_count(void)
{
	return stall_episodes;
}

unsigned long
wlan8189_census_rxstall_ms_total(void)
{
	return stall_ms_total;
}

unsigned long
wlan8189_census_rxstall_ms_max(void)
{
	return stall_ms_max;
}

static void
wlan8189_census_rxstall_line(void)
{

	printk("perf census: X rxstall episodes=%lu ms_total=%lu ms_max=%lu "
	    "armed=%d\n", stall_episodes, stall_ms_total, stall_ms_max,
	    stall_armed ? 1 : 0);
}

/* one second of the recorder, called from the watchdog thread */
static void
stall_tick(void)
{
	uint64_t t;
	unsigned long long p;
	struct lkpinetif_diag d;
	int stalled;

	if (!stall_armed)
		return;
	t = lkpi_census_now_ns();
	p = stall_progress();
	if (!stall_started) {
		/* the phase's first movement starts the clock: a window that never
		 * carried traffic is not a stall */
		if (p != stall_base.progress) {
			stall_started = 1;
			stall_snapshot(&stall_base);
		}
		return;
	}
	/* the rate-collapse signal: the last full second's netif receive bytes */
	lkpinetif_get_diag(&d);
	if ((t - stall_last_rx_ns) >= 1000000000ULL) {
		unsigned long rate = (unsigned long)
		    (((d.rx_bytes - stall_last_rx) * 1000000000ULL) /
		    (t - stall_last_rx_ns));

		stall_last_rx = d.rx_bytes;
		stall_last_rx_ns = t;
		if (rate < WLAN8189_STALL_RATE_FLOOR) {
			if (stall_slow_ticks < 100U)
				stall_slow_ticks++;
		} else {
			stall_slow_ticks = 0U;
		}
	}
	/* one episode state machine for both signatures: no progress at all
	 * (silence) or two seconds below the rate floor (trickle) */
	stalled = (p == stall_base.progress) || (stall_slow_ticks >= 2U);
	if (stalled && !stall_active) {
		stall_active = 1;
		stall_open_ns = t;
		stall_last_print_ns = t;
		stall_episodes++;
		stall_print((stall_slow_ticks >= 2U) ? "slow" : "begin",
		    (unsigned int)((t - stall_base.t_ns) / 1000000ULL));
	} else if (!stalled && stall_active) {
		unsigned long dur = (unsigned long)((t - stall_open_ns) /
		    1000000ULL);

		stall_print("end", (unsigned int)dur);
		stall_ms_total += dur;
		if (dur > stall_ms_max)
			stall_ms_max = dur;
		stall_active = 0;
	} else if (stall_active &&
	    (t - stall_last_print_ns) >= 1000000000ULL) {
		stall_last_print_ns = t;
		stall_print("hold", (unsigned int)((t - stall_open_ns) /
		    1000000ULL));
	}
	if (p != stall_base.progress)
		stall_snapshot(&stall_base);
}

static void
wlan8189_watchdog(void *arg)
{
	unsigned long last_hb = 0UL;
	unsigned long long last_progress = 0ULL;
	unsigned int quiet = 0U;

	(void)arg;
	for (;;) {
		unsigned long long progress;

		(void)osDelay(1000U);
		stall_tick();
		/*
		 * The freeze backstop only speaks while a traffic window is armed.
		 * Outside one (the AP case's station wait, the gap between sheets)
		 * no data-plane progress is normal, and the old line fired there -
		 * a false positive that would invalidate a perfectly good boot.
		 */
		if (!stall_armed || !stall_started) {
			last_hb = census_hb;
			last_progress = stall_progress();
			quiet = 0U;
			continue;
		}
		progress = stall_progress();
		if (census_hb != last_hb || progress != last_progress) {
			last_hb = census_hb;
			last_progress = progress;
			quiet = 0U;
			continue;
		}
		quiet++;
		if (quiet < 3U || (quiet > 3U && (quiet % 10U) != 0U))
			continue;
		{
			_adapter *adapter = census_adapter();
			struct lkpi_skb_pool_stats skb;
			struct lkpinetif_diag d;

			lkpi_skb_pool_stats(&skb);
			lkpinetif_get_diag(&d);
			LOG_ANCHOR("wlan8189: watchdog stall quiet=%u hb=%lu "
			    "alloc=%lu free=%lu skb_used=%u data=%u fail=%u "
			    "rx=%lu tx=%lu tx_fail=%lu drv_tx=%llu drv_drop=%llu "
			    "drv_rx=%llu tcpip_hb=%lu\n", quiet, census_hb,
			    (unsigned long)skb.n_alloc, (unsigned long)skb.n_free,
			    skb.skb_used, skb.data_used, skb.alloc_fail,
			    d.rx_bytes, d.tx_bytes, d.tx_fail,
			    (unsigned long long)((adapter != NULL) ?
			    adapter->xmitpriv.tx_pkts : 0ULL),
			    (unsigned long long)((adapter != NULL) ?
			    adapter->xmitpriv.tx_drop : 0ULL),
			    (unsigned long long)((adapter != NULL) ?
			    adapter->recvpriv.rx_pkts : 0ULL),
			    lkpinetif_tcpip_heartbeat());
		}
		census_stalls++;
	}
}

void
wlan8189_census_watchdog_start(void)
{
	static osThreadId_t id;
	static const osThreadAttr_t attr = {
		.name = "wlan8189-wd",
		.stack_size = 4096U,
		.priority = osPriorityRealtime,
	};

	if (id == NULL) {
		id = osThreadNew(wlan8189_watchdog, NULL, &attr);
	}
}
