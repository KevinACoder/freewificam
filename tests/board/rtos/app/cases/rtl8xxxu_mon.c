/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8xxxu_mon.c
 * @brief  Case rtl8xxxu_mon: the RTL8188EUS (rtl8xxxu, USB) becomes an
 *         RX-only monitor on the bench AP's channel, and the port's sink
 *         counts what the chip hands up.
 *
 * The bring-up is the one rtl8xxxu_c3 proved: linuxkpi core, the URB engine
 * and CherryUSB on the xHCI pair, the compiled-in rtlwifi/rtl8188eufw.bin
 * (Firmware revision 28.0), `wlan0`.  What changes is the role:
 *
 *   sink -> SET_INTERFACE(MONITOR) -> open -> SET_WIPHY(2432, 20_NOHT) -> 15 s
 *
 * The order is cfg80211's, not a choice: cfg80211_set_monitor_channel()
 * refuses with -EBUSY unless the wiphy has monitors only and at least one of
 * them is running (stack/wireless/chan.c:1818-1823, core.h:
 * cfg80211_has_monitors_only), so the interface has to be up before its
 * channel can be set.  The monitor's virtual sdata is created by the open
 * (stack/mac80211/iface.c:1240-1285); that is also where the driver is told
 * (WANT_MONITOR_VIF -> add_interface(MONITOR), patch 0004), which is when the
 * promiscuous RCR is programmed.
 *
 * The frames land in the port's monitor sink (components/liblinux80211/
 * linuxkpi_netdev.c:netif_rx), not in lwIP: a monitor skb's dev->type is
 * ARPHRD_IEEE80211_RADIOTAP and it has no ethernet header.  The case reads the
 * sink's counters, the lwIP adapter's counters before/after the window, and
 * the netdev's tx counters - M2, M3 and M4 are made of those.
 *
 * Nothing about the role is specific to the 8188E except the patch: mac80211
 * builds the radiotap header at ieee80211_rx_monitor(), and a monitor interface
 * is the driver's third mode on this line (the station and AP rounds are the
 * other two).  This is the card's first monitor arm, and the image carries the
 * driver's fourth recorded patch (`supports_monitor`, see
 * wireless-drivers/rtl8xxxu/patches/README.md).
 *
 * Anchors:
 *   rtl8xxxu_mon: core ready
 *   rtl8xxxu_mon: usb backend ready
 *   rtl8xxxu_mon: initcalls run
 *   rtl8xxxu_mon: usb drivers=<n>
 *   rtl8xxxu_mon: firmware <driver line>
 *   rtl8xxxu_mon: fw image <driver line>
 *   rtl8xxxu_mon: fw registered=0 name=<profile blob>
 *   rtl8xxxu_mon: usb buses=3,2
 *   rtl8xxxu_mon: usb bus=<n> hcd=<xhci|ehci> base=0x<hex> started
 *   rtl8xxxu_mon: wlan0 present=1 ifindex=<n> mac=<mac> iftype=<n>
 *   rtl8xxxu_mon: chan freq=<mhz> present=<0|1> flags=0x<hex>
 *   rtl8xxxu_mon: sink enabled=<0|1>
 *   rtl8xxxu_mon: set_iface rc=<n> type=<n>
 *   rtl8xxxu_mon: start rc=<n> netif_running=<0|1> devtype=<n>
 *   rtl8xxxu_mon: set_freq rc=<n> freq=<mhz>
 *   rtl8xxxu_mon: window ms=<n>
 *   rtl8xxxu_mon: sample t=<ms> frames=<n> beacons=<n> data=<n>
 *   rtl8xxxu_mon: sink frames=<n> bytes=<n> beacons=<n> probes=<n>
 *                 probe_resp=<n> auth=<n> assoc=<n> data=<n> ctrl=<n>
 *                 other=<n>
 *   rtl8xxxu_mon: radiotap it_len=<min>/<max> freq=<mhz> bad=<n> short=<n>
 *   rtl8xxxu_mon: freq <i> mhz=<mhz> frames=<n>
 *   rtl8xxxu_mon: bssbeacons=<mac> beacons=<n>
 *   rtl8xxxu_mon: bsscensus slots=<n>/<max> full=<n>
 *   rtl8xxxu_mon: frame <i> len=<n> it_len=<n> fc=0x<hex> hdr=<hex>
 *   rtl8xxxu_mon: bssid=<mac> first_frame=<i>   (a kept-frame sample, not
 *                                                necessarily the bench AP)
 *   rtl8xxxu_mon: lwip in rx_frames=<n> rx_foreign=<n> tx_frames=<n>
 *   rtl8xxxu_mon: lwip out rx_frames=<n> rx_foreign=<n> tx_frames=<n>
 *   rtl8xxxu_mon: tx dev tx_packets=<n> tx_bytes=<n>
 *   rtl8xxxu_mon: usb submits=<n> completes=<n> refused=<n>
 *                 stage_overflow=<n> slots_exhausted=<n> rx=<n> rx_err=<n>
 *   rtl8xxxu_mon: PASS MON
 *   rtl8xxxu_mon: FAIL <reason>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cases.h"
