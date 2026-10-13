/**
 * @file    iwlwifi_probe.c
 * @brief   Case iwlwifi-probe: the PCIe bus is brought up, the linuxkpi PCI
 *          engine enumerates the AC 7260, the vendored iwlwifi driver's probe
 *          runs (transport bring-up, firmware download, NVM read) and its hw is
 *          registered - which is the moment the bridge attaches.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h). The
 *          chain is the three layers in their real order: the linuxkpi PCI
 *          engine's board backend over the RK3568 root complex
 *          (ports/pcie/lkpi_pcie_port.c, components/linuxkpi/pci/
 *          linuxkpi_pcie_backend.h), the engine that owns the bus and probes
 *          drivers (components/linuxkpi/pci/linuxkpi_pci.c), and the module-init
 *          channel that runs the driver's entry point
 *          (components/linuxkpi/linuxkpi_core.h, module_init).
 *
 * The order carries one decision worth naming: the driver's entry point is run
 * BEFORE the engine starts, because the entry point only registers the driver
 * (pci_register_driver) - the engine's start is what brings the bus up and
 * probes whatever is registered, which is the same order a Linux boot takes.
 *
 * What the case can honestly claim: the driver's probe path brings its
 * transport up, downloads its firmware (the blob the registry holds under
 * "iwlwifi-7260-17.ucode") and reads its NVM before it registers anything. A
 * registered hw with an attached bridge is therefore evidence that all of that
 * ran - the probe returns failure before registration if any step fails. The
 * case says exactly that rather than inferring more.
 *
 * Anchors (the judge checks each on PASS):
 *   pcie: 'iwlwifi' claimed 8086:08b1          - the engine claimed the device
 *   lkpi: <bdf>: Detected Intel(R) ... 7260    - the driver identified the chip
 *   lkpi: <bdf>: loaded firmware version ...   - the driver loaded its firmware
 *   lkpi: lkpi80211: hw registered             - the mac80211 substitute
 *   wlan: probe done attached=1 ...            - the bridge attached
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "linuxkpi_pci.h"

/* net80211-world header of the bridge: scalars only, safe from this world */
#include "wlan_bridge.h"

#define IWLWIFI_PROBE_WAIT_MS	60000U
#define IWLWIFI_PROBE_POLL_MS	100U

static int iwlwifi_attached(void)
{
	struct wlan_bridge_counters c;

	wlan_bridge_stats(&c);
	return c.attached != 0;
}

void iwlwifi_probe_chain(void)
{
	struct wlan_bridge_counters c;
	unsigned spins = IWLWIFI_PROBE_WAIT_MS / IWLWIFI_PROBE_POLL_MS;

	/*
	 * The engine's start: it installs nothing (the board backend was
	 * registered at boot), brings the bus up through the seam - the board's
	 * own root-complex bring-up runs inside that call - builds its device
	 * records and probes every registered driver. The driver's probe runs
	 * its own transport bring-up, downloads its firmware and registers its
	 * hw; the engine logs the claim as it happens.
	 */
	lkpi_pci_start();
	LOG_ANCHOR("iwlwifi: pci bus up, %u device(s)\n",
		  lkpi_pci_dev_count_get());

	if (lkpi_pci_dev_count_get() == 0U) {
		board_case_fail("the PCIe bus enumerated no device - see the "
		    "pcie lines above");
		return;
	}

	/*
	 * The driver answers asynchronously (its firmware request and mac80211
	 * start run on the linuxkpi workqueue), so the attach arrives a little
	 * after the probe returns. The wait is bounded, so a driver that never
	 * registers its hw is reported rather than waited on forever.
	 */
	for (unsigned spin = 0; spin < spins; spin++) {
		if (iwlwifi_attached()) {
			break;
		}
		osDelay(IWLWIFI_PROBE_POLL_MS);
	}

	if (!iwlwifi_attached()) {
		board_case_fail("the driver never registered its hw within 60 s "
		    "(no device claimed, or its probe failed - see the lines "
		    "above)");
		return;
	}

	/*
	 * The driver's operational start. net80211's side exists from attach, but
	 * the chip only begins receiving after the driver's own start runs (the
	 * mac80211 start path brings the firmware's RX queues up) - the host
	 * suite takes the same step after its attach, and without it a sweep
	 * transmits but never hears a beacon.
	 */
	if (wlan_bridge_up() != 0) {
		board_case_fail("the driver's start chain refused");
		return;
	}

	wlan_bridge_stats(&c);
	LOG_INFO("wlan: probe done attached=%d rx=%u mgt_tx=%u scan=%u/%u\n",
		  c.attached, c.rx_frames, c.mgt_tx, c.scan_starts,
		  c.scan_completes);
}

BOARD_TEST("iwlwifi-probe", iwlwifi_probe_chain);
