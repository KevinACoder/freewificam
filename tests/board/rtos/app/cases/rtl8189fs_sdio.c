/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8189fs_sdio.c
 * @brief  Case rtl8189fs_sdio: the board's SDIO 8189FTV attaches through the
 *         imported fsl_sdmmc stack to the vendored vendor driver, downloads its
 *         embedded firmware, and registers its wiphy + netdev (C3).
 *
 * The component's services come up in the vendor stack's own order: the SDIO
 * backend is installed, the module_init entries run (cfg80211/mac80211's
 * initcalls and the vendor driver's `sdio_register_driver`), and the SDIO
 * engine's start enumerates the card and probes the registered driver.  The
 * vendor probe is where the power sequence, the embedded firmware download and
 * the MAC/BB/RF init happen; it ends by registering the wiphy and the netdev.
 * The case then opens the interface (the driver's ndo_open) and reports the bus
 * and transfer counters the engine kept.
 *
 * This is C3 only: no scan, no association, no frames.  The vendor stack has
 * its own MLME, so the control plane is cfg80211 (D2 of the round's plan), not
 * mac80211; mac80211 is linked (the wireless world is one candidate set) but
 * has no driver in this image.
 *
 * The console carries the driver's attach lines here on purpose: C3 has no
 * timing requirement, and the firmware identity is the gate's own evidence.
 * The station case lowers the same runtime level before its attach, because
 * the GT round proved console printing costs association time.
 *
 * Anchors:
 *   rtl8189fs_sdio: core ready
 *   rtl8189fs_sdio: initcalls run
 *   rtl8189fs_sdio: card <vendor>:<device> funcs=<n> clock=<hz> width=<n> blk=<n>
 *   rtl8189fs_sdio: fw <driver line>
 *   rtl8189fs_sdio: wlan0 present=<0|1> after <ms> ms
 *   rtl8189fs_sdio: mac=<mac>
 *   rtl8189fs_sdio: start rc=<n>
 *   rtl8189fs_sdio: sdio cards=<n> drivers=<n> probes=<n> io=<n>/<n> errors=<n>
 *   rtl8189fs_sdio: PASS C3
 *   rtl8189fs_sdio: FAIL <reason>
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/lkpi_sdio_backend.h"
#include "linuxkpi_sdio.h"

#include <linux/etherdevice.h>
#include <linux/mmc/sdio_func.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>

#define SDIO_WAIT_MS		30000U
#define SDIO_POLL_MS		100U
#define SDIO_LOG_MAX		40U

/* The SDIO backend adapter over the imported stack.  Its install entry is the
 * port's own (third-party/fsl_sdmmc/ports/lkpi_sdio_fsl.c) and has no public
 * header; the case is the image's composition root, so the declaration lives
 * here rather than in a header no other unit reads. */
void lkpi_sdio_fsl_install(void);

/* The vendor driver's module parameters, as the GT round set them through
 * insmod: power management and LPS off (KI-027), and the driver's log level.
 * They are the driver's own globals (os_dep/linux/os_intfs.c:60,61,151); this
 * port has no insmod channel, so the case states the same values directly
 * before module init.  The type spellings match the definitions: int, int, uint. */
extern int rtw_power_mgnt;
extern int rtw_lps_level;
extern unsigned int rtw_drv_log_level;

#define RTW_LOG_INFO		4	/* _DRV_INFO_ */
#define PS_MODE_ACTIVE		0
#define LPS_OFF			0

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));

static unsigned sdio_log_lines;

static void
sdio_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL || sdio_log_lines >= SDIO_LOG_MAX)
		return;
	/* The gate's own evidence, lifted out of the driver's attach log. */
	if (strstr(line, "FW_NIC") != NULL || strstr(line, "fw_ver=") != NULL ||
	    strstr(line, "DLFW OK") != NULL || strstr(line, "CHIP TYPE") != NULL ||
	    strstr(line, "Chip Version Info") != NULL) {
		CASE_ANCHOR("rtl8189fs_sdio: fw %s\n", line);
		sdio_log_lines++;
	}
}

int
case_rtl8189fs_sdio_run(void)
{
	struct net_device *dev = NULL;
	struct lkpi_sdio_stats stats;
	struct lkpi_sdio_bus_info bus;
	struct sdio_func *f1;
	unsigned char mac[6];
	unsigned int waited = 0U;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("rtl8189fs_sdio: core ready\n");
	lkpi_log_sink_register(sdio_sink);

	/* The vendor values, before the driver's initcalls read them. */
	rtw_power_mgnt = PS_MODE_ACTIVE;
	rtw_lps_level = LPS_OFF;
	rtw_drv_log_level = RTW_LOG_INFO;

	lkpi_sdio_fsl_install();

	/* cfg80211's fs_initcall, then the vendor driver's module_init (which only
	 * registers the SDIO driver). */
	lkpi_module_init_run();
	CASE_ANCHOR("rtl8189fs_sdio: initcalls run\n");

	/* Card bring-up, then probe of everything registered: the vendor probe
	 * runs the power sequence, the embedded firmware download and the MAC /
	 * BB / RF init before it registers the wiphy and the netdev. */
	lkpi_sdio_start();

	f1 = lkpi_sdio_func(1U);
	if (f1 == NULL) {
		CASE_ANCHOR("rtl8189fs_sdio: FAIL no SDIO function 1 (card not up)\n");
		return 1;
	}
	lkpi_sdio_bus_info_get(&bus);
	CASE_ANCHOR("rtl8189fs_sdio: card %04x:%04x funcs=%u clock=%u width=%u blk=%u\n",
		    (unsigned)f1->vendor, (unsigned)f1->device,
		    (lkpi_sdio_func(2U) != NULL) ? 2U : 1U,
		    (unsigned)bus.clock_hz, (unsigned)bus.bus_width,
		    (unsigned)bus.blk_size);

	while (waited < SDIO_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(SDIO_POLL_MS);
		waited += SDIO_POLL_MS;
	}
	CASE_ANCHOR("rtl8189fs_sdio: wlan0 present=%d after %u ms\n",
		    dev != NULL, (unsigned)waited);
	if (dev == NULL) {
		CASE_ANCHOR("rtl8189fs_sdio: FAIL no wlan0 (probe/firmware/register)\n");
		return 2;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("rtl8189fs_sdio: mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	rc = dev_open(dev);
	CASE_ANCHOR("rtl8189fs_sdio: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_sdio: FAIL start rc=%d\n", rc);
		return 3;
	}

	lkpi_sdio_stats_get(&stats);
	CASE_ANCHOR("rtl8189fs_sdio: sdio cards=%u drivers=%u probes=%u io=%u/%u errors=%u\n",
		    (unsigned)stats.cards, (unsigned)stats.drivers,
		    (unsigned)stats.probes, (unsigned)stats.io_direct,
		    (unsigned)stats.io_extended, (unsigned)stats.errors);

	CASE_ANCHOR("rtl8189fs_sdio: PASS C3\n");
	return 0;
}