#include "rtl8xxxu_fw.h"
#include "os/cmsis2/cmsis2.h"

/* CherryUSB first (see rtl8xxxu_c3.c): its <stdlib.h> must be parsed before
 * the Linux face's compiler-attributes header is. */
#include "usbh_core.h"
#include "usb_glue.h"
#include "usb_domain.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"
#include "linuxkpi_usb.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <linux/nl80211.h>
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
#define MON_WAIT_MS		60000U
#define MON_POLL_MS		100U
#define MON_SAMPLE_MS		1000U
/* The pre-registered floor: 8 beacons/s, the same floor the 7260 and 8851BU
 * monitor verdicts use.  The bench AP advertises 100 TU (about 9.77/s). */
#define MON_BEACON_PER_S	8U
#define MON_LOG_MAX		120U
/* The 802.11 header the case reads out of a kept frame: FC, duration, and the
 * three addresses (BSSID is addr3 for beacons).  22 bytes is enough, 24 keeps
 * it on a tidy hex line. */
#define MON_HDR_BYTES		24U
#define MON_HDR_HEX		(MON_HDR_BYTES * 2U + 1U)

#if CONFIG_RTL8XXXU_USB_BUS == 0
static const unsigned int mon_buses[] = { 3U, 2U };
#define MON_BUS_LIST_TXT	"3,2"
#else
static const unsigned int mon_buses[] = { 1U, 0U };
#define MON_BUS_LIST_TXT	"1,0"
#endif

/* The port entry points the other rtl8xxxu cases also declare locally. */
void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);
void usb_osal_timer_service_start(void);

static unsigned int mon_log_lines;

/* The driver's own lines, tagged and bounded: the firmware revision is the
 * C3/M-line evidence, and the rest is observation.  A monitor boot's receive
 * path must not drown the console that carries the anchors. */
static void
mon_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	if (strstr(line, "Firmware revision") != NULL) {
		CASE_ANCHOR("rtl8xxxu_mon: firmware %s\n", line);
		return;
	}
	if (strstr(line, "Loading firmware") != NULL) {
		CASE_ANCHOR("rtl8xxxu_mon: fw image %s\n", line);
		return;
	}
	if (mon_log_lines >= MON_LOG_MAX)
		return;
	if (strstr(line, "failed to") != NULL ||
	    strstr(line, "error") != NULL ||
	    strstr(line, "timeout") != NULL) {
		CASE_INFO("rtl8xxxu_mon: drv %s\n", line);
		mon_log_lines++;
	}
}

static void
mon_usb_ledger(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtl8xxxu_mon: usb submits=%u completes=%u refused=%u "
		    "stage_overflow=%u slots_exhausted=%u rx=%u rx_err=%u\n",
		    st.submits, st.completed, st.refused, st.stage_overflow,
		    st.slots_exhausted, st.rx_completes, st.rx_errors);
}

/* The shared bring-up of every rtl8xxxu case, up to the controller loop.
 * Returns the number of controllers that started. */
