/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   ap6181_c3.c
 * @brief  Case ap6181_c3: the onboard AP6181 (BCM43362) attaches through the
 *         vendored brcmfmac FullMAC driver over the linuxkpi SDIO engine,
 *         downloads its firmware and registers wlan0.
 *
 * The composition root is the SDIO line's: the kernel-API core, the fsl
 * adapter install, the module initcalls (cfg80211's and brcmfmac's
 * sdio_register_driver), then the engine's card bring-up, which probes the
 * registered driver.  brcmfmac's probe runs the chip backplane bring-up,
 * downloads the 43362 firmware + NVRAM through the compiled-in blobs, enables
 * function 2 and registers the wiphy/netdev.  The case waits for wlan0, opens
 * it and stops: scan/association/data are later gates.
 *
 * This is C3 only.  It does not claim a scan, an association or a frame.
 *
 * Anchors:
 *   ap6181_c3: core ready
 *   ap6181_c3: initcalls run
 *   ap6181_c3: card <vendor>:<device> funcs=<n>
 *   ap6181_c3: wlan0 present=<0|1> after <ms> ms
 *   ap6181_c3: start rc=<n>
 *   ap6181_c3: PASS ATTACH
 *   ap6181_c3: FAIL <reason>
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

#define AP6181_WAIT_MS		30000U
#define AP6181_POLL_MS		100U

/* The backend adapter's install entry (third-party/fsl_sdmmc/ports): the case
 * is the image's composition root. */
void lkpi_sdio_fsl_install(void);
void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));

static unsigned c3_log_lines;

static void
c3_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL || c3_log_lines >= 80U)
		return;
	/* The sink is registered after the boot lines, so everything it sees
	 * belongs to the stack/driver bring-up.  Keep all of it (bounded):
	 * the probe's failure line names no "brcm" prefix on this port's
	 * device model, and a keyword filter hid exactly that line. */
	CASE_ANCHOR("ap6181_c3: log %s\n", line);
	c3_log_lines++;
}

/* The compiled-in 43362 firmware and NVRAM (tools/rtos/fw-blobs.py): the
 * registry is this image's only filesystem, so the blob units must be
 * registered before brcmfmac's probe asks for them. */
int lkpi_fw_brcmfmac43362_fw_register(void);
int lkpi_fw_brcmfmac43362_nvram_register(void);

int
case_ap6181_c3_run(void)
{
	struct net_device *dev = NULL;
	struct sdio_func *f1;
	unsigned int waited = 0U;
	int rc;

	CASE_ANCHOR("ap6181_c3: core ready\n");
	lkpi_core_init();
	lkpi_log_sink_register(c3_sink);
	rc = lkpi_fw_brcmfmac43362_fw_register();
	CASE_ANCHOR("ap6181_c3: fw registered rc=%d\n", rc);
	if (rc != 0)
		return 1;
	rc = lkpi_fw_brcmfmac43362_nvram_register();
	CASE_ANCHOR("ap6181_c3: nvram registered rc=%d\n", rc);
	if (rc != 0)
		return 1;
	lkpi_sdio_fsl_install();
	lkpi_module_init_run();
	CASE_ANCHOR("ap6181_c3: initcalls run\n");

	lkpi_sdio_start();
	f1 = lkpi_sdio_func(1U);
	if (f1 == NULL) {
		CASE_ANCHOR("ap6181_c3: FAIL no SDIO function 1 (card not up)\n");
		return 1;
	}
	CASE_ANCHOR("ap6181_c3: card %04x:%04x funcs=%u\n",
		    (unsigned)f1->vendor, (unsigned)f1->device,
		    (lkpi_sdio_func(2U) != NULL) ? 2U : 1U);
	{
		struct lkpi_sdio_stats st;

		lkpi_sdio_stats_get(&st);
		CASE_ANCHOR("ap6181_c3: engine cards=%u drivers=%u probes=%u "
			    "io=%u/%u errors=%u\n", st.cards, st.drivers,
			    st.probes, st.io_direct, st.io_extended, st.errors);
	}

	while (waited < AP6181_WAIT_MS) {
		struct lkpi_sdio_stats st;

		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(AP6181_POLL_MS);
		waited += AP6181_POLL_MS;
		if ((waited % 5000U) == 0U) {
			lkpi_sdio_stats_get(&st);
			void *tcb = lkpi_sdio_claim_owner_thread();
			unsigned int pc = 0U, lr = 0U;

			if (tcb != NULL) {
				uint32_t *sp = *(uint32_t **)tcb;

				if (sp != NULL) {
					lr = sp[14];
					pc = sp[15];
				}
			}
			CASE_ANCHOR("ap6181_c3: wait ms=%u wlan0=%d io=%u/%u "
				    "errors=%u claim=%s/%s/%u pump=%s "
				    "pc=%08x lr=%08x\n",
				    waited, dev != NULL, st.io_direct,
				    st.io_extended, st.errors,
				    lkpi_sdio_claim_owner(),
				    lkpi_sdio_claim_op(),
				    lkpi_sdio_claim_held_ms(),
				    lkpi_sdio_pump_stage(), pc, lr);
		}
	}
	CASE_ANCHOR("ap6181_c3: wlan0 present=%d after %u ms\n",
		    dev != NULL, waited);
	if (dev == NULL) {
		CASE_ANCHOR("ap6181_c3: FAIL no wlan0 after %u ms\n", waited);
		return 2;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("ap6181_c3: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("ap6181_c3: FAIL start rc=%d\n", rc);
		return 3;
	}

	CASE_ANCHOR("ap6181_c3: PASS ATTACH\n");
	return 0;
}
