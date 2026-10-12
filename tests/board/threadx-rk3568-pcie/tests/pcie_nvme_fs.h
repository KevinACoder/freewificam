/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    pcie_nvme_fs.h
 * @brief   The storage cases' shared body: the mount-without-format case body.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case body split across translation units - the image's BOARD_TESTS
 *          section lists cases in link order, and a single unit's entries are
 *          not emitted in source order, so the second case lives in its own
 *          file (tests/pcie_nvme_fs_verify.c) and registers the body this
 *          header declares. The two cases share one implementation file
 *          (tests/pcie_nvme_fs.c) and therefore one chunk buffer and one set of
 *          helpers.
 *
 * The body prints its own anchors and calls board_case_fail() on a failure,
 * like every other case body.
 */

#ifndef PCIE_NVME_FS_H
#define PCIE_NVME_FS_H

/** Case body: mount the volume the previous cold boot formatted, WITHOUT
 *  formatting it, read the burn-in file back and checksum it. */
void pcie_nvme_fs_verify_chain( void );

#endif /* PCIE_NVME_FS_H */
