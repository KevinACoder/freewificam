/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    pcie_nvme_fs_verify.c
 * @brief   Case pcie-nvme-fs-verify: the volume survives the cold boot.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h).
 *          The body lives in tests/pcie_nvme_fs.c (it shares the chunk buffer
 *          and the read/verify helpers with the burn-in case); this file only
 *          registers it, so the two cases sit in their own translation units
 *          and the image's BOARD_TESTS section lists them in execution order.
 *
 * It runs after pcie-nvme-fs in a normal round, but its real job is the SECOND
 * (and third) cold boot: there the volume is already written, so this case is
 * the one that proves the exFAT volume and the 1 GiB file survived the power
 * loss. It never formats.
 *
 * Anchors (the judge checks each on PASS):
 *   pcie-nvme-fs-verify: mounted type=exFAT sector=4096
 *   pcie-nvme-fs-verify: read bytes=<n> ms=<n> kib=<n>
 *   pcie-nvme-fs-verify: verify ok checksum=<8 hex digits>
 */

#include "board_suite.h"
#include "pcie_nvme_fs.h"

BOARD_TEST( "pcie-nvme-fs-verify", pcie_nvme_fs_verify_chain );
