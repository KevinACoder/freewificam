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
 *   iwl7260: core wq=<state> runs=<n> full=<n> max=<n>
 *   iwl7260: start ok attached=<n> rx=<n>
 *   iwl7260: PASS
 *
 * No endpoint / no device: `iwl7260: platform-inactive <reason>` and return 2.
 */

#include <stdint.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"
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
	/*
	 * The linuxkpi core services first.  `lkpi_system_wq` is the executor
	 * every deferred driver path rides on, and with no queue `schedule_work()`
	 * returns false without a word (linuxkpi_core.c:1125-1131,
	 * linuxkpi_core.h:2840).  The AC 7260 defers two things through it, and
	 * both are on this boot's path:
	 *
	 *  - the firmware scan's completion is an ASYNC rx handler
	 *    (mvm/ops.c:366-369, which reaches :1775's
	 *    schedule_work(&mvm->async_handlers_wk)), so without the queue the
	 *    sweep fills the table and the window is never closed - the board
	 *    showed `scan=1/0 close=0` on two boots (L3-05 f1/f1b);
	 *  - a station's per-TID TXQ is allocated lazily by add_stream_wk
	 *    (mvm/mac80211.c:928-950, mvm/sta.c:1518+, which is the TRM's own
	 *    verified row, fw-trm/iwl7260/05-sequences.md 5.6 row 1), so without
	 *    the queue the first management frame is queued and never read out -
	 *    `mgt_tx=2 txc=0` with no authentication frame on the air.
	 *
	 * Nothing else in this image creates it: the wlan (SDIO) image does, from
	 * its own start-up chain (cases/wlan.c:370, reached through main.c's
	 * RTOS_CASE_WLAN arm), and the reference image states the rule for its
	 * chain - "without it that work is silently dropped"
	 * (tests/board/threadx-rk3568-iwlwifi/main.c:140-150, documented only).
	 */
	lkpi_core_init();
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

	/*
	 * The deferred-work service, made visible: `wq=-` is the defect this
	 * case's start-up now removes (no system queue at all), and `runs` is
	 * how much deferred driver work has already run by the time the attach
	 * completed.  A scan or join case that fails beside these numbers says
	 * whether the executor exists at all.
	 */
	{
		struct lkpi_wq_stats wq;

		lkpi_wq_stats_get(&wq);
		CASE_ANCHOR("iwl7260: core wq=%s runs=%u full=%u max=%u\n",
			    lkpi_wq_state(), (unsigned)wq.runs, (unsigned)wq.full,
			    (unsigned)wq.queue_max);
	}

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