static int
mon_usb_up(void)
{
	unsigned int i;
	int started = 0;

	lkpi_usb_cherryusb_init();
	CASE_ANCHOR("rtl8xxxu_mon: usb backend ready\n");

	lkpi_module_init_run();
	CASE_ANCHOR("rtl8xxxu_mon: initcalls run\n");

	lkpi_usb_init();
	CASE_ANCHOR("rtl8xxxu_mon: usb drivers=%u\n",
		    (unsigned)lkpi_usb_driver_count_get());

	lkpi_usb_force_sync_set(false);
	if (rtl8xxxu_fw_register() != 0) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL firmware blob registration\n");
		return -1;
	}
	CASE_ANCHOR("rtl8xxxu_mon: fw registered=0 name=%s\n", RTL8XXXU_FW_NAME);

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtl8xxxu_mon: usb buses=%s\n", MON_BUS_LIST_TXT);
	for (i = 0U; i < sizeof(mon_buses) / sizeof(mon_buses[0]); i++) {
		unsigned int bus = mon_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtl8xxxu_mon: bus%u board layer failed\n",
				  bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtl8xxxu_mon: bus%u usbh_initialize failed\n",
				  bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtl8xxxu_mon: usb bus=%u hcd=%s base=0x%lx "
			    "started\n", bus,
			    usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
			    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	return started;
}

/* 802.11 header bytes of a kept frame, as hex, starting where radiotap ends.
 * Returns the number of bytes written to the frame (0 when the kept copy does
 * not reach the header). */
static unsigned int
mon_frame_hdr_hex(const struct lkpi_net_mon_frame *k, char *out,
		  unsigned int out_len)
{
	unsigned int off = k->it_len;
	unsigned int n = 0U;
	unsigned int i;

	if (off < 8U || off >= k->len)
		return 0U;
	if (off >= LKPI_NET_MON_KEEP_BYTES)
		return 0U;
	n = LKPI_NET_MON_KEEP_BYTES - off;
	if (n > MON_HDR_BYTES)
		n = MON_HDR_BYTES;
	if (n > (k->len - off))
		n = k->len - off;
	if (out_len < n * 2U + 1U)
		n = (out_len - 1U) / 2U;
	for (i = 0U; i < n; i++)
		snprintf(&out[i * 2U], 3U, "%02x", k->head[off + i]);
	out[n * 2U] = '\0';
	return n;
}

/* addr3 of a kept beacon/probe-response frame: the BSSID. */
static int
mon_frame_bssid(const struct lkpi_net_mon_frame *k, unsigned char bssid[6])
{
	unsigned int off = k->it_len;

	if (off < 8U || off >= LKPI_NET_MON_KEEP_BYTES)
		return 0;
	if (off + 22U > LKPI_NET_MON_KEEP_BYTES)
		return 0;
	memcpy(bssid, &k->head[off + 16U], 6U);
	return 1;
}

