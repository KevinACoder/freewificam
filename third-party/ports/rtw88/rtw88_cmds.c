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
#include "rtw88_d3_diag.h"

#ifdef CONFIG_WLAN_NETIF
extern int wlanrtw_netif_ip(void);
extern void wlanrtw_netif_stats(unsigned long *rx_fed,
    unsigned long *rx_drop, unsigned long *tx_frames, unsigned long *tx_err);
#endif

static int
rtw88_cmd(int argc, char *argv[])
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	uint8_t mac[6];
	int ret;

	if (argc < 2) {
		csh_printf(csh, "usage: wlanrtw up|scan|bss|connect <ssid>|"
		    "status|hwaddr|diag [reset]|quiet on|off\n");
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
	} else if (strcmp(argv[1], "ip") == 0) {
#ifdef CONFIG_WLAN_NETIF
		unsigned long rf, rd, tf, te;
		unsigned int q, s, d, np;

		ret = wlanrtw_netif_ip();
		wlanrtw_netif_stats(&rf, &rd, &tf, &te);
		net80211_bridge_tx_stats(&q, &s, &d, &np);
		net80211_bridge_rx_stats(NULL, NULL, NULL, NULL);
		csh_printf(csh, "wlanrtw: netif %s rx_fed %lu drop %lu "
		    "tx %lu err %lu | txq %u sent %u drop %u nopeer %u\n",
		    ret == 0 ? "BOUND" : "not-bound", rf, rd, tf, te,
		    q, s, d, np);
#else
		csh_printf(csh, "wlanrtw: ip needs CONFIG_WLAN_NETIF\n");
#endif
	} else if (strcmp(argv[1], "hwaddr") == 0) {
		if (net80211_bridge_get_hwaddr(mac) == 0)
			csh_printf(csh, "wlanrtw: hwaddr "
			    "%02x:%02x:%02x:%02x:%02x:%02x\n",
			    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else if (strcmp(argv[1], "diag") == 0) {
		if (argc >= 3 && strcmp(argv[2], "reset") == 0) {
			rtw_d3_usb_rx_diag_reset();
			rtw_d3_query_diag_reset();
			rtw_d3_tx_diag_reset();
			csh_printf(csh, "wlanrtw: d3 counters reset\n");
		} else if (argc >= 3 && strcmp(argv[2], "regs") == 0) {
			rtw_d3_regs_dump();
		} else if (argc >= 3 && strcmp(argv[2], "txhex") == 0) {
			rtw_d3_tx_diag_dump();
		} else {
			rtw_d3_usb_rx_diag_dump();
			rtw_d3_query_diag_dump();
			lkpi_skb_diag_dump();
			csh_printf(csh, "wlanrtw: d3 dumps above "
			    "(regs/txhex subcommands; usbkpi rxq for the pump gap)\n");
		}
	} else if (strcmp(argv[1], "quiet") == 0) {
		if (argc < 3) {
			csh_printf(csh, "wlanrtw: quiet on|off "
			    "(RCR AM|AB, D-3 diagnostic)\n");
			return 0;
		}
		rtw_d3_rcr_quiet(strcmp(argv[2], "on") == 0 ? 1 : 0);
	} else if (strcmp(argv[1], "promisc") == 0) {
		if (argc < 3) {
			csh_printf(csh, "wlanrtw: promisc on|off "
			    "(RCR AAP accept-all-unicast, D-3 diagnostic)\n");
			return 0;
		}
		rtw_d3_rcr_promisc(strcmp(argv[2], "on") == 0 ? 1 : 0);
	} else if (strcmp(argv[1], "swseq") == 0) {
		if (argc < 3) {
			csh_printf(csh, "wlanrtw: swseq on|off "
			    "(mgmt radiates the header seq instead of the FW "
			    "seq counter, D-3 fork)\n");
			return 0;
		}
		rtw_d3_mgmt_swseq = strcmp(argv[2], "on") == 0 ? 1 : 0;
		csh_printf(csh, "wlanrtw: swseq %s\n",
		    rtw_d3_mgmt_swseq ? "ON" : "OFF");
	} else if (strcmp(argv[1], "mac") == 0) {
		uint8_t na[6];
		const char *s = argc >= 3 ? argv[2] : "";
		int k, ok = 1;

		for (k = 0; k < 6 && ok; k++) {
			int b[2], j;

			if (k != 0 && *s == ':')
				s++;
			for (j = 0; j < 2; j++) {
				char c = *s++;

				if (c >= '0' && c <= '9')
					b[j] = c - '0';
				else if (c >= 'a' && c <= 'f')
					b[j] = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F')
					b[j] = c - 'A' + 10;
				else {
					ok = 0;
					break;
				}
			}
			if (ok)
				na[k] = (uint8_t)((b[0] << 4) | b[1]);
		}
		if (!ok) {
			csh_printf(csh, "wlanrtw: mac 02:aa:bb:cc:dd:ee "
			    "(before up; D-3 diagnostic)\n");
			return 0;
		}
		net80211_bridge_set_mac(na);
	} else if (strcmp(argv[1], "cmpdetune") == 0) {
		uint8_t na[6];
		const char *s = argc >= 3 ? argv[2] : "";
		int k, ok = 1;

		for (k = 0; k < 6 && ok; k++) {
			int b[2], j;

			if (k != 0 && *s == ':')
				s++;
			for (j = 0; j < 2; j++) {
				char c = *s++;

				if (c >= '0' && c <= '9')
					b[j] = c - '0';
				else if (c >= 'a' && c <= 'f')
					b[j] = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F')
					b[j] = c - 'A' + 10;
				else {
					ok = 0;
					break;
				}
			}
			if (ok)
				na[k] = (uint8_t)((b[0] << 4) | b[1]);
		}
		if (!ok) {
			csh_printf(csh, "wlanrtw: cmpdetune 02:00:00:00:00:01 "
			    "(point the port comparator at M'; apply DURING a "
			    "scan - scan start/complete rewrite it)\n");
			return 0;
		}
		rtw_d3_cmp_detune(na);
	} else if (strcmp(argv[1], "cmprestore") == 0) {
		rtw_d3_cmp_restore();
	} else if (strcmp(argv[1], "verbose") == 0) {
		if (argc < 3) {
			csh_printf(csh, "wlanrtw: verbose on|off "
			    "(per-probe forensic prints, default off)\n");
			return 0;
		}
		rtw_d3_verbose_set(strcmp(argv[2], "on") == 0 ? 1 : 0);
		csh_printf(csh, "wlanrtw: verbose %s\n",
		    strcmp(argv[2], "on") == 0 ? "ON" : "OFF");
	} else {
		csh_printf(csh, "wlanrtw: unknown '%s'\n", argv[1]);
	}
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(rtw88_cmd, wlanrtw,
    "wlanrtw up|scan|bss|connect|status|hwaddr",
    "rtw88 bring-up, scan and connect (S9 W3/W4)");
