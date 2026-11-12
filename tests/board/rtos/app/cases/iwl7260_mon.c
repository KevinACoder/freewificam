/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_mon.c
 * @brief  Case iwl7260_mon: the AC 7260 becomes a monitor (RX-only) and the
 *         port's sink counts what arrives (docs/HANDOFF-AC7260-MONITOR.md).
 *
 * The bring-up is the AP case's C3: core, firmware registration, initcalls and
 * the PCI bus, then `wlan0`.  What changes is the role:
 *
 *   down -> SET_INTERFACE(MONITOR) -> up -> SET_WIPHY(freq) -> 15 s window
 *
 * The order is cfg80211's, not a choice, and it is the same order `iw` and the
 * GT arm use: cfg80211_set_monitor_channel() refuses with -EBUSY unless the
 * wiphy has monitors only and at least one of them is running
 * (stack/wireless/chan.c:1818-1823, core.h:cfg80211_has_monitors_only), so the
 * interface has to be up before its channel can be set.  Setting it while down
 * was the first attempt and returned -EBUSY; that is this rule, not a driver
 * fault.  The monitor's virtual sdata is created by the open
 * (iface.c:1240-1285) and the channel request then reaches the driver as a
 * channel switch on it.
 *
 * The frames land in the port's monitor sink (components/liblinux80211/
 * linuxkpi_netdev.c:netif_rx), not in lwIP: a monitor skb's dev->type is
 * ARPHRD_IEEE80211_RADIOTAP and it has no ethernet header.  The case reads the
 * sink's counters, the lwIP adapter's counters before/after the window, and the
 * device's tx counters - the three facts M2, M3 and M4 are made of.
 *
 * Anchors:
 *   iwl7260_mon: core ready
 *   iwl7260_mon: fw registered=<0|err>
 *   iwl7260_mon: pci devs=<n>
 *   iwl7260_mon: wlan0 present=1 ifindex=<n> mac=<mac> iftype=<n>
 *   iwl7260_mon: sink enabled=<0|1>
 *   iwl7260_mon: set_iface rc=<n> type=<n>
 *   iwl7260_mon: set_freq rc=<n> freq=<mhz>
 *   iwl7260_mon: start rc=<n> netif_running=<0|1> devtype=<n>
 *   iwl7260_mon: window ms=<n>
 *   iwl7260_mon: sample t=<ms> frames=<n> beacons=<n> data=<n>
 *   iwl7260_mon: sink frames=<n> bytes=<n> beacons=<n> probes=<n> probe_resp=<n> auth=<n> assoc=<n> data=<n> ctrl=<n> other=<n>
 *   iwl7260_mon: radiotap it_len=<min>/<max> freq=<mhz> bad=<n> short=<n>
 *   iwl7260_mon: freq <i> mhz=<mhz> frames=<n>
 *   iwl7260_mon: frame <i> len=<n> it_len=<n> fc=0x<hex> hex=<64B hex>
 *   iwl7260_mon: lwip in rx_frames=<n> rx_foreign=<n> tx_frames=<n>
 *   iwl7260_mon: lwip out rx_frames=<n> rx_foreign=<n> tx_frames=<n>
 *   iwl7260_mon: tx dev tx_packets=<n> tx_bytes=<n>
 *   iwl7260_mon: PASS MON
 *   iwl7260_mon: FAIL <reason>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"
#include "linuxkpi_pci.h"
#include "adapter.h"

#include <linux/etherdevice.h>
#include <linux/nl80211.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#ifndef CONFIG_MON_FREQ
#define CONFIG_MON_FREQ		2432
#endif
#ifndef CONFIG_MON_WINDOW_MS
#define CONFIG_MON_WINDOW_MS	15000
#endif

#define MON_FREQ_MHZ		((unsigned int)CONFIG_MON_FREQ)
#define MON_WINDOW_MS		((unsigned int)CONFIG_MON_WINDOW_MS)
#define MON_WAIT_MS		40000U
#define MON_POLL_MS		200U
#define MON_SAMPLE_MS		1000U
/* The pre-registered floor: 8 beacons/s (GT measured 9.25/s on 2026-11-12),
 * so a window that is shorter than a second still has one sample. */
#define MON_BEACON_PER_S	8U
#define MON_LOG_MAX		224U

/* The two port entry points the C3/AP cases also declare locally: the log
 * sink and the compiled-in firmware registration. */
void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
int lkpi_fw_iwl7260_fw_register(void);

static unsigned mon_log_lines;

/* The driver's own log lines, tagged so the serial record can be read without
 * guessing which case produced them.  Bounded: a monitor boot's listener and
 * snif commands are the evidence, and a busy receive path must not drown the
 * console. */
