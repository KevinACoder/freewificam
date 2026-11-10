/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   pcie_cases.h
 * @brief  Shared bring-up for the PCIe/NVMe cases.
 *
 * The bus bring-up is one step both the bus case (pcie), the benchmark
 * (pcie_bench) and the endpoint case (pcie_7260) need; it lives in pcie.c and
 * every later case calls it.  The drivers are idempotent, so the first caller
 * probes and the rest read the counts the first one reached.
 */

#ifndef RTOS_APP_CASES_PCIE_CASES_H
#define RTOS_APP_CASES_PCIE_CASES_H

#include <stdint.h>

/* dwc_pcie_init() + dwc_nvme_init(), once per boot.  Returns the number of
 * NVMe controllers found (0 when the endpoint is absent). */
uint32_t pcie_board_bringup(void);

/* Largest namespace geometry seen by the last bring-up, in 512-byte units.
 * The benchmark uses it to place its scratch window; 0 means "ask again". */
uint64_t pcie_ns_blocks(void);
uint32_t pcie_ns_sector_size(void);

#endif /* RTOS_APP_CASES_PCIE_CASES_H */