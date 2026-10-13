/**
 * @file    iwlwifi_cmds.c
 * @brief   The console command face of the iwlwifi image: wlan [status|probe|
 *          scan|join|dataplane|iperf].
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  console command row - same shape as the WLAN images' command set
 *          (tests/board/threadx-rk3568-rtw89/tests/rtw89_cmds.c): CSH_CMD_EXPORT_
 *          ALIAS_FULL registers the name, the handler reads the shell from
 *          argv[argc + 1] and prints with csh_printf, and the first argument
 *          lives at argv[1]. The command is the human re-run path for the same
 *          chain the cases exercise.
 */

#include <string.h>

#include "csh.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

/* net80211 world: the bridge's scalar face */
#include "wlan_bridge.h"

/* the cases' shared bodies carry the anchors; the command reuses them */
void iwlwifi_probe_chain(void);
void iwlwifi_scan_chain(void);
void iwlwifi_join_chain(void);
void iwlwifi_dataplane_chain(void);
/* the measurement session's console face (tests/iwlwifi_dataplane.c) */
void iwlwifi_dataplane_iperf_cmd(chry_shell_t *csh, int argc, char **argv);
/* the driver's debug mask (tests/iwlwifi_dbg.c, compiled with the driver's
 * headers under CONFIG_IWLWIFI_DEBUG) */
void iwlwifi_set_debug_level(unsigned int mask);

static int
cmd_wlan(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];

	if (argc < 2 || strcmp(argv[1], "status") == 0) {
		struct wlan_bridge_counters c;

		wlan_bridge_stats(&c);
		csh_printf(csh, "wlan: attached=%d state=%d mgt_tx=%u data_tx=%u "
		    "rx=%u tx_done=%u scan=%u/%u filter=%u/%u\n", c.attached,
		    wlan_bridge_state(), c.mgt_tx, c.data_tx, c.rx_frames,
		    c.tx_completes, c.scan_starts, c.scan_completes,
		    c.filter_sets, c.filter_clears);
		return 0;
	}
	if (strcmp(argv[1], "probe") == 0) {
		iwlwifi_probe_chain();
		return 0;
	}
	if (strcmp(argv[1], "scan") == 0) {
		iwlwifi_scan_chain();
		return 0;
	}
	if (strcmp(argv[1], "join") == 0) {
		iwlwifi_join_chain();
		return 0;
	}
	if (strcmp(argv[1], "dataplane") == 0) {
		iwlwifi_dataplane_chain();
		return 0;
	}
	if (strcmp(argv[1], "iperf") == 0) {
		iwlwifi_dataplane_iperf_cmd(csh, argc - 1, argv + 1);
		return 0;
	}
	if (strcmp(argv[1], "dbg") == 0) {
		unsigned int mask = 0;

		if (argc >= 3) {
			const char *p = argv[2];

			if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
				p += 2;
			while (*p != '\0') {
				unsigned int d;

				if (*p >= '0' && *p <= '9')
					d = (unsigned int)(*p - '0');
				else if (*p >= 'a' && *p <= 'f')
					d = (unsigned int)(*p - 'a' + 10);
				else if (*p >= 'A' && *p <= 'F')
					d = (unsigned int)(*p - 'A' + 10);
				else
					break;
				mask = (mask << 4) | d;
				p++;
			}
		}
		iwlwifi_set_debug_level(mask);
		csh_printf(csh, "wlan: iwlwifi debug_level=0x%x\n", mask);
		return 0;
	}
	csh_printf(csh, "usage: wlan [status|probe|scan|join|dataplane|iperf|dbg <mask>]\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wlan, wlan,
			  "wlan [status|probe|scan|join|dataplane|iperf|dbg]",
			  "wlan status - bridge counters and state;\n"
			  "wlan probe - bring up the PCIe chain and wait for the driver;\n"
			  "wlan scan - sweep the channels and compare with the ground truth;\n"
			  "wlan join - wait for the supplicant's WPA2 association;\n"
			  "wlan dataplane - lwIP over the bridge, DHCP, ping, iperf server;\n"
			  "wlan iperf <ip>|server|stop - the TCP measurement session;\n"
			  "wlan dbg <hex> - the driver's debug mask (needs CONFIG_IWLWIFI_DEBUG)\n")
