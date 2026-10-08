/*
 * @file   board_cmds.c
 * @brief  Board-level console commands that are not test cases.
 *
 * The registration home for commands that act on the board itself rather
 * than on a component under test. Today: `reboot`, the warm reset back to
 * the firmware prompt through the board's PSCI conduit - the round runner's
 * cheap path between suites (a cold power cycle is the crash path).
 *
 * @design command-export row: the same CSH_CMD_EXPORT_ALIAS_FULL shape the
 * shell's own built-ins use (components/cherrysh/builtin/clear.c:9-27), so
 * the command table stays one mechanism. The reset primitive is the board
 * layer's (board/rk3568-itx/reset.c); this file only wraps it in the console
 * line and the drain pause.
 */

#include "os/cmsis2/cmsis2.h"

#include "csh.h"

#include "reset.h"

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