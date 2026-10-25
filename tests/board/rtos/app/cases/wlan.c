/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   wlan.c
 * @brief  Case wlan: bring the SDIO card up through the imported stack, run the
 *         vendored RTL8189FTV driver through the linuxkpi module-init channel,
 *         and report the wireless device it registered.
 *
 * This is the 8189FTV line's P1+P2 in one case.  The order is the vendor
 * stack's own order:
 *
 *   1. install the fsl_sdmmc SDIO backend (the same seam another backend can
 *      replace);
 *   2. enumerate the card and read its identity (CMD5/CMD3/CMD7/CMD52);
 *   3. run the driver's module_init entries (it only registers an SDIO driver);
 *   4. start the SDIO engine, which brings the card up and probes whatever is
 *      registered - the driver's probe runs its power sequence, firmware
 *      download and MAC/BB/RF init before it registers a wiphy;
 *   5. bring the netdev up (the driver's open path) and read the MAC.
 *
 * The driver bytes are the vendor's.  This file only reports what the stack
 * says; a failure line names the layer that failed so the next step is a
 * verdict, not a guess.
 */

#include "cases.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"
#include "linuxkpi_cfg80211.h"
#include "lkpi_sdio_backend.h"
#include "linuxkpi_sdio.h"

#include "lkpinetif_seam.h"
#include "wlan_netif.h"

#include "fsl_sdio.h"
#include "sdmmc_port.h"

#include <linux/netdevice.h>

#define WLAN_ATTACH_WAIT_MS	20000u
#define WLAN_ATTACH_POLL_MS	100u

#define WLAN_VID		0x024cu
#define WLAN_PID		0xf179u

#define WLAN_SSID		"lab8189b"
#define WLAN_SSID_LEN		8u	/* "lab8189b" */
#define WLAN_SCAN_WAIT_MS	20000u
#define WLAN_JOIN_WAIT_MS	20000u
#define WLAN_POLL_MS		100u
#define WLAN_PING_COUNT		10u

#ifndef WLAN_SCAN_DIAG
#define WLAN_SCAN_DIAG		0
#endif
#ifndef WLAN_SCAN_BATCH
#define WLAN_SCAN_BATCH		1
#endif

static int wlan_prepared;

/* The cfg80211 substitute's upward notifications.  The scan table and the
 * connect result arrive here; the case waits on the counters, not on sleeps
 * whose duration would be the test's own assumption. */
static volatile int wlan_scan_done;
static volatile int wlan_connected;
static volatile uint16_t wlan_conn_status;
static uint8_t wlan_conn_bssid[6];
static unsigned int wlan_bss_rows;
static int wlan_bss_found;
static uint8_t wlan_bss_bssid[6];
static uint8_t wlan_bss_ssid[33];
static uint32_t wlan_bss_freq;
static int wlan_bss_signal;
static unsigned int wlan_bss_dump;
static unsigned int wlan_bss_ch_hist[16];

static void wlan_on_attached(struct wiphy *wiphy)
{
	(void)wiphy;
}

static void wlan_on_scan_done(struct wiphy *wiphy)
{
	(void)wiphy;
	wlan_scan_done = 1;
}

static void wlan_on_bss(struct wiphy *wiphy, const struct lkpicfg_bss *bss)
{
	(void)wiphy;
	if (bss == NULL) {
		return;
	}
	wlan_bss_rows++;
	{
		unsigned int ch = 0u;
		uint32_t freq = bss->freq;

		if (freq == 2484u)
			ch = 14u;
		else if ((freq >= 2412u) && (freq <= 2472u))
			ch = (freq - 2407u) / 5u;
		if (ch < 16u)
			wlan_bss_ch_hist[ch]++;
	}
	if (wlan_bss_dump < 10u) {
		char ssid[33];
		unsigned int n = bss->ssid_len;

		if (n > 32u) {
			n = 32u;
		}
		memcpy(ssid, bss->ssid, n);
		ssid[n] = '\0';
		CASE_ANCHOR("wlan: bss ssid=%s len=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x "
			    "freq=%u signal=%d\n",
			    ssid, (unsigned)bss->ssid_len,
			    bss->bssid[0], bss->bssid[1], bss->bssid[2],
			    bss->bssid[3], bss->bssid[4], bss->bssid[5],
			    (unsigned)bss->freq, bss->signal);
		wlan_bss_dump++;
	}
	if ((bss->ssid_len == WLAN_SSID_LEN) &&
	    (memcmp(bss->ssid, WLAN_SSID, WLAN_SSID_LEN) == 0)) {
		wlan_bss_found = 1;
		memcpy(wlan_bss_bssid, bss->bssid, sizeof(wlan_bss_bssid));
		memcpy(wlan_bss_ssid, bss->ssid, WLAN_SSID_LEN);
		wlan_bss_ssid[WLAN_SSID_LEN] = '\0';
		wlan_bss_freq = bss->freq;
		wlan_bss_signal = bss->signal;
	}
}

static void wlan_on_connected(struct wiphy *wiphy, const u8 bssid[6],
			      u16 status)
{
	(void)wiphy;
	memcpy(wlan_conn_bssid, bssid, sizeof(wlan_conn_bssid));
	wlan_conn_status = status;
	wlan_connected = 1;
}

static void wlan_on_disconnected(struct wiphy *wiphy, u16 reason)
{
	(void)wiphy;
	(void)reason;
	wlan_connected = 0;
}

static void wlan_on_sta_joined(struct wiphy *wiphy, const u8 mac[6])
{
	(void)wiphy;
	(void)mac;
}

static void wlan_on_sta_left(struct wiphy *wiphy, const u8 mac[6])
{
	(void)wiphy;
	(void)mac;
}


/* --- scan shapes and the production full scan ---------------------------- */

static void
wlan_scan_reset(int all)
{
	wlan_scan_done = 0;
	if (all) {
		wlan_bss_rows = 0u;
		wlan_bss_dump = 0u;
		wlan_bss_found = 0;
		wlan_bss_freq = 0u;
		wlan_bss_signal = 0;
		memset(wlan_bss_bssid, 0, sizeof(wlan_bss_bssid));
		memset(wlan_bss_ssid, 0, sizeof(wlan_bss_ssid));
		memset(wlan_bss_ch_hist, 0, sizeof(wlan_bss_ch_hist));
	}
}

static int
wlan_scan_wait(void)
{
	unsigned int waited = 0u;

	while ((wlan_scan_done == 0) && (waited < WLAN_SCAN_WAIT_MS)) {
		(void)osDelay(WLAN_POLL_MS);
		waited += WLAN_POLL_MS;
	}
	return (wlan_scan_done != 0) ? 0 : -1;
}

static void
wlan_scan_hist(char *buf, size_t len)
{
	unsigned int ch;
	int off = 0;

	buf[0] = '\0';
	for (ch = 1u; ch <= 14u; ch++) {
		if (wlan_bss_ch_hist[ch] == 0u)
			continue;
		if (off >= (int)len)
			break;
		off += snprintf(buf + off, len - (size_t)off, "%s%u:%u",
		    (off == 0) ? "" : ",", ch, wlan_bss_ch_hist[ch]);
	}
	if (off == 0)
		(void)snprintf(buf, len, "-");
}

static int
wlan_scan_shape(struct wiphy *wiphy, const unsigned int *chans,
		unsigned int n, const char *name)
{
	char hist[128];
	int st;

	wlan_scan_reset(1);
	st = lkpicfg_scan_start_list(wiphy, chans, n);
	if (st != 0) {
		CASE_ANCHOR("wlan: diag scan shape=%s start rc=%d\n", name, st);
		return st;
	}
	if (wlan_scan_wait() != 0) {
		CASE_ANCHOR("wlan: diag scan shape=%s timeout rows=%u\n",
		    name, wlan_bss_rows);
		return -1;
	}
	wlan_scan_hist(hist, sizeof(hist));
	CASE_ANCHOR("wlan: diag scan shape=%s rows=%u found=%d ch=%s\n",
	    name, wlan_bss_rows, wlan_bss_found, hist);
	return 0;
}

static int
wlan_scan_production(struct wiphy *wiphy)
{
	int st;

	wlan_scan_reset(1);
#if WLAN_SCAN_BATCH
	{
		unsigned int base;

		for (base = 1u; base <= 14u; base += 4u) {
			unsigned int chans[4];
			unsigned int n = 0u;
			unsigned int ch;

			for (ch = base; (ch < base + 4u) && (ch <= 14u); ch++)
				chans[n++] = ch;
			wlan_scan_reset(0);
			st = lkpicfg_scan_start_list(wiphy, chans, n);
			if (st != 0) {
				CASE_ANCHOR("wlan: FAIL scan batch base=%u rc=%d\n",
				    base, st);
				return -1;
			}
			if (wlan_scan_wait() != 0) {
				CASE_ANCHOR("wlan: FAIL scan batch timeout base=%u\n",
				    base);
				return -1;
			}
		}
	}
#else
	st = lkpicfg_scan_start(wiphy);
	if (st != 0) {
		CASE_ANCHOR("wlan: FAIL scan start rc=%d\n", st);
		return -1;
	}
	if (wlan_scan_wait() != 0) {
		CASE_ANCHOR("wlan: FAIL scan timeout rows=%u\n", wlan_bss_rows);
		return -1;
	}
#endif

	CASE_ANCHOR("wlan: scan rows=%u ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x "
		    "freq=%u signal=%d\n",
		    wlan_bss_rows,
		    wlan_bss_found ? (const char *)wlan_bss_ssid : "-",
		    wlan_bss_bssid[0], wlan_bss_bssid[1], wlan_bss_bssid[2],
		    wlan_bss_bssid[3], wlan_bss_bssid[4], wlan_bss_bssid[5],
		    (unsigned)wlan_bss_freq, wlan_bss_signal);
	if (wlan_bss_found == 0) {
		CASE_ANCHOR("wlan: FAIL ssid=%s not in the scan table\n",
		    WLAN_SSID);
		return -1;
	}
	return 0;
}

static const struct lkpicfg_consumer_ops wlan_consumer = {
	.attached = wlan_on_attached,
	.scan_done = wlan_on_scan_done,
	.bss = wlan_on_bss,
	.connected = wlan_on_connected,
	.disconnected = wlan_on_disconnected,
	.sta_joined = wlan_on_sta_joined,
	.sta_left = wlan_on_sta_left,
};

/* The linuxkpi world's console outlet: every line the core and the vendor
 * driver produce lands on the board log.  The driver's print format strings do
 * not all end in a newline, so the line is terminated here before the log
 * layer's whole-line discipline sees it. */
static void wlan_lkpi_sink(ulog_level_t level, const char *line)
{
	size_t n;

	if (line == NULL) {
		return;
	}
	n = strlen(line);
	while ((n > 0u) && ((line[n - 1] == '\n') || (line[n - 1] == '\r'))) {
		n--;
	}
	if (n > 0u) {
		log_emit(level, "lkpi: %.*s\n", (int)n, line);
	}
}

/*
 * Called from main() after osKernelInitialize() and before the scheduler, the
 * same place the reference image wires the core.  The driver's module_init is
 * run later, from the case, so this only creates the core's services and the
 * sink.
 */
void case_wlan_prepare(void)
{
	if (wlan_prepared != 0) {
		return;
	}
	lkpi_log_sink_register(wlan_lkpi_sink);
	lkpi_core_init();
	wlan_prepared = 1;
}

int case_wlan_run(void)
{
	struct lkpi_sdio_be_card_info info;
	struct lkpi_sdio_stats stats;
	struct net_device *dev;
	struct wiphy *wiphy;
	uint8_t mac[6];
	unsigned int waited;
	unsigned int i;
	bool mac_zero = true;
	int st;

	case_wlan_prepare();

	/* --- P1: the card on the bus -------------------------------------- */
	if (!lkpi_sdio_be_ready()) {
		lkpi_sdio_fsl_install();
	}
	memset(&info, 0, sizeof(info));
	st = lkpi_sdio_be_card_init(&info);
	if (st != 0) {
		CASE_ANCHOR("wlan: FAIL sdio card init st=%d\n", st);
		return -1;
	}

	CASE_ANCHOR("wlan: sdio up card=%04x:%04x funcs=%u cccr=%u clock=%u "
		    "width=%u hs=%u\n",
		    (unsigned)info.vendor, (unsigned)info.device,
		    (unsigned)info.func_count, (unsigned)info.cccr_version,
		    (unsigned)info.clock_hz, (unsigned)info.bus_width,
		    (unsigned)info.high_speed);

	if ((info.vendor != WLAN_VID) || (info.device != WLAN_PID)) {
		CASE_ANCHOR("wlan: FAIL card id %04x:%04x want %04x:%04x\n",
			    (unsigned)info.vendor, (unsigned)info.device,
			    WLAN_VID, WLAN_PID);
		return -1;
	}

	/* --- P2: driver registration, probe, firmware, MAC ---------------- */
	lkpi_module_init_run();
	lkpi_sdio_start();

	wiphy = lkpicfg_wiphy();
	waited = 0u;
	while ((wiphy == NULL) && (waited < WLAN_ATTACH_WAIT_MS)) {
		(void)osDelay(WLAN_ATTACH_POLL_MS);
		waited += WLAN_ATTACH_POLL_MS;
		wiphy = lkpicfg_wiphy();
	}
	if (wiphy == NULL) {
		CASE_ANCHOR("wlan: FAIL no wiphy after %u ms (driver probe failed)\n",
			    (unsigned)waited);
		return -1;
	}

	st = lkpicfg_netdev_up(wiphy);
	CASE_ANCHOR("wlan: netdev up rc=%d\n", st);
	if (st != 0) {
		CASE_ANCHOR("wlan: FAIL netdev up rc=%d\n", st);
		return -1;
	}

	if (lkpicfg_get_hwaddr(wiphy, mac) != 0) {
		CASE_ANCHOR("wlan: FAIL no hardware address\n");
		return -1;
	}
	CASE_ANCHOR("wlan: attach mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	dev = lkpicfg_netdev(wiphy);
	if (dev != NULL) {
		CASE_ANCHOR("wlan: netdev addr=%02x:%02x:%02x:%02x:%02x:%02x\n",
			    dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
			    dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
	}

	for (i = 0u; i < sizeof(mac); i++) {
		if (mac[i] != 0u) {
			mac_zero = false;
			break;
		}
	}
	if (mac_zero) {
		CASE_ANCHOR("wlan: FAIL hardware address is all zero\n");
		return -1;
	}

	/* --- P3: scan, open join, lwIP netif, ping ------------------------- */
	if (lkpinetif_bind(wiphy) != 0) {
		CASE_ANCHOR("wlan: FAIL netif bind\n");
		return -1;
	}
	if (wlan_netif_start() != 0) {
		CASE_ANCHOR("wlan: FAIL netif start\n");
		return -1;
	}
	if (lkpicfg_consumer_register(&wlan_consumer, NULL) != 0) {
		CASE_ANCHOR("wlan: FAIL consumer register\n");
		return -1;
	}

#if WLAN_SCAN_DIAG
	{
		static const unsigned int full[14] = {
			1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u
		};
		static const unsigned int pair_1_5[2] = { 1u, 5u };
		static const unsigned int pair_5_1[2] = { 5u, 1u };
		static const unsigned int single_5[1] = { 5u };

		CASE_ANCHOR("wlan: diag begin\n");
		(void)wlan_scan_shape(wiphy, full, 14u, "full");
		(void)wlan_scan_shape(wiphy, pair_1_5, 2u, "pair-1-5");
		(void)wlan_scan_shape(wiphy, pair_5_1, 2u, "pair-5-1");
		(void)wlan_scan_shape(wiphy, single_5, 1u, "single-5");
		CASE_ANCHOR("wlan: diag end\n");
	}
#endif
	st = wlan_scan_production(wiphy);
	if (st != 0)
		return -1;

	wlan_connected = 0;
	wlan_conn_status = 0xffffu;
	memset(wlan_conn_bssid, 0, sizeof(wlan_conn_bssid));
	st = lkpicfg_connect(wiphy, (const u8 *)WLAN_SSID, WLAN_SSID_LEN,
			     NULL, NULL, 0);
	if (st != 0) {
		CASE_ANCHOR("wlan: FAIL connect rc=%d\n", st);
		return -1;
	}
	waited = 0u;
	while ((wlan_connected == 0) && (waited < WLAN_JOIN_WAIT_MS)) {
		(void)osDelay(WLAN_POLL_MS);
		waited += WLAN_POLL_MS;
	}
	CASE_ANCHOR("wlan: join state=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x "
		    "status=%u\n",
		    wlan_connected ? 4u : 0u,
		    wlan_conn_bssid[0], wlan_conn_bssid[1], wlan_conn_bssid[2],
		    wlan_conn_bssid[3], wlan_conn_bssid[4], wlan_conn_bssid[5],
		    (unsigned)wlan_conn_status);
	if ((wlan_connected == 0) || (wlan_conn_status != 0u)) {
		CASE_ANCHOR("wlan: FAIL join not completed\n");
		return -1;
	}

	wlan_netif_link(1);
	{
		unsigned int ping_rx = 0u;

		st = wlan_netif_ping("192.168.99.1", WLAN_PING_COUNT, &ping_rx);
		if ((st != 0) || (ping_rx != WLAN_PING_COUNT)) {
#if WLAN_TCP_SINK
			CASE_ANCHOR("wlan: WARN ping %u/%u; continuing to tcp sink\n",
				    ping_rx, WLAN_PING_COUNT);
			if (ping_rx == 0u) {
				CASE_ANCHOR("wlan: FAIL no ping reply\n");
				return -1;
			}
#else
			CASE_ANCHOR("wlan: FAIL ping %u/%u\n", ping_rx,
				    WLAN_PING_COUNT);
			return -1;
#endif
		}
	}

#if WLAN_TCP_SINK
	st = wlan_netif_tcp_sink(WLAN_TCP_SINK_SECONDS);
	if (st != 0) {
		CASE_ANCHOR("wlan: FAIL tcp sink\n");
		return -1;
	}
#endif

	memset(&stats, 0, sizeof(stats));
	lkpi_sdio_stats_get(&stats);
	CASE_ANCHOR("wlan: sdio stats cards=%u drivers=%u probes=%u "
		    "io_direct=%u io_ext=%u errors=%u\n",
		    (unsigned)stats.cards, (unsigned)stats.drivers,
		    (unsigned)stats.probes, (unsigned)stats.io_direct,
		    (unsigned)stats.io_extended, (unsigned)stats.errors);
	if (stats.errors != 0u) {
		CASE_ANCHOR("wlan: FAIL sdio errors=%u\n",
			    (unsigned)stats.errors);
		return -1;
	}

	CASE_ANCHOR("wlan: PASS\n");
	return 0;
}
