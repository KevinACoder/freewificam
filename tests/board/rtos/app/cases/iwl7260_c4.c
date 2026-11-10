/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_c4.c
 * @brief  Case iwl7260_c4: the AC 7260 scans and finds the bench AP.
 *
 * C4 sits on C3's attach (the same probe, firmware ALIVE, registration and
 * dev_open).  On top of it, this case sends the real NL80211_CMD_TRIGGER_SCAN
 * attribute stream through nl80211's own handler, waits for the stack's BSS
 * table to carry the bench AP, and prints the BSSID/channel/signal.
 *
 * The bench seam is only the netlink socket: the attributes are built with the
 * real nla_put* and parsed by nlmsg_parse; nl80211's pre_doit/doit/post_doit
 * run for real, and the scan itself is mac80211 -> iwlwifi -> firmware.  What
 * this is not: an `iw` control plane, association (C5) or the data plane (C6).
 *
 * Anchors:
 *   iwl7260_c4: core ready
 *   iwl7260_c4: fw registered=<0|err>
 *   iwl7260_c4: initcalls run
 *   iwl7260_c4: pci devs=<n>
 *   iwl7260_c4: claimed 8086:08b1
 *   iwl7260_c4: firmware <driver line>
 *   iwl7260_c4: wlan0 present=<0|1> ifindex=<n>
 *   iwl7260_c4: start rc=<n>
 *   iwl7260_c4: scan ssid=<ssid> freq=<mhz> rc=<n>
 *   iwl7260_c4: scan wildcard rc=<n>
 *   iwl7260_c4: bss ssid=<ssid> bssid=<mac> freq=<mhz> signal=<dbm>
 *   iwl7260_c4: PASS
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linuxkpi_pci.h"
#include "adapter.h"

#include <linux/etherdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#define C4_WAIT_MS		40000U
#define C4_POLL_MS		100U
#define C4_SCAN_WAIT_MS		10000U

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
int lkpi_fw_iwl7260_fw_register(void);

static void
c4_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL)
		return;
	if (strstr(line, "loaded firmware version") != NULL)
		CASE_ANCHOR("iwl7260_c4: firmware %s\n", line);
	else if (strstr(line, "claimed") != NULL)
		CASE_INFO("iwl7260_c4: drv %s\n", line);
	else if (strstr(line, "scan") != NULL || strstr(line, "Scan") != NULL)
		CASE_INFO("iwl7260_c4: drv %s\n", line);
}

int
case_iwl7260_c4_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	unsigned char bssid[6];
	uint32_t waited = 0U;
	int freq = 0;
	int sig = 0;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("iwl7260_c4: core ready\n");
	lkpi_log_sink_register(c4_sink);

	lkpi_pcie_port_install();

	rc = lkpi_fw_iwl7260_fw_register();
	CASE_ANCHOR("iwl7260_c4: fw registered=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c4: FAIL firmware registration rc=%d\n", rc);
		return 1;
	}

	lkpi_module_init_run();
	CASE_ANCHOR("iwl7260_c4: initcalls run\n");

	lkpi_pci_start();
	CASE_ANCHOR("iwl7260_c4: pci devs=%u\n",
		    (unsigned)lkpi_pci_dev_count_get());

	while (waited < C4_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(C4_POLL_MS);
		waited += C4_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_c4: FAIL no wlan0 after %u ms\n",
			    (unsigned)waited);
		return 2;
	}
	CASE_ANCHOR("iwl7260_c4: wlan0 present=1 ifindex=%d\n", dev->ifindex);

	rc = dev_open(dev);
	CASE_ANCHOR("iwl7260_c4: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c4: FAIL start rc=%d\n", rc);
		return 3;
	}

	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("iwl7260_c4: FAIL no wireless_dev\n");
		return 4;
	}

	/*
	 * Directed scan: the bench AP is on ch5 (2432 MHz) and its SSID is fixed
	 * by the lab.  A wildcard retry follows only if the directed pass produces
	 * no BSS within the window.
	 */
	rc = lkpi_nl80211_trigger_scan(dev, "labwifi-ar-ht20s", 2432);
	CASE_ANCHOR("iwl7260_c4: scan ssid=labwifi-ar-ht20s freq=2432 rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c4: FAIL scan rc=%d\n", rc);
		return 5;
	}

	for (waited = 0U; waited < C4_SCAN_WAIT_MS; waited += C4_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, "labwifi-ar-ht20s",
					  bssid, &freq, &sig) == 0)
			goto found;
		osDelay(C4_POLL_MS);
	}

	/* The directed pass saw nothing; try every supported channel once. */
	rc = lkpi_nl80211_trigger_scan(dev, NULL, 0);
	CASE_ANCHOR("iwl7260_c4: scan wildcard rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c4: FAIL wildcard scan rc=%d\n", rc);
		return 6;
	}
	for (waited = 0U; waited < C4_SCAN_WAIT_MS; waited += C4_POLL_MS) {
		if (lkpi_nl80211_bss_find(wdev->wiphy, "labwifi-ar-ht20s",
					  bssid, &freq, &sig) == 0)
			goto found;
		osDelay(C4_POLL_MS);
	}

	CASE_ANCHOR("iwl7260_c4: FAIL bss timeout (scan started, no BSS)\n");
	return 7;

found:
	CASE_ANCHOR("iwl7260_c4: bss ssid=labwifi-ar-ht20s bssid=%02x:%02x:%02x:%02x:%02x:%02x freq=%d signal=%d\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    freq, sig / 100);
	CASE_ANCHOR("iwl7260_c4: PASS\n");
	return 0;
}
