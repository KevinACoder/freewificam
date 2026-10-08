/**
 * @file    wlan_cmds.c
 * @brief   The console command face of the WLAN image: wlan [status|probe|scan|dump].
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  console command row - same shape as the GMAC dataplane's command set
 *          (tests/board/threadx-rk3568-regression/tests/gmac_dataplane.c:312-316,
 *          :443-449): CSH_CMD_EXPORT_ALIAS_FULL registers the name, the handler
 *          reads the shell from argv[argc + 1] and prints with csh_printf, and
 *          the first argument lives at argv[1]. The command is the human re-run
 *          path for the same chain the cases exercise.
 */

#include <string.h>

#include "csh.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

/* net80211 world: the bridge's scalar face */
#include "wlan_bridge.h"

/* the cases' shared bodies carry the anchors; the command reuses them */
void wlan_probe_chain(void);
void wlan_scan_chain(void);

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
		wlan_probe_chain();
		return 0;
	}
	if (strcmp(argv[1], "scan") == 0) {
		wlan_scan_chain();
		return 0;
	}
	if (strcmp(argv[1], "dump") == 0) {
		wlan_port_scan_dump();
		return 0;
	}
	csh_printf(csh, "usage: wlan [status|probe|scan|dump]\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wlan, wlan, "wlan [status|probe|scan|dump]",
			  "wlan status - bridge counters and state;\n"
			  "wlan probe - bring up the USB chain and wait for the driver;\n"
			  "wlan scan - sweep the channels and compare with the ground truth;\n"
			  "wlan dump - print the scan table as found\n")