static void
mon_sink(ulog_level_t level, const char *line)
{

	(void)level;
	if (line == NULL || mon_log_lines >= MON_LOG_MAX)
		return;
	mon_log_lines++;
	CASE_ANCHOR("iwl7260_mon: fw %s\n", line);
}

int
case_iwl7260_mon_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct ieee80211_channel *chan;
	struct lkpi_net_diag net_in, net_out;
	struct lkpi_net_mon_diag mon;
	unsigned char mac[6];
	unsigned int waited;
	unsigned int elapsed;
	unsigned int i;
	unsigned int scale;
	int rc;
	int beacon_ok;

	lkpi_core_init();
	CASE_ANCHOR("iwl7260_mon: core ready\n");
	lkpi_log_sink_register(mon_sink);

	lkpi_pcie_port_install();

	rc = lkpi_fw_iwl7260_fw_register();
	CASE_ANCHOR("iwl7260_mon: fw registered=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_mon: FAIL firmware registration rc=%d\n", rc);
		return 1;
	}

	lkpi_module_init_run();
	CASE_ANCHOR("iwl7260_mon: initcalls run\n");

	lkpi_pci_start();
	CASE_ANCHOR("iwl7260_mon: pci devs=%u\n",
		    (unsigned)lkpi_pci_dev_count_get());

	for (waited = 0U; waited < MON_WAIT_MS; waited += MON_POLL_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(MON_POLL_MS);
	}
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_mon: FAIL no wlan0 after %u ms\n", waited);
		return 2;
	}
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("iwl7260_mon: FAIL no wireless_dev\n");
		return 3;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("iwl7260_mon: wlan0 present=1 ifindex=%d "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x iftype=%d\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		    (int)wdev->iftype);

	/* The channel must exist in the wiphy before the type change: a firmware
	 * that cannot tune there would fail later with a driver error instead of
	 * a case line, and the two are different verdicts (R7). */
	chan = ieee80211_get_channel(wdev->wiphy, (int)MON_FREQ_MHZ);
	if (chan == NULL) {
		CASE_ANCHOR("iwl7260_mon: FAIL channel %u not in wiphy\n",
			    MON_FREQ_MHZ);
		return 4;
	}

	/* --- the sink is armed before the interface opens ---------------- */
	(void)lkpi_net_monitor_enable(0);
	lkpi_net_monitor_reset();
	(void)lkpi_net_monitor_enable(1);
	/* The line is the state *after* arming: printing the call's return value
	 * (the previous state, 0) made the judge's "sink enabled" check fail on
	 * an image that had just enabled it - 2026-11-12 mon1/mon2. */
	CASE_ANCHOR("iwl7260_mon: sink enabled=%d\n",
		    lkpi_net_monitor_enabled());
	lkpi_net_diag(&net_in);

	if (netif_running(dev))
		(void)dev_close(dev);
	rc = lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_MONITOR);
	CASE_ANCHOR("iwl7260_mon: set_iface rc=%d type=%d\n", rc,
		    lkpi_nl80211_iftype(dev));
	if (rc != 0 || lkpi_nl80211_iftype(dev) != NL80211_IFTYPE_MONITOR) {
		CASE_ANCHOR("iwl7260_mon: FAIL set_iface rc=%d type=%d\n", rc,
			    lkpi_nl80211_iftype(dev));
		return 5;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("iwl7260_mon: start rc=%d netif_running=%d devtype=%d\n",
		    rc, netif_running(dev) ? 1 : 0, (int)dev->type);
	if (rc != 0 || !netif_running(dev)) {
		CASE_ANCHOR("iwl7260_mon: FAIL dev_open rc=%d\n", rc);
		return 6;
	}

	rc = lkpi_nl80211_set_freq(dev, MON_FREQ_MHZ,
				   NL80211_CHAN_WIDTH_20_NOHT);
	CASE_ANCHOR("iwl7260_mon: set_freq rc=%d freq=%u\n", rc, MON_FREQ_MHZ);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_mon: FAIL set_freq rc=%d freq=%u\n", rc,
			    MON_FREQ_MHZ);
		return 7;
	}

	/* --- the capture window ------------------------------------------ */
	CASE_ANCHOR("iwl7260_mon: window ms=%u\n", MON_WINDOW_MS);
	for (elapsed = 0U; elapsed < MON_WINDOW_MS; elapsed += MON_SAMPLE_MS) {
		osDelay(MON_SAMPLE_MS);
		lkpi_net_monitor_diag(&mon);
		CASE_ANCHOR("iwl7260_mon: sample t=%u frames=%lu beacons=%lu "
			    "data=%lu\n", elapsed + MON_SAMPLE_MS,
			    mon.frames, mon.beacons, mon.data);
	}

	lkpi_net_monitor_diag(&mon);
	CASE_ANCHOR("iwl7260_mon: sink frames=%lu bytes=%lu beacons=%lu "
		    "probes=%lu probe_resp=%lu auth=%lu assoc=%lu data=%lu "
		    "ctrl=%lu other=%lu\n",
		    mon.frames, mon.bytes, mon.beacons, mon.probes,
		    mon.probe_resp, mon.auth, mon.assoc, mon.data, mon.ctrl,
		    mon.other);
	CASE_ANCHOR("iwl7260_mon: radiotap it_len=%u/%u freq=%u bad=%lu "
		    "short=%lu\n",
		    mon.it_len_min, mon.it_len_max, mon.freq_mhz,
		    mon.bad_radiotap, mon.too_short);
	{
		unsigned int f;

		for (f = 0U; f < LKPI_NET_MON_FREQS; f++) {
			if (mon.freq[f].freq == 0U)
				continue;
			CASE_ANCHOR("iwl7260_mon: freq %u mhz=%u frames=%lu\n",
				    f, mon.freq[f].freq, mon.freq[f].count);
		}
	}
	for (i = 0U; i < mon.kept; i++) {
		const struct lkpi_net_mon_frame *k = &mon.keep[i];

		CASE_ANCHOR("iwl7260_mon: frame %u len=%u it_len=%u fc=0x%04x "
			    "hex=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
			    "%02x%02x%02x%02x%02x%02x\n",
			    i, k->len, k->it_len, k->fc,
			    k->head[0], k->head[1], k->head[2], k->head[3],
			    k->head[4], k->head[5], k->head[6], k->head[7],
			    k->head[8], k->head[9], k->head[10], k->head[11],
			    k->head[12], k->head[13], k->head[14],
			    k->head[15]);
	}

	/* --- M4: the monitor frames did not become lwIP's ----------------
	 * This image carries CONFIG_LIBLINUX80211_NET_LWIP (the adapter object is
	 * linked) but no netif is brought up in a monitor boot, so the adapter's
	 * counters are the witness that the sink and not lwIP took the frames.
	 * The device's tx counters come from the netdev stats mac80211 fills. */
	lkpi_net_diag(&net_out);
	CASE_ANCHOR("iwl7260_mon: lwip in rx_frames=%lu rx_foreign=%lu "
		    "tx_frames=%lu\n", net_in.rx_frames, net_in.rx_foreign,
		    net_in.tx_frames);
	CASE_ANCHOR("iwl7260_mon: lwip out rx_frames=%lu rx_foreign=%lu "
		    "tx_frames=%lu\n", net_out.rx_frames, net_out.rx_foreign,
		    net_out.tx_frames);
	CASE_ANCHOR("iwl7260_mon: tx dev tx_packets=%lu tx_bytes=%lu\n",
		    dev->stats.tx_packets, dev->stats.tx_bytes);

	/*
	 * The verdict is M2's floor only.  M3's radiotap and M4's pollution are
	 * printed and judged by the harness against the GT arm: the case does not
	 * promote a counter into a claim ("received beacons" is not "monitor
	 * supported"), it states the session's facts and lets the round's
	 * pre-registered table decide.
	 */
	/* beacons * 1000 >= 8 * window_ms, without a 64-bit divide. */
	scale = MON_WINDOW_MS / 1000U;
	if (scale == 0U)
		scale = 1U;
	beacon_ok = mon.beacons >= (unsigned long)(MON_BEACON_PER_S * scale);
	if (!beacon_ok) {
		CASE_ANCHOR("iwl7260_mon: FAIL beacons=%lu below %u/s over %u ms\n",
			    mon.beacons, MON_BEACON_PER_S, MON_WINDOW_MS);
		return 8;
	}
	if (mon.bad_radiotap != 0UL) {
		CASE_ANCHOR("iwl7260_mon: FAIL radiotap bad=%lu\n",
			    mon.bad_radiotap);
		return 9;
	}
	if (net_out.rx_frames != net_in.rx_frames) {
		CASE_ANCHOR("iwl7260_mon: FAIL monitor frames reached lwIP "
			    "(%lu -> %lu)\n", net_in.rx_frames,
			    net_out.rx_frames);
		return 10;
	}
	if (dev->stats.tx_packets != 0UL) {
		CASE_ANCHOR("iwl7260_mon: FAIL tx while monitoring (%lu)\n",
			    dev->stats.tx_packets);
		return 11;
	}
	CASE_ANCHOR("iwl7260_mon: PASS MON\n");
	return 0;
}
