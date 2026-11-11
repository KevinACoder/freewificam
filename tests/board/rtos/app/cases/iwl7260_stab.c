/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_stab.c
 * @brief  Case iwl7260_stab: the stability round after C5 joined and C6T
 *         measured.  Two claims, one boot:
 *
 *   1. a 30-minute TCP downlink keeps running: the peer's iperf2 client sends
 *      for the whole window, and the case prints a health line every 30 s -
 *      adapter counters, heap free/low-water, skb pool occupancy.  A reset, a
 *      watchdog or a stalled stack shows up as a missing report or a health
 *      line that stops moving.
 *   2. ten disconnect -> scan -> connect -> ping cycles all succeed.  The AP's
 *      view is sampled by the host round script; the case's PASS is the
 *      station's own.
 *
 * The static neighbour on the peer side is the throughput round's workaround;
 * the dynamic-ARP gate is a separate functional check (the missing firmware
 * multicast filter), not this case's claim.
 *
 * Anchors:
 *   iwl7260_stab: plan=stability bssid=<mac> dev=<name>
 *   iwl7260_stab: long begin window_ms=<n> health_ms=<n>
 *   iwl7260_stab: health t=<ms> rxf=<n> txf=<n> heap_free=<n> heap_min=<n>
 *                 pool_used=<n> pool_high=<n>
 *   iwl7260_stab: long report done=<0|1> bytes=<n> ms=<n> kbit=<n>
 *   iwl7260_stab: reconnect n=<i> ok=<0|1> ms=<n>
 *   iwl7260_stab: PASS STAB
 *   iwl7260_stab: FAIL <reason>
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"

#include <linux/netdevice.h>
#include <net/cfg80211.h>
#include <net/net_namespace.h>

#define STAB_SSID		"labwifi-ar-ht20s"
#define STAB_FREQ_MHZ		2432u
#define STAB_PEER		"192.168.99.1"
#define STAB_TCP_PORT		5001u
#define STAB_LONG_MS		(30u * 60u * 1000u)
#define STAB_HEALTH_MS		30000u
#define STAB_RECONNECTS		10u
#define STAB_DISCONNECT_WAIT_MS	5000u
#define STAB_SCAN_WAIT_MS	10000u
#define STAB_CONNECT_WAIT_MS	20000u
#define STAB_PING_COUNT		4u

static int
stab_wait_connected(struct net_device *dev, unsigned char bssid[6],
		    unsigned wait_ms)
{
	unsigned waited = 0u;

	while (waited < wait_ms) {
		int rc = lkpi_nl80211_connected(dev, bssid);

		if (rc == 1)
			return 0;
		if (rc < 0)
			return rc;
		(void)osDelay(100u);
		waited += 100u;
	}
	return -1;
}

static int
stab_wait_disconnected(struct net_device *dev, unsigned wait_ms)
{
	unsigned waited = 0u;

	while (waited < wait_ms) {
		int rc = lkpi_nl80211_connected(dev, NULL);

		if (rc == 0)
			return 0;
		if (rc < 0)
			return rc;
		(void)osDelay(100u);
		waited += 100u;
	}
	return -1;
}

/* One reconnect cycle: deauth, scan, join, then four echoes. */
static int
stab_reconnect(struct net_device *dev, unsigned n)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	unsigned char bssid[6];
	unsigned char joined[6];
	unsigned waited;
	unsigned rx = 0u;
	unsigned rtt_min = 0u, rtt_avg = 0u, rtt_max = 0u;
	uint64_t t0 = rtos_clock_us();
	unsigned ms;
	int freq = 0;
	int sig = 0;
	int rc;

	(void)lkpi_nl80211_disconnect(dev, 3u);
	rc = stab_wait_disconnected(dev, STAB_DISCONNECT_WAIT_MS);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_stab: reconnect n=%u ok=0 ms=%u why=deauth\n",
			    n, (unsigned)((rtos_clock_us() - t0) / 1000u));
		return -1;
	}

	rc = lkpi_nl80211_trigger_scan(dev, STAB_SSID, STAB_FREQ_MHZ);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_stab: reconnect n=%u ok=0 ms=%u why=scan_rc_%d\n",
			    n, (unsigned)((rtos_clock_us() - t0) / 1000u), rc);
		return -1;
	}
	for (waited = 0u; waited < STAB_SCAN_WAIT_MS; waited += 100u) {
		if (wdev != NULL && wdev->wiphy != NULL &&
		    lkpi_nl80211_bss_find(wdev->wiphy, STAB_SSID, bssid, &freq,
					  &sig) == 0)
			break;
		(void)osDelay(100u);
	}
	if (waited >= STAB_SCAN_WAIT_MS) {
		CASE_ANCHOR("iwl7260_stab: reconnect n=%u ok=0 ms=%u why=bss\n",
			    n, (unsigned)((rtos_clock_us() - t0) / 1000u));
		return -1;
	}

	rc = lkpi_nl80211_connect(dev, STAB_SSID, bssid, (unsigned int)freq);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_stab: reconnect n=%u ok=0 ms=%u why=connect_rc_%d\n",
			    n, (unsigned)((rtos_clock_us() - t0) / 1000u), rc);
		return -1;
	}
	rc = stab_wait_connected(dev, joined, STAB_CONNECT_WAIT_MS);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_stab: reconnect n=%u ok=0 ms=%u why=timeout\n",
			    n, (unsigned)((rtos_clock_us() - t0) / 1000u));
		return -1;
	}

	rc = lkpi_net_ping(STAB_PEER, STAB_PING_COUNT, 56u, 300u, &rx,
			   &rtt_min, &rtt_avg, &rtt_max);
	ms = (unsigned)((rtos_clock_us() - t0) / 1000u);
	CASE_ANCHOR("iwl7260_stab: reconnect n=%u ok=%d ms=%u ping=%u/%u\n",
		    n, (rc == 0 && rx == STAB_PING_COUNT) ? 1 : 0, ms, rx,
		    STAB_PING_COUNT);
	return (rc == 0 && rx == STAB_PING_COUNT) ? 0 : -1;
}

