/**
 * @file    wlan8189_cmds.c
 * @brief   The console command face of the RTL8189FTV image: wlan8189
 *          [status|probe|scan|census|ps|rate].
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  console command row - same shape as the SDIO image's command set
 *          (tests/board/threadx-rk3568-sdio/tests/sdio_cmds.c):
 *          CSH_CMD_EXPORT_ALIAS_FULL registers the name, the handler reads the
 *          shell from argv[argc + 1] and prints with csh_printf, and the first
 *          argument lives at argv[1]. The command is the human re-run path for
 *          the same chains the cases exercise; the census subcommands call the
 *          same snapshot/readout functions the dataplane case brackets its
 *          phases with, so an operator can re-run any piece by hand.
 *
 * The status read goes through the substitute only, so the command file carries
 * no dependency on the driver's internals: it reports whether a wireless device
 * has been registered and how many access points the table holds, and says so in
 * one fixed shape whether or not the driver has attached yet.
 */

#include <string.h>

#include "csh.h"

#include "linuxkpi_cfg80211.h"

#include "wlan8189_cases.h"
#include "wlan8189_census.h"

/*
 * The uplink command's address, parsed here: this file links no lwIP world (so
 * no inet_aton), and this port's address vocabulary is a strict IPv4 dotted
 * quad - four decimal octets, each 1..3 digits and <= 255, nothing else. The
 * value is the human order (a<<24|b<<16|c<<8|d); the dataplane case turns it
 * into an lwIP address without assuming a byte order.
 */
static int
parse_ipv4(const char *s, unsigned int *out)
{
	unsigned int octets[4];
	unsigned int value = 0U;
	unsigned int digits = 0U;
	unsigned int field = 0U;
	const char *p = s;

	for (; *p != '\0'; p++) {
		if (*p >= '0' && *p <= '9') {
			value = value * 10U + (unsigned int)(*p - '0');
			if (++digits > 3U || value > 255U)
				return -1;
			continue;
		}
		if (*p != '.' || digits == 0U || field >= 3U)
			return -1;
		octets[field++] = value;
		value = 0U;
		digits = 0U;
	}
	if (digits == 0U || field != 3U)
		return -1;
	octets[3] = value;
	*out = (octets[0] << 24) | (octets[1] << 16) |
	    (octets[2] << 8) | octets[3];
	return 0;
}

