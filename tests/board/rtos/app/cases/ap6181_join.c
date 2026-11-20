/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ap6181_join.c - G3: associate with the bench AP.
 *
 * Anchor (one line, greppable):
 *   ap6181: PASS JOIN
 * Above it the case reports the join result, the BSSID the chip says it is
 * associated with, and the RSSI.  The BSSID must match the AP the case asked
 * for: "join returned success" alone is not this gate.
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

/* The bench AP: the Kali Pi's 802.11n AP, channel 5, open. */
#define JOIN_SSID	"labwifi-ar-ht20s"
#define JOIN_BSSID_0	0x1c
#define JOIN_BSSID_1	0xa7
#define JOIN_BSSID_2	0x70
#define JOIN_BSSID_3	0xde
#define JOIN_BSSID_4	0x29
#define JOIN_BSSID_5	0xa1

static host_semaphore_type_t s_join_sem;

int
case_f429_ap6181_join_run(void)
{
	wiced_ssid_t ssid;
	wiced_mac_t bssid;
	wwd_result_t rc;
	int32_t rssi = 0;
	bool bssid_ok;

	CASE_INFO("ap6181: stage=join\n");

	ap6181_lwip_core_init();
	(void)wwd_buffer_init(NULL);
	rc = wwd_management_wifi_on(WICED_COUNTRY_AUSTRALIA);
	CASE_INFO("ap6181: wifi_on rc=%d\n", (int)rc);
	if (rc != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL wifi_on rc=%d\n", (int)rc);
		return -1;
	}

	if (host_rtos_init_semaphore(&s_join_sem) != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL join semaphore\n");
		return -1;
	}

	ssid.length = (uint8_t)(sizeof(JOIN_SSID) - 1U);
	memcpy(ssid.value, JOIN_SSID, sizeof(JOIN_SSID) - 1U);

	rc = wwd_wifi_join(&ssid, WICED_SECURITY_OPEN, NULL, 0U, &s_join_sem,
			   WWD_STA_INTERFACE);
	CASE_INFO("ap6181: join rc=%d\n", (int)rc);
	if (rc != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL join rc=%d\n", (int)rc);
		return -1;
	}

	/* wwd_wifi_join() returns once the request is accepted; the link comes
	 * up with the association event, so wait for it instead of sampling
	 * immediately. */
	{
		unsigned wait;

		for (wait = 0U; wait < 50U; wait++) {
			if (wwd_wifi_is_ready_to_transceive(WWD_STA_INTERFACE) ==
			    WWD_SUCCESS)
				break;
			host_rtos_delay_milliseconds(100U);
		}
		CASE_INFO("ap6181: link wait=%ums\n", wait * 100U);
	}

	memset(&bssid, 0, sizeof(bssid));
	if (wwd_wifi_get_bssid(&bssid) == WWD_SUCCESS) {
		CASE_INFO("ap6181: bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
			  bssid.octet[0], bssid.octet[1], bssid.octet[2],
			  bssid.octet[3], bssid.octet[4], bssid.octet[5]);
	}
	(void)wwd_wifi_get_rssi(&rssi);
	CASE_INFO("ap6181: rssi=%d\n", (int)rssi);

	if (wwd_wifi_is_ready_to_transceive(WWD_STA_INTERFACE) != WWD_SUCCESS) {
		CASE_ANCHOR("ap6181: FAIL not ready to transceive\n");
		return -1;
	}

	bssid_ok = (bssid.octet[0] == JOIN_BSSID_0 &&
		    bssid.octet[1] == JOIN_BSSID_1 &&
		    bssid.octet[2] == JOIN_BSSID_2 &&
		    bssid.octet[3] == JOIN_BSSID_3 &&
		    bssid.octet[4] == JOIN_BSSID_4 &&
		    bssid.octet[5] == JOIN_BSSID_5);
	if (!bssid_ok) {
		CASE_ANCHOR("ap6181: FAIL bssid mismatch\n");
		return -1;
	}

	CASE_ANCHOR("ap6181: PASS JOIN\n");
	return 0;
}
