/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   shell_cmds.c
 * @brief  The board command table: cases, groups, log, reboot and board info.
 *
 * Every command prints a machine-matchable `cmd: ...` result line.  The case
 * commands call the same table the static runner walks, so the anchor language
 * is unchanged: `rtos: case <name> start` / `PASS` / `FAIL rc=`.
 */

#include "csh.h"

#include <string.h>

#include "cases.h"
#include "clock.h"
#include "log.h"
#include "reset.h"
#include "runner.h"
#include "shell_cmds.h"

#include "os/cmsis2/cmsis2.h"

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif
#ifndef RTOS_BACKEND_NAME
#define RTOS_BACKEND_NAME "unknown"
#endif
#ifndef CONFIG_SHELL_AUTORUN
#define CONFIG_SHELL_AUTORUN ""
#endif

static chry_shell_t *cmd_csh(int argc, char **argv)
{
	return (chry_shell_t *)(void *)argv[argc + 1];
}

static int is_nonreturning(const char *name)
{
	return strcmp(name, "net_mdk_netio") == 0 ||
	       strcmp(name, "net_pps") == 0 ||
	       strcmp(name, "net_loop") == 0 ||
	       strcmp(name, "rtl8xxxu_netio") == 0;
}

static int run_one(chry_shell_t *csh, int index, int report)
{
	const char *name = rtos_case_name((unsigned)index);
	int rc;

	if (name == NULL) {
		return -1;
	}

	csh_printf(csh, "rtos: case %s start\n", name);
	rc = rtos_case_run((unsigned)index);
	if (rc == 0) {
		csh_printf(csh, "rtos: case %s PASS\n", name);
	} else {
		csh_printf(csh, "rtos: case %s FAIL rc=%d\n", name, rc);
	}
	if (report != 0) {
		csh_printf(csh, "cmd: case run name=%s rc=%d\n", name, rc);
	}
	return rc;
}