static int
cmd_wlan8189(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];
	struct wiphy *wiphy = lkpicfg_wiphy();

	if (argc < 2 || strcmp(argv[1], "status") == 0) {
		/*
		 * The line has one shape whether or not the driver is up. The console
		 * steps are sent as soon as the suite's start line appears, which can be
		 * before the probe case has run, so a status line whose shape depended
		 * on that would make the interaction step a race rather than a check.
		 */
		csh_printf(csh, "wlan8189: attached=%d bss=%u\n",
			   (wiphy != NULL) ? 1 : 0,
			   (wiphy != NULL) ? lkpicfg_bss_count(wiphy) : 0U);
		return 0;
	}
	if (strcmp(argv[1], "probe") == 0) {
		wlan8189_probe_chain();
		return 0;
	}
	if (strcmp(argv[1], "scan") == 0) {
		wlan8189_scan_chain();
		return 0;
	}
	if (strcmp(argv[1], "census") == 0) {
		if (argc >= 3 && strcmp(argv[2], "reset") == 0)
			wlan8189_census_reset();
		else
			wlan8189_census_print();
		return 0;
	}
	if (strcmp(argv[1], "ps") == 0) {
		if (argc >= 4 && strcmp(argv[2], "ips") == 0)
			(void)wlan8189_ps_ips_set(atoi(argv[3]));
		else if (argc >= 3 && strcmp(argv[2], "show") != 0)
			(void)wlan8189_ps_set(atoi(argv[2]));
		wlan8189_ps_show();
		return 0;
	}
	if (strcmp(argv[1], "rate") == 0) {
		wlan8189_rate_show();
		return 0;
	}
	if (strcmp(argv[1], "pump") == 0) {
		if (argc >= 3 && strcmp(argv[2], "show") != 0)
			(void)wlan8189_pump_set(argv[2]);
		wlan8189_pump_show();
		return 0;
	}
	if (strcmp(argv[1], "agg") == 0) {
		if (argc >= 3 && strcmp(argv[2], "show") != 0) {
			if (strcmp(argv[2], "timeout") == 0) {
				if (argc >= 4)
					wlan8189_agg_timeout_set(
					    (unsigned int)atoi(argv[3]));
			} else {
				wlan8189_agg_set((unsigned int)atoi(argv[2]));
			}
		}
		wlan8189_card_stats_show();
		return 0;
	}
	if (strcmp(argv[1], "dwc") == 0) {
		if (argc >= 4 && strcmp(argv[2], "data") == 0)
			(void)wlan8189_dwc_data_set((unsigned int)atoi(argv[3]));
		else if (argc >= 3 && strcmp(argv[2], "show") != 0)
			(void)wlan8189_dwc_poll_set((unsigned int)atoi(argv[2]));
		wlan8189_dwc_show();
		return 0;
	}
	if (strcmp(argv[1], "irq") == 0) {
		if (argc >= 3 && strcmp(argv[2], "show") != 0)
			(void)wlan8189_irq_set(strcmp(argv[2], "on") == 0);
		wlan8189_irq_show();
		return 0;
	}
	if (strcmp(argv[1], "uplink") == 0) {
		unsigned int peer = 0U;

		if (argc >= 3 && parse_ipv4(argv[2], &peer) == 0) {
			wlan8189_uplink_peer_set(peer);
			csh_printf(csh, "wlan8189: uplink peer=%u.%u.%u.%u "
			    "configured\n", (peer >> 24) & 0xffU,
			    (peer >> 16) & 0xffU, (peer >> 8) & 0xffU,
			    peer & 0xffU);
		} else {
			csh_printf(csh, "usage: wlan8189 uplink <a.b.c.d>\n");
		}
		return 0;
	}
	if (strcmp(argv[1], "ap") == 0) {
		if (argc >= 4 && strcmp(argv[2], "start") == 0) {
			unsigned int ch = 1U;
			int rc;

			if (argc >= 5)
				ch = (unsigned int)atoi(argv[4]);
			rc = wlan8189_ap_start(argv[3], ch);
			csh_printf(csh, "wlan8189: ap start ssid=%s ch=%u "
			    "rc=%d\n", argv[3], ch, rc);
		} else if (argc >= 3 && strcmp(argv[2], "stop") == 0) {
			int rc = wlan8189_ap_stop();

			csh_printf(csh, "wlan8189: ap stop rc=%d\n", rc);
		} else {
			csh_printf(csh, "usage: wlan8189 ap start <ssid> [ch] | "
			    "ap stop\n");
		}
		return 0;
	}
	csh_printf(csh, "usage: wlan8189 [status|probe|scan|census [reset]|"
		   "ps [show|lps-mode]|ps ips <mode>|rate|"
		   "pump [show|fixed|adaptive|yield|noyield]|"
		   "agg [show|<size-kb>|timeout <units>]|"
		   "dwc [show|<poll-us>|data <us>]|irq [show|on|off]|"
		   "uplink <a.b.c.d>|"
		   "ap start <ssid> [ch]|ap stop]\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wlan8189, wlan8189,
			  "wlan8189 [status|probe|scan|census|ps|rate|pump|agg|dwc|irq|uplink|ap]",
			  "wlan8189 status - whether the driver attached and how many BSS rows are held;\n"
			  "wlan8189 probe - bring the bus and the card up and run the driver's probe;\n"
			  "wlan8189 scan - ask the driver for a scan and print the table it reports\n"
			  "wlan8189 census [reset] - print (or zero) the throughput census snapshot;\n"
			  "wlan8189 ps [show|<lps-mode>] - read back (or set) the power-save policy;\n"
			  "wlan8189 ps ips <mode> - set the idle power-save policy;\n"
			  "wlan8189 rate - print the connected station's rate/bandwidth/BA state\n"
			  "wlan8189 pump [show|fixed|adaptive|yield|noyield] - SDIO pump cadence and fairness\n"
			  "wlan8189 agg [show|<size-kb>|timeout <units>] - card RX aggregation length/timeout\n"
			  "wlan8189 dwc [show|<poll-us>|data <us>] - host controller command/data poll step\n"
			  "wlan8189 irq [show|on|off] - card interrupt (DAT1) event mode vs the polling pump\n"
			  "wlan8189 uplink <a.b.c.d> - the peer the uplink stage's iperf client sends to\n"
			  "wlan8189 ap start <ssid> [ch] - run the interface as an open access point\n"
			  "wlan8189 ap stop - leave access-point mode\n")
