/**
 * @file    sdio_cmds.c
 * @brief   The console command face of the SDIO image: sdio [status|enum|datapath|pump].
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  console command row - same shape as the driver images' command sets
 *          (tests/board/threadx-rk3568-rtw88/tests/rtw88_cmds.c): CSH_CMD_EXPORT_ALIAS_FULL
 *          registers the name, the handler reads the shell from argv[argc + 1]
 *          and prints with csh_printf, and the first argument lives at argv[1].
 *          The command is the human re-run path for the same chains the cases
 *          exercise.
 *
 * The status read goes through the seam only, so the command file carries no
 * dependency on the imported stack: an absent card is an error from the seam,
 * not a null pointer this file has to know about.
 */

#include <string.h>

#include "csh.h"

#include "lkpi_sdio_backend.h"

#include "sdio_cases.h"

/* the CCCR registers the status line reads */
#define SDIO_CCCR_REVISION	0x00U
#define SDIO_CCCR_IO_ENABLE	0x02U

static int
cmd_sdio(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];

	if (argc < 2 || strcmp(argv[1], "status") == 0) {
		uint8_t cccr = 0U;
		uint8_t enable = 0U;
		int st;

		/*
		 * The line has one shape whether or not the card is up. The console
		 * steps are sent as soon as the suite's start line appears, which can be
		 * before the first case has installed the backend and brought the card
		 * up, so a status line whose shape depended on that would make the
		 * interaction step a race rather than a check.
		 */
		st = lkpi_sdio_be_io_direct(0U, SDIO_CCCR_REVISION, false, &cccr);
		if (st == 0) {
			(void)lkpi_sdio_be_io_direct(0U, SDIO_CCCR_IO_ENABLE, false, &enable);
		}
		csh_printf(csh, "sdio: ready=%d card=%d cccr=%#04x ioex=%#04x\n",
			   (int)lkpi_sdio_be_ready(), (st == 0) ? 1 : 0, cccr, enable);
		return 0;
	}
	if (strcmp(argv[1], "enum") == 0) {
		sdio_enum_chain();
		return 0;
	}
	if (strcmp(argv[1], "datapath") == 0) {
		sdio_datapath_chain();
		return 0;
	}
	if (strcmp(argv[1], "pump") == 0) {
		sdio_pump_chain();
		return 0;
	}
	csh_printf(csh, "usage: sdio [status|enum|datapath|pump]\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_sdio, sdio, "sdio [status|enum|datapath|pump]",
			  "sdio status - the card's revision and function-enable registers;\n"
			  "sdio enum - bring the card up and print its identity;\n"
			  "sdio datapath - the CIS block read and the two-byte register write;\n"
			  "sdio pump - poll the card's pending-interrupt word\n")
