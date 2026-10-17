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

#include <stdio.h>
#include <string.h>

#include "csh.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

/* net80211 world: the bridge's scalar face */
#include "wlan_bridge.h"

/* the image's census snapshot (the same call the dataplane case makes) */
#include "wlan_census.h"

/* lwIP's surface for the measurement commands (the same lwiperf engine the
 * wired line's image drives, tests/board/threadx-rk3568-regression/tests/
 * gmac_dataplane.c:385-441) */
#include "lwip/tcpip.h"
#include "lwip/apps/lwiperf.h"

/* the cases' shared bodies carry the anchors; the command reuses them */
void wlan_probe_chain(void);
void wlan_scan_chain(void);
void wlan_join_chain(void);
void wlan_dataplane_chain(void);

/* the dataplane case's server session (started by the case, re-driven here) */
void wlan_dataplane_iperf_cmd(chry_shell_t *csh, int argc, char **argv);

/* the dataplane case's uplink peer: the reverse direction's far end, named
 * here and read by the case when its uplink phases run */
void wlan_uplink_peer_set(unsigned int peer);

/* `a.b.c.d` into the host-order word the case splits back into an lwIP
 * address: parsed here and handed over as a plain number, so the one place an
 * lwIP address is built stays in the case that uses it */
static int
parse_ipv4(const char *s, unsigned int *out)
{
	unsigned int b[4];

	if (sscanf(s, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) != 4)
		return -1;
	if (b[0] > 255U || b[1] > 255U || b[2] > 255U || b[3] > 255U)
		return -1;
	*out = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
	return 0;
}

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
	if (strcmp(argv[1], "census") == 0) {
		if (argc >= 3 && strcmp(argv[2], "reset") == 0)
			wlan_census_reset();
		else
			wlan_census_print();
		return 0;
	}
	if (strcmp(argv[1], "join") == 0) {
		wlan_join_chain();
		return 0;
	}
	if (strcmp(argv[1], "dataplane") == 0) {
		wlan_dataplane_chain();
		return 0;
	}
	if (strcmp(argv[1], "iperf") == 0) {
		wlan_dataplane_iperf_cmd(csh, argc - 1, argv + 1);
		return 0;
	}
	if (strcmp(argv[1], "uplink") == 0) {
		unsigned int peer;

		if (argc < 3 || parse_ipv4(argv[2], &peer) != 0) {
			csh_printf(csh, "usage: wlan uplink <a.b.c.d>\n");
			return 0;
		}
		wlan_uplink_peer_set(peer);
		csh_printf(csh, "wlan: uplink peer=%u.%u.%u.%u configured\n",
		    (peer >> 24) & 0xffU, (peer >> 16) & 0xffU,
		    (peer >> 8) & 0xffU, peer & 0xffU);
		return 0;
	}
	csh_printf(csh, "usage: wlan [status|probe|scan|dump|census [reset]|join|"
	    "dataplane|iperf <ip>|server|stop|uplink <ip>]\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wlan, wlan, "wlan [status|probe|scan|dump|census|join|dataplane|iperf|uplink]",
			  "wlan status - bridge counters and state;\n"
			  "wlan probe - bring up the USB chain and wait for the driver;\n"
			  "wlan scan - sweep the channels and compare with the ground truth;\n"
			  "wlan dump - print the scan table as found;\n"
			  "wlan census [reset] - print (or zero) the throughput census snapshot;\n"
			  "wlan join - wait for the supplicant's WPA2 association;\n"
			  "wlan dataplane - netif + DHCP + ping + iperf server + the phases;\n"
			  "wlan iperf <ip>|server|stop - the TCP measurement session;\n"
			  "wlan uplink <ip> - name the far end for the uplink phases\n")
