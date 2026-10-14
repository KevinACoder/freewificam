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

/* --- the snapshot --------------------------------------------------------- */

void
wlan8189_census_reset(void)
{

	lkpi_sdio_census_reset();
	lkpi_core_census_reset();
	lkpi_osdep_census_reset();
	lkpi_fsl_census_reset();
	dwc_mmc_census_reset();
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

	wlan8189_rate_show();
	wlan8189_ps_show();
}
