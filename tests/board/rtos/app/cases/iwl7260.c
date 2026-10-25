/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260.c
 * @brief  Case iwl7260: the AC 7260 attaches to the vendored iwlwifi/mvm
 *         driver over the linuxkpi PCI engine, and its firmware reaches ALIVE.
 *
 * The B3 gate.  It carries the driver, net80211 and the linuxkpi wireless face
 * (iwlwifi.mk), starts the engine on the board's root complex, waits for the
 * driver's asynchronous probe to register its hw, then runs the driver's start
 * chain.  The attach counter is the host-visible proof; the firmware's ALIVE
 * is the driver's own `loaded firmware version` line, which the judge keys on
 * from the captured serial.
 *
 * Anchors:
 *   iwl7260: pci bus up, <n> device(s)
 *   iwl7260: attach ok wiphy=<n> after <ms> ms
 *   iwl7260: start ok attached=<n> rx=<n>
 *   iwl7260: PASS
 *
 * No endpoint / no device: `iwl7260: platform-inactive <reason>` and return 2.
 */

#include <stdint.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linuxkpi_pci.h"
#include "wlan_bridge.h"
#include "lkpi_pcie_port.h"

#define IWL7260_WAIT_MS		40000U
#define IWL7260_POLL_MS		100U

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_module_init_run(void);
int lkpi_fw_iwl7260_fw_register(void);

static void iwl7260_sink(ulog_level_t level, const char *line)
{
	CASE_INFO("lkpi: %s\n", line);
}

static int iwl7260_attached(void)
{
	struct wlan_bridge_counters counters;

	wlan_bridge_stats(&counters);
	return counters.attached != 0;
}

int case_iwl7260_run(void)
{
	struct wlan_bridge_counters counters;
	uint32_t waited = 0U;

	lkpi_log_sink_register(iwl7260_sink);
	(void)wlan_seam_adapter_init();
	lkpi_pcie_port_install();
	(void)lkpi_fw_iwl7260_fw_register();
	lkpi_module_init_run();

	lkpi_pci_start();
	CASE_ANCHOR("iwl7260: pci bus up, %u device(s)\n",
		    (unsigned)lkpi_pci_dev_count_get());
	if (lkpi_pci_dev_count_get() == 0U) {
		CASE_ANCHOR("iwl7260: platform-inactive no pci device\n");
		return 2;
	}

	while (waited < IWL7260_WAIT_MS && !iwl7260_attached()) {
		osDelay(IWL7260_POLL_MS);
		waited += IWL7260_POLL_MS;
	}
	if (!iwl7260_attached()) {
		CASE_ANCHOR("iwl7260: FAIL attach timeout after %u ms\n",
			    (unsigned)waited);
		return 1;
	}

	CASE_ANCHOR("iwl7260: attach ok after %u ms\n", (unsigned)waited);

	if (wlan_bridge_up() != 0) {
		CASE_ANCHOR("iwl7260: FAIL start chain refused\n");
		return 1;
	}
	wlan_bridge_stats(&counters);
	CASE_ANCHOR("iwl7260: start ok attached=%u rx=%u\n",
		    (unsigned)counters.attached, (unsigned)counters.rx_frames);
	if (counters.attached == 0U) {
		CASE_ANCHOR("iwl7260: FAIL start returned but not attached\n");
		return 1;
	}

	CASE_ANCHOR("iwl7260: PASS\n");
	return 0;
}
