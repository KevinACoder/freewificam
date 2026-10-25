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
 *   1. install the fsl_sdmmc SDIO backend (the same seam a host mock can
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
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"
#include "linuxkpi_cfg80211.h"
#include "lkpi_sdio_backend.h"
#include "linuxkpi_sdio.h"

#include "fsl_sdio.h"
#include "sdmmc_port.h"

#include <linux/netdevice.h>

#define WLAN_ATTACH_WAIT_MS	20000u
#define WLAN_ATTACH_POLL_MS	100u

#define WLAN_VID		0x024cu
#define WLAN_PID		0xf179u

static int wlan_prepared;

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
