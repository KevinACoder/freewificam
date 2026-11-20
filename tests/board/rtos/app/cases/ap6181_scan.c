/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ap6181_scan.c - G2: bring the WLAN up and ask it for a scan.
 *
 * Anchor (one line, greppable):
 *   ap6181: PASS SCAN
 * with "ap6181: scan n=<n> first=<ssid>/<bssid>/ch<ch>" above it.  The case
 * fails if the bring-up fails, if the scan never completes, or if the scan
 * returns no BSS at all: an empty scan is a failure of this gate, not a quiet
 * pass.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "platform/wwd_platform_interface.h"
#include "platform/wwd_resource_interface.h"
#include "RTOS/wwd_rtos_interface.h"
#include "network/wwd_buffer_interface.h"
#include "wwd_constants.h"
#include "wwd_management.h"
#include "wwd_wifi.h"

#include "wiced_port.h"

#define SCAN_SLOTS	24U
#define SCAN_WAIT_MS	8000U

static wiced_scan_result_t s_results[SCAN_SLOTS];
static host_semaphore_type_t s_scan_sem;
static volatile unsigned s_written;
static volatile bool s_done;

static void
scan_cb(wiced_scan_result_t **result_ptr, void *user_data,
	wiced_scan_status_t status)
{
	wiced_scan_result_t *next;
	unsigned slot;

	(void)user_data;
	(void)status;

	if (result_ptr == NULL || *result_ptr == NULL) {
		s_done = true;
		(void)host_rtos_set_semaphore(&s_scan_sem, WICED_FALSE);
		return;
	}

	slot = s_written < SCAN_SLOTS ? s_written : (SCAN_SLOTS - 1U);
	if (s_written < SCAN_SLOTS) {
		memcpy(&s_results[slot], *result_ptr, sizeof(s_results[slot]));
		s_written++;
	}

	next = &s_results[slot];
	*result_ptr = next;
	(void)host_rtos_set_semaphore(&s_scan_sem, WICED_FALSE);
}

static void
print_bss(unsigned index, const wiced_scan_result_t *r)
{
	char ssid[33];
	unsigned i;
	unsigned len = r->SSID.length;

	if (len > sizeof(ssid) - 1U)
		len = sizeof(ssid) - 1U;
	for (i = 0U; i < len; i++)
		ssid[i] = (char)r->SSID.value[i];
	ssid[len] = '\0';

	CASE_INFO("ap6181: bss%u ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x "
		  "ch=%u rssi=%d\n", index, ssid,
		  r->BSSID.octet[0], r->BSSID.octet[1], r->BSSID.octet[2],
		  r->BSSID.octet[3], r->BSSID.octet[4], r->BSSID.octet[5],
		  (unsigned)r->channel, (int)r->signal_strength);
}

int
case_f429_ap6181_scan_run(void)
{
	wiced_scan_result_t *result_ptr = &s_results[0];
	wwd_result_t rc;
	uint32_t waited = 0U;
	unsigned i;

	CASE_INFO("ap6181: stage=scan\n");

	ap6181_lwip_core_init();
	(void)wwd_buffer_init(NULL);
	rc = wwd_management_wifi_on(WICED_COUNTRY_AUSTRALIA);
	CASE_INFO("ap6181: wifi_on rc=%d\n", (int)rc);
	if (rc != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL wifi_on rc=%d\n", (int)rc);
		return -1;
	}

	if (host_rtos_init_semaphore(&s_scan_sem) != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL scan semaphore\n");
		return -1;
	}

	rc = wwd_wifi_scan(WICED_SCAN_TYPE_ACTIVE, WICED_BSS_TYPE_ANY,
			   NULL, NULL, NULL, NULL, scan_cb, &result_ptr, NULL,
			   WWD_STA_INTERFACE);
	CASE_INFO("ap6181: scan start rc=%d\n", (int)rc);
	if (rc != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL scan start rc=%d\n", (int)rc);
		return -1;
	}

	while (!s_done && waited < SCAN_WAIT_MS) {
		if (host_rtos_get_semaphore(&s_scan_sem, 100U, WICED_FALSE) ==
		    WWD_SUCCESS) {
			continue;
		}
		waited += 100U;
	}

	CASE_INFO("ap6181: scan done=%d n=%u waited=%lums\n",
		  (int)(s_done ? 1 : 0), s_written, (unsigned long)waited);

	for (i = 0U; i < s_written && i < 8U; i++)
		print_bss(i, &s_results[i]);

	if (!s_done) {
		CASE_ANCHOR("ap6181: FAIL scan timeout\n");
		return -1;
	}
	if (s_written == 0U) {
		CASE_ANCHOR("ap6181: FAIL scan empty\n");
		return -1;
	}

	CASE_ANCHOR("ap6181: PASS SCAN\n");
	return 0;
}