static int cmd_case(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	unsigned i;

	if (argc < 2) {
		csh_printf(csh, "usage: case list|run <name>|run-all\n");
		return 0;
	}

	if (strcmp(argv[1], "list") == 0) {
		csh_printf(csh, "cmd: case list count=%u\n", rtos_case_count());
		for (i = 0u; i < rtos_case_count(); i++) {
			csh_printf(csh, "cmd: case name=%s\n", rtos_case_name(i));
		}
		return 0;
	}

	if (strcmp(argv[1], "run") == 0) {
		int index;

		if (argc < 3) {
			csh_printf(csh, "usage: case run <name>\n");
			return 0;
		}
		index = rtos_case_find(argv[2]);
		if (index < 0) {
			csh_printf(csh, "cmd: case run name=%s rc=-1\n", argv[2]);
			return -1;
		}
		if (is_nonreturning(argv[2])) {
			csh_printf(csh, "cmd: case run name=%s non-returning; "
				   "use the dedicated start/stop command\n",
				   argv[2]);
			return -1;
		}
		return run_one(csh, index, 1);
	}

	if (strcmp(argv[1], "run-all") == 0) {
		unsigned total = rtos_case_count();
		unsigned ok = 0u;

		for (i = 0u; i < total; i++) {
			const char *name = rtos_case_name(i);

			if (is_nonreturning(name)) {
				csh_printf(csh, "cmd: case skip name=%s "
					   "non-returning\n", name);
				continue;
			}
			if (run_one(csh, (int)i, 0) == 0) {
				ok++;
			}
		}
		csh_printf(csh, "cmd: case run-all ok=%u total=%u\n", ok, total);
		return (ok == total) ? 0 : -1;
	}

	csh_printf(csh, "usage: case list|run <name>|run-all\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_case, case,
	"case list|run <name>|run-all",
	"run one compiled-in regression case with the legacy anchors\r\n");

static int group_run(chry_shell_t *csh, const char *group, const char *sub,
		     const char *const *names, unsigned count)
{
	unsigned i;

	for (i = 0u; i < count; i++) {
		int index = rtos_case_find(names[i]);

		if (index < 0) {
			continue;
		}
		if (is_nonreturning(names[i])) {
			csh_printf(csh, "cmd: %s %s non-returning; use the "
				   "dedicated start/stop command\n", group, sub);
			return -1;
		}
		return run_one(csh, index, 1);
	}

	csh_printf(csh, "cmd: %s %s unavailable\n", group, sub);
	return -1;
}

static int cmd_wifi(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	static const char *const scan[] = {
		"rtl8xxxu_scan", "rtw89_scan", "rtw88_scan", "iwl7260_scan",
		"wlan",
	};
	static const char *const join[] = {
		"rtl8xxxu_join", "rtw89_join", "iwl7260_join", "wlan",
	};
	static const char *const link[] = {
		"rtl8xxxu", "rtw89", "rtw88", "iwl7260", "wlan",
	};
	static const char *const ba[] = {
		"rtl8xxxu_dataplane", "rtw89_dataplane", "iwl7260_dataplane",
	};
	static const char *const netio[] = { "rtl8xxxu_netio", "wlan" };

	if (argc < 2) {
		csh_printf(csh, "usage: wifi scan|join|link|ba|netio\n");
		return 0;
	}
	if (strcmp(argv[1], "scan") == 0) {
		return group_run(csh, "wifi", "scan", scan,
				 sizeof(scan) / sizeof(scan[0]));
	}
	if (strcmp(argv[1], "join") == 0) {
		return group_run(csh, "wifi", "join", join,
				 sizeof(join) / sizeof(join[0]));
	}
	if (strcmp(argv[1], "link") == 0) {
		return group_run(csh, "wifi", "link", link,
				 sizeof(link) / sizeof(link[0]));
	}
	if (strcmp(argv[1], "ba") == 0) {
		return group_run(csh, "wifi", "ba", ba,
				 sizeof(ba) / sizeof(ba[0]));
	}
	if (strcmp(argv[1], "netio") == 0) {
		return group_run(csh, "wifi", "netio", netio,
				 sizeof(netio) / sizeof(netio[0]));
	}
	csh_printf(csh, "usage: wifi scan|join|link|ba|netio\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wifi, wifi,
	"wifi scan|join|link|ba|netio",
	"run the wireless case that this image carries\r\n");

static int cmd_net(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	static const char *const data[] = { "net", "net_xmac" };
	static const char *const netio[] = { "net_mdk_netio" };

	if (argc < 2) {
		csh_printf(csh, "usage: net ping|udp|tcp|netio\n");
		return 0;
	}
	if (strcmp(argv[1], "ping") == 0 || strcmp(argv[1], "udp") == 0 ||
	    strcmp(argv[1], "tcp") == 0) {
		return group_run(csh, "net", argv[1], data,
				 sizeof(data) / sizeof(data[0]));
	}
	if (strcmp(argv[1], "netio") == 0) {
		return group_run(csh, "net", "netio", netio,
				 sizeof(netio) / sizeof(netio[0]));
	}
	csh_printf(csh, "usage: net ping|udp|tcp|netio\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_net, net,
	"net ping|udp|tcp|netio",
	"run the wired network case this image carries\r\n");

/*
 * The two on-demand controller diagnostics.  Weak, because this file is in every
 * image and not every image carries the linuxkpi USB engine.
 */
extern void lkpi_usb_controller_report(void) __attribute__((weak));
extern int lkpi_usb_controller_rearm(void) __attribute__((weak));

static int cmd_usb(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	static const char *const usb[] = { "usb_xhci", "usb_ehci" };

	if (argc < 2) {
		csh_printf(csh, "usage: usb enum|msc|census|rearm\n");
		return 0;
	}
	if (strcmp(argv[1], "census") == 0) {
		if (lkpi_usb_controller_report == NULL) {
			csh_printf(csh, "usb census: no linuxkpi USB in this image\n");
			return 0;
		}
		lkpi_usb_controller_report();
		return 0;
	}
	if (strcmp(argv[1], "rearm") == 0) {
		int n;

		if (lkpi_usb_controller_rearm == NULL) {
			csh_printf(csh, "usb rearm: no linuxkpi USB in this image\n");
			return 0;
		}
		n = lkpi_usb_controller_rearm();
		csh_printf(csh, "usb rearm: %d controller(s), completions resume?\n", n);
		return 0;
	}
	if (strcmp(argv[1], "enum") == 0 || strcmp(argv[1], "msc") == 0) {
		return group_run(csh, "usb", argv[1], usb,
				 sizeof(usb) / sizeof(usb[0]));
	}
	csh_printf(csh, "usage: usb enum|msc|census|rearm\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_usb, usb,
	"usb enum|msc|census|rearm",
	"run the USB host case this image carries\r\n");

static int cmd_sdio(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	static const char *const sdio[] = { "sdio_mci" };

	if (argc < 2 ||
	    (strcmp(argv[1], "probe") != 0 && strcmp(argv[1], "cells") != 0)) {
		csh_printf(csh, "usage: sdio probe|cells\n");
		return 0;
	}
	return group_run(csh, "sdio", argv[1], sdio,
			 sizeof(sdio) / sizeof(sdio[0]));
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_sdio, sdio,
	"sdio probe|cells",
	"run the SDIO socket case this image carries\r\n");

static int cmd_pci(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	static const char *const enum_cases[] = { "pcie", "pcie_ecam" };
	static const char *const bench[] = { "pcie_bench" };
	static const char *const ecam[] = { "pcie_ecam" };

	if (argc < 2) {
		csh_printf(csh, "usage: pci enum|bench|ecam\n");
		return 0;
	}
	if (strcmp(argv[1], "enum") == 0) {
		return group_run(csh, "pci", "enum", enum_cases,
				 sizeof(enum_cases) / sizeof(enum_cases[0]));
	}
	if (strcmp(argv[1], "bench") == 0) {
		return group_run(csh, "pci", "bench", bench,
				 sizeof(bench) / sizeof(bench[0]));
	}
	if (strcmp(argv[1], "ecam") == 0) {
		return group_run(csh, "pci", "ecam", ecam,
				 sizeof(ecam) / sizeof(ecam[0]));
	}
	csh_printf(csh, "usage: pci enum|bench|ecam\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_pci, pci,
	"pci enum|bench|ecam",
	"run the PCIe case this image carries\r\n");

static int cmd_emmc(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	static const char *const emmc[] = { "emmc" };

	if (argc < 2 ||
	    (strcmp(argv[1], "info") != 0 && strcmp(argv[1], "rw") != 0 &&
	     strcmp(argv[1], "sweep") != 0)) {
		csh_printf(csh, "usage: emmc info|rw|sweep\n");
		return 0;
	}
	return group_run(csh, "emmc", argv[1], emmc,
			 sizeof(emmc) / sizeof(emmc[0]));
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_emmc, emmc,
	"emmc info|rw|sweep",
	"run the eMMC case this image carries\r\n");

static int cmd_uvc(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	static const char *const detect[] = { "uvc_detect" };
	static const char *const stream[] = { "uvc_stream" };

	if (argc < 2) {
		csh_printf(csh, "usage: uvc detect|stream\n");
		return 0;
	}
	if (strcmp(argv[1], "detect") == 0) {
		return group_run(csh, "uvc", "detect", detect,
				 sizeof(detect) / sizeof(detect[0]));
	}
	if (strcmp(argv[1], "stream") == 0) {
		return group_run(csh, "uvc", "stream", stream,
				 sizeof(stream) / sizeof(stream[0]));
	}
	csh_printf(csh, "usage: uvc detect|stream\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_uvc, uvc,
	"uvc detect|stream",
	"run the UVC case this image carries\r\n");

static const char *log_level_name(ulog_level_t level)
{
	switch (level) {
	case ULOG_TRACE_LEVEL:		return "trace";
	case ULOG_DEBUG_LEVEL:		return "debug";
	case ULOG_INFO_LEVEL:		return "info";
	case ULOG_WARNING_LEVEL:	return "warning";
	case ULOG_ERROR_LEVEL:		return "error";
	case ULOG_CRITICAL_LEVEL:	return "critical";
	default:			return "?";
	}
}

static int log_level_parse(const char *name, ulog_level_t *out)
{
	static const struct {
		const char *name;
		ulog_level_t level;
	} tab[] = {
		{ "trace", ULOG_TRACE_LEVEL },
		{ "debug", ULOG_DEBUG_LEVEL },
		{ "info", ULOG_INFO_LEVEL },
		{ "warning", ULOG_WARNING_LEVEL },
		{ "error", ULOG_ERROR_LEVEL },
		{ "critical", ULOG_CRITICAL_LEVEL },
	};
	unsigned i;

	for (i = 0u; i < sizeof(tab) / sizeof(tab[0]); i++) {
		if (strcmp(name, tab[i].name) == 0) {
			*out = tab[i].level;
			return 0;
		}
	}
	return -1;
}

static int cmd_log(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	ulog_level_t level;

	if (argc >= 3 && strcmp(argv[1], "level") == 0) {
		if (log_level_parse(argv[2], &level) != 0) {
			csh_printf(csh, "cmd: log level name=%s rc=-1\n", argv[2]);
			return -1;
		}
		log_level_set(level);
		csh_printf(csh, "cmd: log level=%s rc=0\n", log_level_name(level));
		return 0;
	}
	if (argc >= 2 && strcmp(argv[1], "mark") == 0) {
		csh_printf(csh, "cmd: log mark build=%s tick=%u\n",
			   FW_BUILD_TAG, (unsigned)osKernelGetTickCount());
		return 0;
	}
	if (argc >= 2 && strcmp(argv[1], "level") == 0) {
		csh_printf(csh, "cmd: log level=%s\n",
			   log_level_name(log_level_get()));
		return 0;
	}
	csh_printf(csh, "usage: log level <trace|debug|info|warning|error|"
		   "critical>|mark\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_log, log,
	"log level <name>|mark",
	"set the console threshold or emit a build/tick mark\r\n");

static int cmd_board(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);

	if (argc < 2) {
		csh_printf(csh, "usage: board info|clock|heap\n");
		return 0;
	}
	if (strcmp(argv[1], "info") == 0) {
		csh_printf(csh, "cmd: board info build=%s backend=%s cases=%u\n",
			   FW_BUILD_TAG, RTOS_BACKEND_NAME, rtos_case_count());
		return 0;
	}
	if (strcmp(argv[1], "clock") == 0) {
		csh_printf(csh, "cmd: board clock ticks=%u freq=%u\n",
			   (unsigned)osKernelGetTickCount(),
			   (unsigned)osKernelGetTickFreq());
		return 0;
	}
	if (strcmp(argv[1], "heap") == 0) {
		csh_printf(csh, "cmd: board heap unavailable=no-portable-api\n");
		return -1;
	}
	csh_printf(csh, "usage: board info|clock|heap\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_board, board,
	"board info|clock|heap",
	"build/backend identity, OS tick and heap availability\r\n");

static int cmd_reboot(int argc, char **argv)
{
	chry_shell_t *csh = cmd_csh(argc, argv);
	int rc;

	(void)argc;
	(void)argv;

	csh_printf(csh, "cmd: reboot PSCI_SYSTEM_RESET\n");
	(void)osDelay(20u);
	rc = board_system_reset();
	csh_printf(csh, "cmd: reboot refused rc=%d\n", rc);
	return rc;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_reboot, reboot,
	"reboot",
	"warm reset to U-Boot through PSCI SYSTEM_RESET\r\n");

static int autorun_one(const char *name)
{
	int index = rtos_case_find(name);
	int rc;

	if (index < 0) {
		CASE_ANCHOR("rtos: case %s FAIL rc=-1\n", name);
		return -1;
	}
	CASE_ANCHOR("rtos: case %s start\n", name);
	rc = rtos_case_run((unsigned)index);
	if (rc == 0) {
		CASE_ANCHOR("rtos: case %s PASS\n", name);
	} else {
		CASE_ANCHOR("rtos: case %s FAIL rc=%d\n", name, rc);
	}
	return rc;
}

void shell_autorun(void)
{
	const char *list = CONFIG_SHELL_AUTORUN;
	char work[192];
	unsigned len = 0u;
	unsigned total = 0u;
	unsigned ok = 0u;
	unsigned i;
	char *p;

	while (list[len] != '\0' && len < sizeof(work) - 1u) {
		work[len] = list[len];
		len++;
	}
	work[len] = '\0';
	if (len == 0u) {
		return;
	}

	for (p = work; *p != '\0';) {
		while (*p == ' ') {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		total++;
		while (*p != '\0' && *p != ' ') {
			p++;
		}
	}

	CASE_ANCHOR("rtos: runner start cases=%u build=%s\n", total,
		    FW_BUILD_TAG);

	for (p = work, i = 0u; *p != '\0' && i < total; i++) {
		char *start;

		while (*p == ' ') {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		start = p;
		while (*p != '\0' && *p != ' ') {
			p++;
		}
		if (*p != '\0') {
			*p++ = '\0';
		}
		if (autorun_one(start) == 0) {
			ok++;
		}
	}

	CASE_ANCHOR("rtos: result %s cases=%u/%u backend=%s\n",
		    (ok == total) ? "pass" : "fail", ok, total,
		    RTOS_BACKEND_NAME);

#if defined(RTOS_BOARD_AUTO_RESET)
	CASE_ANCHOR("rtos: resetting board\n");
	(void)osDelay(200u);
	board_system_reset();
#endif
}
