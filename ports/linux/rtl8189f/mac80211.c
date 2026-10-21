/*
 * @file
 * @brief Stock-mac80211 front-end skeleton for the RTL8189FTV (R2).
 *
 * The HAL/adapter (adapter.c) owns the chip, firmware and channel; this file
 * gives mac80211 a wiphy/netdev and the minimum ieee80211_ops for an
 * unassociated station:
 *
 *   start/stop       - the HAL is already up from probe; no-op
 *   add/remove iface - accept one station vif
 *   config           - program the requested channel through the vendor HAL
 *   sw_scan_*        - set the channel, wait, then complete the scan
 *
 * RX/TX and real scan results (probe request/beacon into ieee80211_rx) are the
 * next slice; this file deliberately does not link the vendor MLME.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>

#include <drv_types.h>
#include <hal_intf.h>
#include <hal_data.h>

extern _adapter *rtl8189f_adapter_get(void);

struct rtl8189f_hw {
	struct ieee80211_hw *hw;
	_adapter *adapter;
	struct delayed_work scan_done;
	u32 scan_rcr;
	u16 scan_flt2;
	bool scanning;
	bool dumped;
};

extern void rtl8189f_dump_mac(_adapter *padapter);
extern int rtl8189f_force_ch_get(void);

extern bool rtl8189f_dump_on(void);
extern u32 rtl8189f_rf_ch(_adapter *padapter);
extern u32 rtl8189f_rf_ch_reg(_adapter *padapter, u32 reg);

static int rtl8189f_dwell_ms;
module_param_named(dwell_ms, rtl8189f_dwell_ms, int, 0644);
MODULE_PARM_DESC(dwell_ms, "extra dwell on channel 5 during scan (debug)");

static struct ieee80211_channel rtl8189f_channels_2ghz[] = {
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2412, .hw_value = 1 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2417, .hw_value = 2 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2422, .hw_value = 3 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2427, .hw_value = 4 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2432, .hw_value = 5 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2437, .hw_value = 6 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2442, .hw_value = 7 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2447, .hw_value = 8 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2452, .hw_value = 9 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2457, .hw_value = 10 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2462, .hw_value = 11 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2467, .hw_value = 12 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2472, .hw_value = 13 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2484, .hw_value = 14 },
};

static struct ieee80211_rate rtl8189f_rates_2ghz[] = {
	{ .bitrate = 10, .hw_value = 0 },
	{ .bitrate = 20, .hw_value = 1 },
	{ .bitrate = 55, .hw_value = 2 },
	{ .bitrate = 110, .hw_value = 3 },
	{ .bitrate = 60, .hw_value = 4 },
	{ .bitrate = 90, .hw_value = 5 },
	{ .bitrate = 120, .hw_value = 6 },
	{ .bitrate = 180, .hw_value = 7 },
	{ .bitrate = 240, .hw_value = 8 },
	{ .bitrate = 360, .hw_value = 9 },
	{ .bitrate = 480, .hw_value = 10 },
	{ .bitrate = 540, .hw_value = 11 },
};

static struct ieee80211_supported_band rtl8189f_band_2ghz = {
	.channels = rtl8189f_channels_2ghz,
	.n_channels = ARRAY_SIZE(rtl8189f_channels_2ghz),
	.bitrates = rtl8189f_rates_2ghz,
	.n_bitrates = ARRAY_SIZE(rtl8189f_rates_2ghz),
};

extern unsigned long rtl8189f_irq_count_get(void);
extern unsigned long rtl8189f_mgt_count_get(void);
extern unsigned int rtl8189f_local_himr(void);
extern unsigned int rtl8189f_local_hisr(void);

static void rtl8189f_set_channel(_adapter *adapter, u8 channel)
{
	if (channel < 1 || channel > 14)
		channel = 1;
	set_channel_bwmode(adapter, channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE,
	    CHANNEL_WIDTH_20);
	pr_info("rtl8189f: mac80211 channel %u rf0=%05x\n", channel,
	    rtl8189f_rf_ch(adapter));
	if (channel == 5 && rtl8189f_dump_on()) {
		u32 r;

		for (r = 0; r < 0x40; r += 8)
			pr_info("chrf %02x %05x %05x %05x %05x %05x %05x %05x %05x\n",
			    r, rtl8189f_rf_ch_reg(adapter, r + 0),
			    rtl8189f_rf_ch_reg(adapter, r + 1),
			    rtl8189f_rf_ch_reg(adapter, r + 2),
			    rtl8189f_rf_ch_reg(adapter, r + 3),
			    rtl8189f_rf_ch_reg(adapter, r + 4),
			    rtl8189f_rf_ch_reg(adapter, r + 5),
			    rtl8189f_rf_ch_reg(adapter, r + 6),
			    rtl8189f_rf_ch_reg(adapter, r + 7));
	}
}

static int rtl8189f_start(struct ieee80211_hw *hw)
{
	struct rtl8189f_hw *priv = hw->priv;
	_adapter *padapter = priv->adapter;

	/* The vendor starts its command thread when the netdev opens; that
	 * thread is what drains the H2C queue after the HAL init. */
	if (padapter->cmdThread == NULL) {
		padapter->cmdThread = kthread_run(rtw_cmd_thread, padapter,
		    "rtl8189f-cmd");
		if (IS_ERR(padapter->cmdThread)) {
			padapter->cmdThread = NULL;
			pr_err("rtl8189f: cmd thread failed\n");
			return -EIO;
		}
		_rtw_down_sema(&padapter->cmdpriv.start_cmdthread_sema);
		pr_info("rtl8189f: cmd thread running\n");
	}
	{
		extern void rtl8189f_arm_dm(void);

		rtl8189f_arm_dm();
	}
	/* The vendor marks the adapter as up in its netdev open; a large part
	 * of its runtime paths (power save, TSF, LPS control) refuses to run
	 * while this is clear. */
	padapter->bup = _TRUE;
	pr_info("rtl8189f: adapter marked up\n");
	if (rtl8189f_dump_on()) {
		extern void rtl8189f_schedule_idle_dump(void);

		rtl8189f_schedule_idle_dump();
	}
	{
		u8 ps_mode = PS_MODE_ACTIVE;

		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, &ps_mode);
		pr_info("rtl8189f: fw pwr mode sent (%u)\n", ps_mode);
		LeaveAllPowerSaveMode(padapter);
		pr_info("rtl8189f: left power save\n");
	}
	return 0;
}

