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

#include "drv_types.h"
#include "hal/hal_dm.h"

#include "dwc_mmc.h"
#include "lkpinetif_seam.h"
#include "lkpi_census.h"
#include "linuxkpi_sdio.h"

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
 * policies against the throughput (the byte it is not setting, and every other
 * bit, are preserved by the read-modify-write).
 *
 * The measured card releases ~4 KB per burst against a 15 KB length threshold,
 * i.e. the timeout, not the length, is what ends most bursts. The timeout byte
 * is therefore its own lever, and it is the one this knob's second form moves.
 */
void
wlan8189_agg_set(unsigned int size_kb)
{
	_adapter *adapter = census_adapter();
	u32 v;

	if (adapter == NULL)
		return;
	v = rtw_read32(adapter, REG_RXDMA_AGG_PG_TH);
	v = (v & ~0xffu) | (size_kb & 0xffu);
	rtw_write32(adapter, REG_RXDMA_AGG_PG_TH, v);
}

void
wlan8189_agg_timeout_set(unsigned int timeout_units)
{
	_adapter *adapter = census_adapter();
	u32 v;

	if (adapter == NULL)
		return;
	v = rtw_read32(adapter, REG_RXDMA_AGG_PG_TH);
	v = (v & ~0xff00u) | ((timeout_units & 0xffu) << 8);
	rtw_write32(adapter, REG_RXDMA_AGG_PG_TH, v);
}

unsigned int
wlan8189_agg_get(void)
{
	_adapter *adapter = census_adapter();

	if (adapter == NULL)
		return 0u;
	return rtw_read32(adapter, REG_RXDMA_AGG_PG_TH) & 0xffffu;
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
	{
		struct lkpi_skb_pool_stats skb;

		lkpi_skb_pool_stats(&skb);
		printk("perf census: D skb used=%u high=%u data_used=%u data_high=%u dyalloc=%u ovf=%u+%u fail=%u dfree=%u stray=%u uaf=%u fence=%u\n",
		    skb.skb_used, skb.skb_high, skb.data_used, skb.data_high,
		    skb.dyalloc_live, skb.slot_overflow, skb.data_overflow,
		    skb.alloc_fail, skb.double_free, skb.stray_free,
		    skb.uaf_write, skb.fence_bad);
	}

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

	wlan8189_census_log_get(&log_in_lines, &log_in_bytes, &log_out_lines,
	    &log_out_bytes);
	printk("perf census: D log lines=%lu bytes=%lu emitted_lines=%lu emitted_bytes=%lu\n",
	    log_in_lines, log_in_bytes, log_out_lines, log_out_bytes);

	/* the round's question sheet asks about a net80211 account explicitly:
	 * this image carries no net80211 at all (the FullMAC driver's data path
	 * never crosses it), so the protocol-side account is the effective-state
	 * readout below, not an 802.11 input count */
	printk("perf census: N net80211 linked=0 frames=0 (FullMAC line: the data path never crosses net80211)\n");

	wlan8189_card_stats_show();
	wlan8189_cardq_show();
	wlan8189_rate_show();
	wlan8189_ps_show();
	wlan8189_pump_show();
}
