/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    pcie_bringup.c
 * @brief   The PCIe/NVMe bring-up shared by the suite's cases.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  shared case body - the same split the SDIO image uses
 *          (tests/board/threadx-rk3568-sdio/tests/sdio_cases.h declares the
 *          bodies the case and command files re-run). The bus bring-up is one
 *          step both the bus case and the storage cases need, so it lives once
 *          here and each case calls it.
 *
 * One bring-up per boot: the drivers are idempotent, but the storage cases run
 * after the bus case and must not re-run the probe. The probe re-reads the
 * controller identity and the namespace geometry, and on a board whose link is
 * already up that work is pure cost - so the first caller does it and every
 * later caller reads the count the first one reached. The `pcie bringup`
 * console command re-runs the case bodies, which call this and therefore stay
 * cheap once the line is up.
 */

#include "pcie_cases.h"

#include "dwc_pcie.h"
#include "dwc_nvme.h"

/* Set once the drivers' bring-up has run. The runner thread owns it (cases run
 * one at a time); the console command re-runs on the shell thread, and the
 * worst a race there can do is run an idempotent probe twice. */
static int pcie_bringup_done;

int pcie_nvme_bringup( void )
{
	if( !pcie_bringup_done ) {
		dwc_pcie_init();
		dwc_nvme_init();
		pcie_bringup_done = 1;
	}

	return dwc_nvme_ctrl_count() > 0U;
}
