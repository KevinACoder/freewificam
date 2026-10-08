/*
 * @file   wlan_radio.c
 * @brief  The radio's dwell pump: while a sweep is in flight, hand the stack
 *         its next channel at the dwell period.
 *
 * net80211's software scan arms one channel and stands still until the driver
 * reports that channel done; that report is ieee80211_next_scan(), and with
 * no callouts in this port somebody has to make the call. A case cannot own
 * it: a supplicant requests sweeps of its own, on its own thread, whenever it
 * wants - which is why the pump is a thread of the image, the job the
 * predecessor's dwell timer did on its kernel.
 *
 * @design driver-worker pattern - ref linux:rtl8xxxu/core.c:5464 (the
 *         driver-owned delayed dwell that reports the channel back), and the
 *         predecessor's own dwell timer (its timer callback queued the work;
 *         here the pump is one dedicated thread because this port has no
 *         timer-list callout for it). It is idle (50 ms naps) whenever no
 *         sweep is armed, so it costs nothing outside a scan.
 *
 * @author zhugengyu
 * @date    09.10.2026
 */

#include <stdint.h>

#include "os/cmsis2/cmsis2.h"

#include "log.h"

#include "wlan_bridge.h"

/* the per-channel dwell: one beacon interval plus one probe attempt at this
 * card's rates (the sweep the board-passed case uses) */
#define WLAN_RADIO_DWELL_MS	200U

static void
wlan_radio_task(void *arg)
{

	(void)arg;
	for (;;) {
		if (wlan_bridge_scan_active()) {
			osDelay(WLAN_RADIO_DWELL_MS);
			/* the sweep may have ended inside the delay; the
			 * bridge's dwell refuses when no sweep is armed */
			(void)wlan_bridge_scan_dwell();
		} else {
			osDelay(50U);
		}
	}
}

int
wlan_radio_start(void)
{
	static const osThreadAttr_t attr = {
		.name = "wlan-radio",
		.priority = osPriorityNormal,
		.stack_size = 2048U,
	};

	if (osThreadNew(wlan_radio_task, NULL, &attr) == NULL) {
		board_log("wlan: radio pump FAIL\n");
		return -1;
	}
	board_log("wlan: radio dwell pump up\n");
	return 0;
}
