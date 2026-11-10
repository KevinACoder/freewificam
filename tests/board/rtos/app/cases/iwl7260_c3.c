/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_c3.c
 * @brief  Case iwl7260_c3: the AC 7260 attaches to the vendored iwlwifi/mvm
 *         driver over the component's PCI engine, the firmware reaches ALIVE,
 *         and the op-mode registers a wiphy with the real mac80211.
 *
 * C3 is the driver gate, one layer above C2: the same stack that C2 proved can
 * register now gets its wiphy from iwlwifi's own probe path.  The case brings
 * the component's services up in the real order (core -> PCI port -> firmware
 * registry -> module init entries -> bus start), waits for the stack's default
 * interface to appear, then opens it so mac80211 calls the driver's start().
 *
 * What a PASS means: iwlwifi's PCI probe claimed 8086:08b1, its firmware load
 * reached the driver's ALIVE handshake (the driver's own "loaded firmware
 * version" line), the op-mode's ieee80211_register_hw() succeeded (wlan0 in the
 * netdev registry), and dev_open() returned 0 (mac80211's drv_start ->
 * iwl_mvm_mac_start).  No frame has moved; C4/C5/C6 are separate gates.
 *
 * Anchors:
 *   iwl7260_c3: core ready
 *   iwl7260_c3: fw registered=<0|err> name=<name>
 *   iwl7260_c3: initcalls run
 *   iwl7260_c3: pci devs=<n>
 *   iwl7260_c3: driver claimed 8086:08b1=<0|1>
 *   iwl7260_c3: firmware <driver line>
 *   iwl7260_c3: wlan0 present=<0|1> after <ms> ms
 *   iwl7260_c3: start rc=<n>
 *   iwl7260_c3: PASS
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linuxkpi_pci.h"
#include "adapter.h"

#include <linux/etherdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#define C3_WAIT_MS		40000U
#define C3_POLL_MS		100U

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
int lkpi_fw_iwl7260_fw_register(void);

/* The firmware ALIVE is the driver's own line; the sink preserves it in the
 * judged stream and marks the few driver lines that are milestones. */
static void
c3_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL)
		return;
	if (strstr(line, "loaded firmware version") != NULL)
		CASE_ANCHOR("iwl7260_c3: firmware %s\n", line);
	else if (strstr(line, "iwlwifi") != NULL)
		CASE_INFO("iwl7260_c3: drv %s\n", line);
}

int
case_iwl7260_c3_run(void)
{
	struct net_device *dev = NULL;
	uint32_t waited = 0U;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("iwl7260_c3: core ready\n");
	lkpi_log_sink_register(c3_sink);

	/* The board's root complex and the PCI engine over it. */
	lkpi_pcie_port_install();

	/* The compiled-in firmware must be in the registry before any probe. */
	rc = lkpi_fw_iwl7260_fw_register();
	CASE_ANCHOR("iwl7260_c3: fw registered=%d name=iwlwifi-7260-17.ucode\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c3: FAIL firmware registration rc=%d\n", rc);
		return 1;
	}

	/* iwlwifi's two module_init entries (core, then opmode) land in
	 * .lkpi_init; the walk runs them in link order. */
	lkpi_module_init_run();
	CASE_ANCHOR("iwl7260_c3: initcalls run\n");

	/* Enumerate and probe: pci_register_driver() ran in the walk above, so
	 * the start is what hands 8086:08b1 to iwlwifi's probe. */
	lkpi_pci_start();
	CASE_ANCHOR("iwl7260_c3: pci devs=%u\n",
		    (unsigned)lkpi_pci_dev_count_get());

	/* The stack's default interface is created by ieee80211_register_hw()
	 * inside the op-mode start, which runs when the firmware ALIVE
	 * completes (the registry's continuation is synchronous). */
	while (waited < C3_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(C3_POLL_MS);
		waited += C3_POLL_MS;
	}
	CASE_ANCHOR("iwl7260_c3: wlan0 present=%d after %u ms\n",
		    dev != NULL, (unsigned)waited);
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_c3: FAIL no wlan0 (probe/firmware/register)\n");
		return 2;
	}

	/*
	 * C3b: open the interface.  mac80211's ndo_open calls the driver's
	 * start(), whose return value is what this anchor reports.  A failure
	 * here is recorded but does not erase C3a (probe + firmware +
	 * register), which is what the wlan0 line above already proved.
	 */
	rc = dev_open(dev);
	CASE_ANCHOR("iwl7260_c3: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c3: FAIL start rc=%d\n", rc);
		return 3;
	}

	CASE_ANCHOR("iwl7260_c3: PASS\n");
	return 0;
}
