/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * rtw88_cmds.c - the rtw88 line's shell face (S9/rtw88-adapt W3):
 *
 *   wlanrtw up        power on + ops->start + add_interface (the bridge's
 *                     bring-up chain, deferred to the lkpi worker)
 *   wlanrtw scan      net80211 scan: one pass over the 14 b/g channels,
 *                     dwell ~200 ms each (the bridge drives the state
 *                     machine; beacons land through ieee80211_rx_irqsafe)
 *   wlanrtw bss       dump the scan table (SSID/chan/rssi/bssid)
 *   wlanrtw status    state machine position + rx drop counter
 *   wlanrtw hwaddr    the efuse MAC the bridge attached with
 *
 * The command layer only talks the bridge API (os/net80211/
 * net80211_bridge.h) - no net80211 type is visible here (FR-4).
 */

#include <stdint.h>
#include <string.h>

#include "cherrysh_adapter.h"
#include "csh.h"

#include "net80211_bridge.h"

static int
rtw88_cmd(int argc, char *argv[])
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	uint8_t mac[6];
	int ret;

	if (argc < 2) {
		csh_printf(csh, "usage: wlanrtw up|scan|bss|connect <ssid>|"
		    "status|hwaddr\n");
		return 0;
	}
	if (strcmp(argv[1], "up") == 0) {
		ret = net80211_bridge_up();
		csh_printf(csh, "wlanrtw: up -> %d\n", ret);
	} else if (strcmp(argv[1], "scan") == 0) {
		ret = net80211_bridge_scan(NULL, 0);
		csh_printf(csh, "wlanrtw: scan started -> %d "
		    "(wait ~3 s for the 14-channel dwell, then bss)\n", ret);
	} else if (strcmp(argv[1], "bss") == 0) {
		net80211_bridge_scan_dump();
		csh_printf(csh, "wlanrtw: scan table above\n");
	} else if (strcmp(argv[1], "connect") == 0) {
		if (argc < 3) {
			csh_printf(csh, "wlanrtw: connect <ssid>\n");
			return 0;
		}
		ret = net80211_bridge_connect(
		    (const uint8_t *)argv[2], strlen(argv[2]));
		csh_printf(csh, "wlanrtw: connect '%s' -> %d "
		    "(auth/assoc ~1 s; then status)\n", argv[2], ret);
	} else if (strcmp(argv[1], "status") == 0) {
		net80211_bridge_status();
	} else if (strcmp(argv[1], "hwaddr") == 0) {
		if (net80211_bridge_get_hwaddr(mac) == 0)
			csh_printf(csh, "wlanrtw: hwaddr "
			    "%02x:%02x:%02x:%02x:%02x:%02x\n",
			    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		csh_printf(csh, "wlanrtw: unknown '%s'\n", argv[1]);
	}
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(rtw88_cmd, wlanrtw,
    "wlanrtw up|scan|bss|connect|status|hwaddr",
    "rtw88 bring-up, scan and connect (S9 W3/W4)");
