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
	csh_printf(csh, "usage: wlan8189 [status|probe|scan|census [reset]|"
		   "ps [show|lps-mode]|ps ips <mode>|rate]\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wlan8189, wlan8189,
			  "wlan8189 [status|probe|scan|census|ps|rate]",
			  "wlan8189 status - whether the driver attached and how many BSS rows are held;\n"
			  "wlan8189 probe - bring the bus and the card up and run the driver's probe;\n"
			  "wlan8189 scan - ask the driver for a scan and print the table it reports\n"
			  "wlan8189 census [reset] - print (or zero) the throughput census snapshot;\n"
			  "wlan8189 ps [show|<lps-mode>] - read back (or set) the power-save policy;\n"
			  "wlan8189 ps ips <mode> - set the idle power-save policy;\n"
			  "wlan8189 rate - print the connected station's rate/bandwidth/BA state\n")
