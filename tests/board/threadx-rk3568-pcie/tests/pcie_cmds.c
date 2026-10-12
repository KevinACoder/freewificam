/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    pcie_cmds.c
 * @brief   The console command face of the PCIe/NVMe image: pcie [status|bringup].
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  console command row - same shape as the SDIO image's command set
 *          (tests/board/threadx-rk3568-sdio/tests/sdio_cmds.c):
 *          CSH_CMD_EXPORT_ALIAS_FULL registers the name, the handler reads the
 *          shell from argv[argc + 1] and prints with csh_printf, and the first
 *          argument lives at argv[1].  The command is the human re-run path for
 *          the same chain the case exercises.
 *
 * The status read goes through the board drivers' own accessors, which are safe
 * before bring-up (every counter reads zero): an absent controller is a zero,
 * not a null pointer this file has to know about.
 */

#include <string.h>

#include "csh.h"

#include "dwc_pcie.h"
#include "dwc_nvme.h"

#include "pcie_cases.h"

static int
cmd_pcie(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];

	if (argc < 2 || strcmp(argv[1], "status") == 0) {
		/*
		 * The line has one shape whether or not the bring-up has run. The
		 * console steps are sent as soon as the suite's start line appears,
		 * which can be before the first case has brought the line up, so a
		 * status line whose shape depended on that would make the interaction
		 * step a race rather than a check.
		 */
		csh_printf(csh, "pcie: ctrls=%u devs=%u nvme=%u irqs=%u\n",
			   dwc_pcie_ctrl_count(), dwc_pcie_dev_count(),
			   dwc_nvme_ctrl_count(), dwc_nvme_irq_count(0U));
		return 0;
	}
	if (strcmp(argv[1], "bringup") == 0) {
		pcie_nvme_chain();
		return 0;
	}
	csh_printf(csh, "usage: pcie [status|bringup]\n");
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_pcie, pcie, "pcie [status|bringup]",
			  "pcie status - the controller, function, NVMe and message-interrupt counts;\n"
			  "pcie bringup - bring the PCIe root complex and the NVMe controller up and\n"
			  "               force one I/O completion through the message-interrupt path\n")
