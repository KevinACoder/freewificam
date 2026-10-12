/**
 * @file    wlan8189_cmds.c
 * @brief   The console command face of the RTL8189FTV image: wlan8189
 *          [status|probe|scan].
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  console command row - same shape as the SDIO image's command set
 *          (tests/board/threadx-rk3568-sdio/tests/sdio_cmds.c):
 *          CSH_CMD_EXPORT_ALIAS_FULL registers the name, the handler reads the
 *          shell from argv[argc + 1] and prints with csh_printf, and the first
 *          argument lives at argv[1]. The command is the human re-run path for
 *          the same chains the cases exercise.
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
	csh_printf(csh, "usage: wlan8189 [status|probe|scan]\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wlan8189, wlan8189, "wlan8189 [status|probe|scan]",
			  "wlan8189 status - whether the driver attached and how many BSS rows are held;\n"
			  "wlan8189 probe - bring the bus and the card up and run the driver's probe;\n"
			  "wlan8189 scan - ask the driver for a scan and print the table it reports\n")