static void rtl8189f_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct rtl8189f_hw *priv = hw->priv;

	priv->adapter->bup = _FALSE;
}

static int rtl8189f_add_interface(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif)
{
	if (vif->type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;
	return 0;
}

static void rtl8189f_remove_interface(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif)
{
}

static int rtl8189f_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{
	struct rtl8189f_hw *priv = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;

	if ((changed & IEEE80211_CONF_CHANGE_CHANNEL) &&
	    conf->chandef.chan != NULL) {
		rtl8189f_set_channel(priv->adapter, conf->chandef.chan->hw_value);
		if (priv->scanning && !priv->dumped && rtl8189f_dump_on()) {
			priv->dumped = true;
			rtl8189f_dump_mac(priv->adapter);
		}
	}
	if (rtl8189f_force_ch_get() > 0 && !priv->scanning)
		rtl8189f_set_channel(priv->adapter, rtl8189f_force_ch_get());
	return 0;
}

static void rtl8189f_scan_done(struct work_struct *work)
{
	struct rtl8189f_hw *priv = container_of(work, struct rtl8189f_hw,
	    scan_done.work);
	struct cfg80211_scan_info info = { .aborted = false };

	ieee80211_scan_completed(priv->hw, &info);
}

static void rtl8189f_sw_scan_start(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, const u8 *mac_addr)
{
	struct rtl8189f_hw *priv = hw->priv;

	/* RCR_CBSSID_BCN (bit 7) makes the card accept only beacons whose BSSID
	 * matches the configured one; a station that is not associated therefore
	 * hears nothing. The vendor's site-survey clears it through
	 * rtw_hal_rcr_set_chk_bssid(), which does not run on this chip's SDIO
	 * handler, so clear the two CBSSID bits here and restore them when the
	 * scan ends. */
	{
		/* Same two writes the vendor's site-survey path makes:
		 * clear CBSSID_BCN so beacons from any BSS pass, keep
		 * CBSSID_DATA, and close the data filter map. */
		u32 rcr = rtw_read32(priv->adapter, 0x608);

		priv->scan_rcr = rcr;
		priv->scan_flt2 = rtw_read16(priv->adapter, 0x6a4);
		rtw_write32(priv->adapter, 0x608, rcr & ~RCR_CBSSID_BCN);
		rtw_write16(priv->adapter, 0x6a4, 0);
		pr_info("rtl8189f: scan RCR 0x%08x -> 0x%08x flt2 0x%04x\n",
		    rcr, rtw_read32(priv->adapter, 0x608),
		    rtw_read16(priv->adapter, 0x6a4));
	}
	priv->scanning = true;
	rtl8189f_set_channel(priv->adapter, 5);
	if (rtl8189f_dwell_ms) {
		int i;

		/* Keep the vendor's scan-time format and only accept CRC
		 * errors, so a corrupt record no longer makes the handler
		 * drop the whole receive buffer. */
		rtw_write32(priv->adapter, 0x608, 0x7000614e);
		pr_info("rtl8189f: crx bb800=0x%08x c14=0x%08x ca0=0x%08x c50=0x%08x 88c=0x%08x c40=0x%08x d2c=0x%08x s0s1=0x%08x\n",
		    rtw_read32(priv->adapter, 0x800),
		    rtw_read32(priv->adapter, 0xc14),
		    rtw_read32(priv->adapter, 0xca0),
		    rtw_read32(priv->adapter, 0xc50),
		    rtw_read32(priv->adapter, 0x88c),
		    rtw_read32(priv->adapter, 0xc40),
		    rtw_read32(priv->adapter, 0xd2c),
		    rtw_read32(priv->adapter, 0x948));
		for (i = 0; i < rtl8189f_dwell_ms / 50; i++) {
			msleep(50);
			pr_info("rtl8189f: dwell %d hisr=0x%08x rxpkt=0x%08x bb800=0x%08x c14=0x%08x ca0=0x%08x s0s1=0x%08x\n",
			    (i + 1) * 50, rtl8189f_local_hisr(),
			    rtw_read32(priv->adapter, 0x284),
			    rtw_read32(priv->adapter, 0x800),
			    rtw_read32(priv->adapter, 0xc14),
			    rtw_read32(priv->adapter, 0xca0),
			    rtw_read32(priv->adapter, 0x948));
		}
	}
	schedule_delayed_work(&priv->scan_done, msecs_to_jiffies(300));
}

static void rtl8189f_sw_scan_complete(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif)
{
	struct rtl8189f_hw *priv = hw->priv;

	{
		u8 survey = _FALSE;

		rtw_hal_set_hwreg(priv->adapter, HW_VAR_MLME_SITESURVEY, &survey);
		pr_info("rtl8189f: survey off: RCR=0x%08x flt2=0x%04x\n",
		    rtw_read32(priv->adapter, 0x608),
		    rtw_read16(priv->adapter, 0x6a4));
	}
	cancel_delayed_work_sync(&priv->scan_done);
	pr_info("rtl8189f: scan done: irq=%lu mgt=%lu himr=0x%08x hisr=0x%08x\n",
	    rtl8189f_irq_count_get(), rtl8189f_mgt_count_get(),
	    rtl8189f_local_himr(), rtl8189f_local_hisr());
}

static void rtl8189f_tx(struct ieee80211_hw *hw,
    struct ieee80211_tx_control *control, struct sk_buff *skb)
{
	/* The vendor's management frame path is not safe from this context
	 * yet (it panics in softirq); frames are dropped until the transmit
	 * pipeline is moved onto its own context. */
	dev_kfree_skb_any(skb);
}

static void rtl8189f_wake_tx_queue(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq)
{
}

static void rtl8189f_configure_filter(struct ieee80211_hw *hw,
    unsigned int changed_flags, unsigned int *total_flags, u64 multicast)
{
	*total_flags = 0;
}

static const struct ieee80211_ops rtl8189f_ops = {
	.tx			= rtl8189f_tx,
	.start			= rtl8189f_start,
	.stop			= rtl8189f_stop,
	.add_interface		= rtl8189f_add_interface,
	.remove_interface	= rtl8189f_remove_interface,
	.config			= rtl8189f_config,
	.sw_scan_start		= rtl8189f_sw_scan_start,
	.sw_scan_complete	= rtl8189f_sw_scan_complete,
	.configure_filter	= rtl8189f_configure_filter,
	.wake_tx_queue		= rtl8189f_wake_tx_queue,
	.add_chanctx		= ieee80211_emulate_add_chanctx,
	.remove_chanctx		= ieee80211_emulate_remove_chanctx,
	.change_chanctx		= ieee80211_emulate_change_chanctx,
};

static struct ieee80211_hw *rtl8189f_hw;

int rtl8189f_mac80211_register(void)
{
	struct rtl8189f_hw *priv;
	struct ieee80211_hw *hw;
	_adapter *adapter = rtl8189f_adapter_get();
	int ret;

	if (adapter == NULL)
		return -ENODEV;

	hw = ieee80211_alloc_hw(sizeof(*priv), &rtl8189f_ops);
	if (hw == NULL)
		return -ENOMEM;
	priv = hw->priv;
	priv->hw = hw;
	priv->adapter = adapter;
	INIT_DELAYED_WORK(&priv->scan_done, rtl8189f_scan_done);

	SET_IEEE80211_DEV(hw, &adapter_to_dvobj(adapter)->intf_data.func->dev);
	SET_IEEE80211_PERM_ADDR(hw, adapter_mac_addr(adapter));
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	hw->wiphy->bands[NL80211_BAND_2GHZ] = &rtl8189f_band_2ghz;
	hw->queues = 4;
	ieee80211_hw_set(hw, SIGNAL_DBM);

	ret = ieee80211_register_hw(hw);
	if (ret) {
		pr_err("rtl8189f: ieee80211_register_hw failed (%d)\n", ret);
		ieee80211_free_hw(hw);
		return ret;
	}
	rtl8189f_hw = hw;
	pr_info("rtl8189f: mac80211 %s registered, mac %pM\n",
	    wiphy_name(hw->wiphy), adapter_mac_addr(adapter));
	return 0;
}

void *rtl8189f_mac80211_hw(void)
{
	return rtl8189f_hw;
}

void rtl8189f_rx_mgmt(void *hw_v, const u8 *data, int len)
{
	struct ieee80211_hw *hw = hw_v;
	struct sk_buff *skb;

	if (hw == NULL || data == NULL || len <= 0)
		return;
	skb = dev_alloc_skb(len + 2);
	if (skb == NULL)
		return;
	skb_reserve(skb, 2);
	memcpy(skb_put(skb, len), data, len);
	ieee80211_rx_irqsafe(hw, skb);
}

void rtl8189f_mac80211_unregister(void)
{
	struct rtl8189f_hw *priv;

	if (rtl8189f_hw == NULL)
		return;
	priv = rtl8189f_hw->priv;
	cancel_delayed_work_sync(&priv->scan_done);
	ieee80211_unregister_hw(rtl8189f_hw);
	ieee80211_free_hw(rtl8189f_hw);
	rtl8189f_hw = NULL;
}