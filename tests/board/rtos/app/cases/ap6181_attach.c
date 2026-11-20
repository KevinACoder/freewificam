/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   ap6181_attach.c
 * @brief  Case ap6181_attach: the module answers on SDIO and the driver's
 *         bring-up completes (enumeration, firmware download, WICED up, MAC).
 *
 * The image stops here on purpose.  Attach is where a wrong host sequence shows
 * first: enumeration is CMD0/5/3/7 at 400 kHz, then the firmware download is
 * the first long block-write burst, then WICED reads the chip identity over the
 * backplane.  Nothing later in the ladder can pass if this does not.
 *
 * Anchors:
 *   ap6181: stage=attach
 *   ap6181: fw bytes=<n>          the compiled-in resource WWD will download
 *   ap6181: wifi_on rc=<n>        wwd_management_wifi_on
 *   ap6181: sdio rca=.. bus=.. clock=.. intrs=.. retries=..
 *   ap6181: mac=<aa:bb:..>
 *   ap6181: PASS ATTACH | FAIL <reason>
 */

#include "cases.h"

#include <stdio.h>
#include <string.h>

#include "platform/wwd_platform_interface.h"
#include "platform/wwd_resource_interface.h"
#include "RTOS/wwd_rtos_interface.h"
#include "network/wwd_buffer_interface.h"
#include "wwd_constants.h"
#include "wwd_management.h"
#include "wwd_wifi.h"

#include "wiced_port.h"

static void
print_stats(void)
{
	CASE_INFO("ap6181: sdio rca=%u bus=%u clock=%lu intrs=%lu retries=%lu "
		  "ctimeouts=%lu dmaerr=%lu oob=%lu enum=%lu\n",
		  (unsigned)ap6181_stats.rca, (unsigned)ap6181_stats.bus_width,
		  (unsigned long)ap6181_stats.clock_hz,
		  (unsigned long)ap6181_stats.sdio_intrs,
		  (unsigned long)ap6181_stats.cmd_retries,
		  (unsigned long)ap6181_stats.cmd_timeouts,
		  (unsigned long)ap6181_stats.dma_errors,
		  (unsigned long)ap6181_stats.oob_intrs,
		  (unsigned long)ap6181_stats.enum_attempts);
	CASE_INFO("ap6181: dma intrs=%lu cmd53 isr=%lu dma=%lu ndtr=%lu "
		  "errsta=0x%08lx\n",
		  (unsigned long)ap6181_stats.dma_intrs,
		  (unsigned long)ap6181_stats.cmd53_isr,
		  (unsigned long)ap6181_stats.cmd53_dma,
		  (unsigned long)ap6181_stats.last_ndtr,
		  (unsigned long)ap6181_stats.last_err_sta);
	CASE_INFO("ap6181: last cmd=%lu arg=0x%08lx sta=0x%08lx dlen=%lu "
		  "dctrl=0x%08lx\n",
		  (unsigned long)ap6181_stats.last_cmd,
		  (unsigned long)ap6181_stats.last_arg,
		  (unsigned long)ap6181_stats.last_sta,
		  (unsigned long)ap6181_stats.last_dlen,
		  (unsigned long)ap6181_stats.last_dctrl);
}

static void
print_mac(const char *label, const uint8_t *mac)
{
	CASE_INFO("ap6181: %s=%02x:%02x:%02x:%02x:%02x:%02x\n", label,
		  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int
case_f429_ap6181_attach_run(void)
{
	wiced_mac_t mac;
	uint32_t fw_size = 0U;
	wwd_result_t rc;
	int result = -1;

	CASE_INFO("ap6181: stage=attach\n");

	/* WWD buffers are lwIP pbufs: the stack's pools must exist first. */
	ap6181_lwip_core_init();

	if (host_platform_resource_size(WWD_RESOURCE_WLAN_FIRMWARE,
					&fw_size) == WWD_SUCCESS)
		CASE_INFO("ap6181: fw bytes=%lu\n", (unsigned long)fw_size);

	(void)wwd_buffer_init(NULL);
	rc = wwd_management_wifi_on(WICED_COUNTRY_AUSTRALIA);
	CASE_INFO("ap6181: wifi_on rc=%d\n", (int)rc);
	print_stats();
	if (rc != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL wifi_on rc=%d\n", (int)rc);
		return -1;
	}

	rc = wwd_wifi_get_mac_address(&mac, WWD_STA_INTERFACE);
	if (rc != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL mac rc=%d\n", (int)rc);
		return -1;
	}
	print_mac("mac", mac.octet);

	if (ap6181_stats.enumerated == 0U) {
		CASE_ANCHOR("ap6181: FAIL not enumerated\n");
		return -1;
	}
	if (fw_size != 210624UL) {
		CASE_ANCHOR("ap6181: FAIL fw bytes=%lu (want 210624)\n",
			    (unsigned long)fw_size);
		return -1;
	}

	CASE_ANCHOR("ap6181: PASS ATTACH\n");
	result = 0;
	return result;
}