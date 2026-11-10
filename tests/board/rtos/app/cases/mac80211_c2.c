/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   mac80211_c2.c
 * @brief  Case mac80211_c2: the real mac80211/cfg80211 stack registers a wiphy
 *         on the RTOS line.
 *
 * C2 is the registration gate, not a radio: a synthetic driver provides the
 * eight callbacks ieee80211_alloc_hw() requires and one 2.4 GHz band, then the
 * stack's own initcalls (cfg80211_init, ieee80211_init) run, a wiphy is
 * allocated and registered, and the default STA interface "wlan0" appears in
 * the netdev registry.  No PCIe, no driver, no firmware - those are C3.
 *
 * What a PASS means: the stack's own registration path (wiphy_new ->
 * wiphy_register -> ieee80211_if_add -> cfg80211_register_netdevice) executed
 * on the board without an undefined body and returned 0.  It does not mean a
 * radio exists or that a frame can move.
 *
 * Anchors:
 *   mac80211_c2: core ready
 *   mac80211_c2: initcalls run
 *   mac80211_c2: alloc hw=<ptr>
 *   mac80211_c2: register rc=<n> wiphy=<name>
 *   mac80211_c2: netdev <name> present=<0|1>
 *   mac80211_c2: PASS
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"

#include <linux/etherdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

/* The synthetic device's address: locally administered, fixed, and distinct
 * from the board's wired MACs (02:.. is the same shape the mock cases use). */
static const u8 c2_mac[ETH_ALEN] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0xc2 };

static void
c2_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
      struct sk_buff *skb)
{
	(void)hw;
	(void)control;
	/* no radio: the frame is consumed, never queued to a bus */
	if (skb != NULL)
		kfree_skb(skb);
}

static int
c2_start(struct ieee80211_hw *hw)
{
	(void)hw;
	return 0;
}

static void
c2_stop(struct ieee80211_hw *hw, bool suspend)
{
	(void)hw;
	(void)suspend;
}

static int
c2_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{
	(void)hw;
	(void)radio_idx;
	(void)changed;
	return 0;
}

static int
c2_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	(void)hw;
	(void)vif;
	return 0;
}

static void
c2_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	(void)hw;
	(void)vif;
}

static void
c2_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
		    unsigned int *total_flags, u64 multicast)
{
	(void)hw;
	(void)changed_flags;
	(void)multicast;
	/* accept whatever the stack asks for; there is no hardware filter */
	*total_flags = 0;
}

static void
c2_wake_tx_queue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	(void)hw;
	(void)txq;
}

/*
 * The channel-context trio is the interface's emulate mode (mac80211.h:8204):
 * with all three set to the emulate helpers, add_interface/remove_interface
 * work without a driver-managed channel context.  A driver that sets none of
 * them must set all five real callbacks; this case takes the smaller arm.
 */
static const struct ieee80211_ops c2_ops = {
	.tx = c2_tx,
	.start = c2_start,
	.stop = c2_stop,
	.config = c2_config,
	.add_interface = c2_add_interface,
	.remove_interface = c2_remove_interface,
	.configure_filter = c2_configure_filter,
	.wake_tx_queue = c2_wake_tx_queue,
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
};

static struct ieee80211_channel c2_chan = {
	.band = NL80211_BAND_2GHZ,
	.center_freq = 2412,
	.hw_value = 1,
	.max_power = 20,
};

static struct ieee80211_rate c2_rates[] = {
	{ .bitrate = 10,  .hw_value = 0 },
	{ .bitrate = 20,  .hw_value = 1 },
	{ .bitrate = 55,  .hw_value = 2 },
	{ .bitrate = 110, .hw_value = 3 },
};

static struct ieee80211_supported_band c2_band_2ghz = {
	.band = NL80211_BAND_2GHZ,
	.channels = &c2_chan,
	.n_channels = 1,
	.bitrates = c2_rates,
	.n_bitrates = 4,
};

int
case_mac80211_c2_run(void)
{
	struct ieee80211_hw *hw;
	struct net_device *dev = NULL;
	const char *name;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("mac80211_c2: core ready\n");

	/*
	 * The stack's own initcalls: cfg80211_init (wiphy class, pernet,
	 * nl80211 family) and ieee80211_init (rate control, netdev notifier)
	 * land in .lkpi_init via fs_initcall/subsys_initcall and are walked
	 * here, in link order (wireless/ before mac80211/).
	 */
	lkpi_module_init_run();
	CASE_ANCHOR("mac80211_c2: initcalls run\n");

	hw = ieee80211_alloc_hw(0, &c2_ops);
	CASE_ANCHOR("mac80211_c2: alloc hw=%p\n", (void *)hw);
	if (hw == NULL) {
		CASE_ANCHOR("mac80211_c2: FAIL alloc_hw returned NULL\n");
		return 1;
	}

	hw->wiphy->bands[NL80211_BAND_2GHZ] = &c2_band_2ghz;
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	hw->queues = IEEE80211_NUM_ACS;
	hw->max_rates = 4;
	hw->max_rate_tries = 8;
	ether_addr_copy(hw->wiphy->perm_addr, c2_mac);

	CASE_ANCHOR("mac80211_c2: register begin\n");
	rc = ieee80211_register_hw(hw);
	name = (hw->wiphy->dev.name != NULL) ? hw->wiphy->dev.name : "?";
	CASE_ANCHOR("mac80211_c2: register rc=%d wiphy=%s\n", rc, name);
	if (rc != 0) {
		CASE_ANCHOR("mac80211_c2: FAIL register_hw rc=%d\n", rc);
		return 2;
	}

	/*
	 * The default STA interface is the stack's own proof that
	 * ieee80211_register_hw() completed: it creates "wlan%d" through
	 * alloc_netdev_mqs() and registers it through
	 * cfg80211_register_netdevice().  The registry is the one the
	 * component's netdev bodies own.
	 */
	dev = dev_get_by_name(&init_net, "wlan0");
	CASE_ANCHOR("mac80211_c2: netdev wlan0 present=%d\n", dev != NULL);
	if (dev == NULL) {
		CASE_ANCHOR("mac80211_c2: FAIL default interface missing\n");
		return 3;
	}

	CASE_ANCHOR("mac80211_c2: PASS\n");
	return 0;
}