int
case_iwl7260_stab_run(void)
{
	struct net_device *dev;
	struct wireless_dev *wdev;
	unsigned char bssid[6];
	unsigned i;
	unsigned ok_reconnects = 0u;
	unsigned elapsed;
	uint64_t t0;
	struct lkpi_net_phase r;
	struct osMemoryStats heap;
	struct lkpi_net_diag diag;
	struct lkpi_skb_pool_stats pool;
	int long_done = 0;
	int rc;

	dev = dev_get_by_name(&init_net, "wlan0");
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_stab: FAIL no wlan0\n");
		return 1;
	}
	rc = lkpi_nl80211_connected(dev, bssid);
	if (rc != 1) {
		CASE_ANCHOR("iwl7260_stab: FAIL not connected rc=%d\n", rc);
		return 2;
	}
	CASE_ANCHOR("iwl7260_stab: plan=stability bssid=%02x:%02x:%02x:%02x:"
		    "%02x:%02x dev=%s\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    dev->name);
	if (lkpi_net_ready() != 1) {
		CASE_ANCHOR("iwl7260_stab: FAIL lwIP netif not ready\n");
		return 3;
	}
	wdev = dev->ieee80211_ptr;

	rc = lkpi_net_iperf_server_start(STAB_TCP_PORT);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_stab: FAIL server rc=%d\n", rc);
		return 4;
	}

	/* --- the long run -------------------------------------------------- */
	CASE_ANCHOR("iwl7260_stab: long begin window_ms=%u health_ms=%u\n",
		    STAB_LONG_MS, STAB_HEALTH_MS);
	(void)lkpi_net_iperf_down_begin();
	t0 = rtos_clock_us();
	for (elapsed = 0u; elapsed < STAB_LONG_MS; elapsed += STAB_HEALTH_MS) {
		if (lkpi_net_iperf_down_poll(&r) == 0) {
			long_done = 1;
			break;
		}
		lkpi_net_diag(&diag);
		osMemoryGetStats(&heap);
		lkpi_skb_pool_stats(&pool);
		CASE_ANCHOR("iwl7260_stab: health t=%u rxf=%lu txf=%lu "
			    "heap_free=%u heap_min=%u pool_used=%u "
			    "pool_high=%u\n",
			    (unsigned)((rtos_clock_us() - t0) / 1000u),
			    diag.rx_frames, diag.tx_frames,
			    (unsigned)heap.free_bytes,
			    (unsigned)heap.min_free_bytes,
			    (unsigned)pool.skb_used,
			    (unsigned)pool.skb_high);
		(void)osDelay(STAB_HEALTH_MS);
	}
	(void)lkpi_net_iperf_down_poll(&r);
	lkpi_net_iperf_down_end();
	CASE_ANCHOR("iwl7260_stab: long report done=%d bytes=%u ms=%u "
		    "kbit=%u\n",
		    long_done, r.bytes, r.ms, r.kbit);
	if (long_done == 0 || r.bytes == 0u) {
		CASE_ANCHOR("iwl7260_stab: FAIL long run did not complete\n");
		return 5;
	}

	/* --- ten reconnects ------------------------------------------------- */
	for (i = 1u; i <= STAB_RECONNECTS; i++) {
		if (stab_reconnect(dev, i) == 0)
			ok_reconnects++;
	}

	if (ok_reconnects == STAB_RECONNECTS) {
		CASE_ANCHOR("iwl7260_stab: PASS STAB\n");
		return 0;
	}
	CASE_ANCHOR("iwl7260_stab: FAIL reconnects %u/%u\n", ok_reconnects,
		    STAB_RECONNECTS);
	return 6;
}