int
case_rtl8xxxu_mon_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct ieee80211_channel *chan;
	struct lkpi_net_diag net_in, net_out;
	struct lkpi_net_mon_diag mon;
	unsigned char mac[6];
	unsigned char bssid[6];
	unsigned int bssid_beacons = 0U;
	unsigned int waited = 0U;
	unsigned int elapsed;
	unsigned int i;
	unsigned int scale;
	int rc;
	int beacon_ok;

	lkpi_core_init();
	CASE_ANCHOR("rtl8xxxu_mon: core ready\n");
	lkpi_log_sink_register(mon_sink);

	rc = mon_usb_up();
	if (rc <= 0) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL no controller started\n");
		mon_usb_ledger();
		return 1;
	}

	while (waited < MON_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(MON_POLL_MS);
		waited += MON_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL no wlan0 after %u ms\n",
			    (unsigned)waited);
		mon_usb_ledger();
		return 2;
	}
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL no wireless_dev\n");
		return 3;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("rtl8xxxu_mon: wlan0 present=1 ifindex=%d "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x iftype=%d\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		    (int)wdev->iftype);

	/* The channel must exist in the wiphy before the type change: a driver
	 * that cannot tune there would fail later with a driver error instead
	 * of a case line, and the two are different verdicts. */
	chan = ieee80211_get_channel(wdev->wiphy, (int)MON_FREQ_MHZ);
	CASE_ANCHOR("rtl8xxxu_mon: chan freq=%u present=%d flags=0x%08x\n",
		    MON_FREQ_MHZ, chan != NULL ? 1 : 0,
		    chan != NULL ? (unsigned)chan->flags : 0u);
	if (chan == NULL) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL channel %u not in wiphy\n",
			    MON_FREQ_MHZ);
		return 4;
	}

	/* --- the sink is armed before the interface opens ---------------- */
	(void)lkpi_net_monitor_enable(0);
	lkpi_net_monitor_reset();
	(void)lkpi_net_monitor_enable(1);
	/* The line is the state *after* arming: printing the call's return value
	 * would print the previous state, not this one. */
	CASE_ANCHOR("rtl8xxxu_mon: sink enabled=%d\n",
		    lkpi_net_monitor_enabled());
	lkpi_net_diag(&net_in);

	if (netif_running(dev))
		(void)dev_close(dev);
	rc = lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_MONITOR);
	CASE_ANCHOR("rtl8xxxu_mon: set_iface rc=%d type=%d\n", rc,
		    lkpi_nl80211_iftype(dev));
	if (rc != 0 || lkpi_nl80211_iftype(dev) != NL80211_IFTYPE_MONITOR) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL set_iface rc=%d type=%d\n", rc,
			    lkpi_nl80211_iftype(dev));
		return 5;
	}

	/* Opening the monitor is where mac80211 creates the virtual monitor and
	 * calls the driver's add_interface(MONITOR) (WANT_MONITOR_VIF): the
	 * promiscuous RCR is programmed inside this call. */
	rc = dev_open(dev);
	CASE_ANCHOR("rtl8xxxu_mon: start rc=%d netif_running=%d devtype=%d\n",
		    rc, netif_running(dev) ? 1 : 0, (int)dev->type);
	if (rc != 0 || !netif_running(dev)) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL dev_open rc=%d\n", rc);
		return 6;
	}

	rc = lkpi_nl80211_set_freq(dev, MON_FREQ_MHZ,
				   NL80211_CHAN_WIDTH_20_NOHT);
	CASE_ANCHOR("rtl8xxxu_mon: set_freq rc=%d freq=%u\n", rc, MON_FREQ_MHZ);
	if (rc != 0) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL set_freq rc=%d freq=%u\n", rc,
			    MON_FREQ_MHZ);
		return 7;
	}

	/* --- the capture window ------------------------------------------ */
	CASE_ANCHOR("rtl8xxxu_mon: window ms=%u\n", MON_WINDOW_MS);
	for (elapsed = 0U; elapsed < MON_WINDOW_MS; elapsed += MON_SAMPLE_MS) {
		osDelay(MON_SAMPLE_MS);
		lkpi_net_monitor_diag(&mon);
		CASE_ANCHOR("rtl8xxxu_mon: sample t=%u frames=%lu beacons=%lu "
			    "data=%lu\n", elapsed + MON_SAMPLE_MS,
			    mon.frames, mon.beacons, mon.data);
	}

	lkpi_net_monitor_diag(&mon);
	CASE_ANCHOR("rtl8xxxu_mon: sink frames=%lu bytes=%lu beacons=%lu "
		    "probes=%lu probe_resp=%lu auth=%lu assoc=%lu data=%lu "
		    "ctrl=%lu other=%lu\n",
		    mon.frames, mon.bytes, mon.beacons, mon.probes,
		    mon.probe_resp, mon.auth, mon.assoc, mon.data, mon.ctrl,
		    mon.other);
	CASE_ANCHOR("rtl8xxxu_mon: radiotap it_len=%u/%u freq=%u bad=%lu "
		    "short=%lu\n",
		    mon.it_len_min, mon.it_len_max, mon.freq_mhz,
		    mon.bad_radiotap, mon.too_short);
	{
		unsigned int f;

		for (f = 0U; f < LKPI_NET_MON_FREQS; f++) {
			if (mon.freq[f].freq == 0U)
				continue;
			CASE_ANCHOR("rtl8xxxu_mon: freq %u mhz=%u frames=%lu\n",
				    f, mon.freq[f].freq, mon.freq[f].count);
		}
	}

	/* The per-BSSID beacon census: the total above counts the whole channel,
	 * and ch5 carries neighbours, so this is the table that says what *the
	 * bench AP* did.  The A/B arm's rate comparison uses it. */
	for (i = 0U; i < LKPI_NET_MON_BSS; i++) {
		if (mon.bss[i].beacons == 0UL)
			continue;
		CASE_ANCHOR("rtl8xxxu_mon: bssbeacons="
			    "%02x:%02x:%02x:%02x:%02x:%02x beacons=%lu\n",
			    mon.bss[i].bssid[0], mon.bss[i].bssid[1],
			    mon.bss[i].bssid[2], mon.bss[i].bssid[3],
			    mon.bss[i].bssid[4], mon.bss[i].bssid[5],
			    mon.bss[i].beacons);
	}
	CASE_ANCHOR("rtl8xxxu_mon: bsscensus slots=%u/%u full=%lu\n",
		    mon.bss_kept, (unsigned int)LKPI_NET_MON_BSS, mon.bss_full);

	/* The kept frames: the hex starts at the 802.11 header (radiotap ends at
	 * it_len), so the BSSID and the FC are readable from the line itself.
	 * For a beacon, addr3 (bytes 16..21) is the BSSID; the case names it
	 * once, from the first kept beacon, and counts how many kept beacons
	 * carry it - the bench AP's identity is what M3 is made of. */
	for (i = 0U; i < mon.kept; i++) {
		const struct lkpi_net_mon_frame *k = &mon.keep[i];
		char hdr[MON_HDR_HEX];
		unsigned int n;

		n = mon_frame_hdr_hex(k, hdr, sizeof(hdr));
		CASE_ANCHOR("rtl8xxxu_mon: frame %u len=%u it_len=%u "
			    "fc=0x%04x hdr=%s\n",
			    i, k->len, k->it_len, k->fc,
			    n != 0U ? hdr : "-");
		if (((k->fc >> 2) & 3u) == 0u && ((k->fc >> 4) & 0xfu) == 8u &&
		    mon_frame_bssid(k, bssid)) {
			bssid_beacons++;
			if (bssid_beacons == 1U) {
				CASE_ANCHOR("rtl8xxxu_mon: bssid="
					    "%02x:%02x:%02x:%02x:%02x:%02x "
					    "first_frame=%u\n",
					    bssid[0], bssid[1], bssid[2],
					    bssid[3], bssid[4], bssid[5], i);
			}
		}
	}
	if (bssid_beacons == 0U)
		CASE_ANCHOR("rtl8xxxu_mon: bssid=none (no kept beacon "
			    "frame long enough for addr3)\n");

	/* --- M4: the monitor frames did not become lwIP's ----------------
	 * This image carries CONFIG_LIBLINUX80211_NET_LWIP (the adapter object
	 * is linked) but no netif is brought up in a monitor boot, so the
	 * adapter's counters are the witness that the sink and not lwIP took
	 * the frames.  The netdev's tx counters are mac80211's own. */
	lkpi_net_diag(&net_out);
	CASE_ANCHOR("rtl8xxxu_mon: lwip in rx_frames=%lu rx_foreign=%lu "
		    "tx_frames=%lu\n", net_in.rx_frames, net_in.rx_foreign,
		    net_in.tx_frames);
	CASE_ANCHOR("rtl8xxxu_mon: lwip out rx_frames=%lu rx_foreign=%lu "
		    "tx_frames=%lu\n", net_out.rx_frames, net_out.rx_foreign,
		    net_out.tx_frames);
	CASE_ANCHOR("rtl8xxxu_mon: tx dev tx_packets=%lu tx_bytes=%lu\n",
		    dev->stats.tx_packets, dev->stats.tx_bytes);
	mon_usb_ledger();

	/*
	 * The verdict is the floor only.  The radiotap census and the lwIP
	 * pollution are printed and judged by the harness: the case does not
	 * promote a counter into a claim ("received beacons" is not "monitor
	 * supported"), it states the session's facts.
	 */
	/* beacons * 1000 >= 8 * window_ms, without a 64-bit divide. */
	scale = MON_WINDOW_MS / 1000U;
	if (scale == 0U)
		scale = 1U;
	beacon_ok = mon.beacons >= (unsigned long)(MON_BEACON_PER_S * scale);
	if (!beacon_ok) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL beacons=%lu below %u/s over "
			    "%u ms\n", mon.beacons, MON_BEACON_PER_S,
			    MON_WINDOW_MS);
		return 8;
	}
	if (mon.bad_radiotap != 0UL) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL radiotap bad=%lu\n",
			    mon.bad_radiotap);
		return 9;
	}
	if (net_out.rx_frames != net_in.rx_frames) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL monitor frames reached lwIP "
			    "(%lu -> %lu)\n", net_in.rx_frames,
			    net_out.rx_frames);
		return 10;
	}
	if (dev->stats.tx_packets != 0UL) {
		CASE_ANCHOR("rtl8xxxu_mon: FAIL tx while monitoring (%lu)\n",
			    dev->stats.tx_packets);
		return 11;
	}
	CASE_ANCHOR("rtl8xxxu_mon: PASS MON\n");
	return 0;
}
