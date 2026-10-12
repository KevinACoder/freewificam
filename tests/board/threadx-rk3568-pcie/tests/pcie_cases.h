/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    pcie_cases.h
 * @brief   The PCIe/NVMe chain body, shared by the case table and the console
 *          command.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case body plus a re-run path - the same split the SDIO image uses
 *          (tests/board/threadx-rk3568-sdio/tests/sdio_cases.h).
 *
 * The body prints its own anchors and calls board_case_fail() on a failure; the
 * case file registers it into the image's table and the command file runs it
 * again on request.
 */

#ifndef PCIE_CASES_H
#define PCIE_CASES_H

/** Bring the PCIe root complex up, probe the NVMe controller, read its
 *  namespace geometry and force one real I/O completion through the interrupt
 *  path. */
void pcie_nvme_chain( void );

#endif /* PCIE_CASES_H */
