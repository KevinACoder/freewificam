/*
 * @file   board_cmds.c
 * @brief  Board-level console commands that are not test cases.
 *
 * The registration home for commands that act on the board itself rather
 * than on a component under test. Today: `log`, the console level (set the
 * engine threshold, report it with the wpa/usb build keys), and `reboot`,
 * the warm reset back to the firmware prompt through the board's PSCI
 * conduit - the round runner's cheap path between suites (a cold power
 * cycle is the crash path).
 *
 * @design command-export row: the same CSH_CMD_EXPORT_ALIAS_FULL shape the
 * shell's own built-ins use (components/cherrysh/builtin/clear.c:9-27), so
 * the command table stays one mechanism. The reset primitive is the board
 * layer's (board/rk3568-itx/reset.c); this file only wraps it in the console
 * line and the drain pause.
 */

#include "os/cmsis2/cmsis2.h"

#include <stdbool.h>
#include <string.h>

#include "csh.h"

#include "reset.h"

#include "os/baremetal/log.h"

/*
 * The two build keys the console level does not own: the supplicant and the USB
 * stack keep their own compile-time levels. The image passes them in (the port
 * config headers carry hard build-key guards, so this file cannot read them
 * directly); an image that does not carry a port says so.
 */
#ifndef LOG_CMD_WPA_KEY
#define LOG_CMD_WPA_KEY		(-1)
#endif
#ifndef LOG_CMD_USB_KEY
#define LOG_CMD_USB_KEY		(-1)
#endif

/* The level names the command accepts, in the engine's vocabulary. */
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

static bool log_level_parse(const char *name, ulog_level_t *out)
{
	static const struct { const char *name; ulog_level_t level; } tab[] = {
		{ "trace", ULOG_TRACE_LEVEL },
		{ "debug", ULOG_DEBUG_LEVEL },
		{ "info", ULOG_INFO_LEVEL },
		{ "warning", ULOG_WARNING_LEVEL },
		{ "error", ULOG_ERROR_LEVEL },
		{ "critical", ULOG_CRITICAL_LEVEL },
	};
	unsigned int i;

	for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
		if (strcmp(name, tab[i].name) == 0) {
			*out = tab[i].level;
			return true;
		}
	}
	return false;
}

/* Change the console threshold, or report where it stands. The judged lines are
 * not affected either way: they ride the never-filtered families, so the command
 * cannot silence the evidence a suite is read on. */
static int cmd_log(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];
	ulog_level_t level;

	if (argc >= 3 && strcmp(argv[1], "level") == 0) {
		if (!log_level_parse(argv[2], &level)) {
			csh_printf(csh, "log: unknown level '%s'\n", argv[2]);
			return -1;
		}
		log_level_set(level);
		csh_printf(csh, "log: level=%s\n", log_level_name(level));
		return 0;
	}

	if (argc >= 2 && strcmp(argv[1], "status") == 0) {
		csh_printf(csh, "log: level=%s\n",
		    log_level_name(log_level_get()));
		csh_printf(csh, "log: wpa=%d usb=%d (build keys; -1 = port "
		    "not in this image)\n", (int)LOG_CMD_WPA_KEY,
		    (int)LOG_CMD_USB_KEY);
		return 0;
	}

	csh_printf(csh, "usage: log level <trace|debug|info|warning|error|"
	    "critical>|status\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_log, log, "log level|status",
			  "console log level: set the threshold, or report it and the wpa/usb build keys\n")

/* Warm reset back to the firmware prompt. The 20 ms pause is for the
 * console, not the SoC: the line above sits in the UART's FIFO, and at
 * 115200 baud a short line needs a couple of milliseconds on the wire -
 * without the wait it would die with the SoC. Then the SMC: on this board
 * the system resets and this task never runs again. */
static int cmd_reboot(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];
	int ret;

	csh_printf(csh, "reboot: PSCI SYSTEM_RESET\n");
	osDelay(20U);

	ret = board_system_reset();
	/* Only reachable when the firmware refused the call (PSCI
	 * NOT_SUPPORTED or an invalid function id): the board still runs. */
	csh_printf(csh, "reboot: firmware refused the reset (%d)\n", ret);
	return ret;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_reboot, reboot, "reboot",
			  "warm reset to U-Boot through PSCI SYSTEM_RESET\n")