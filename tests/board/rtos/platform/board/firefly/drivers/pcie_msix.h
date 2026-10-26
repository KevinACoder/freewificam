/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   pcie_msix.h
 * @brief  MSI-X table programming for an ECAM PCIe endpoint.
 *
 * Internal to drivers/: an endpoint driver (nvme.c today, an 802.11 one later) calls these two functions and gets back armed vectors; nothing above
 * the driver layer sees MSI-X at all.
 *
 * What remains here is the part that touches hardware, and the ordering is not
 * optional:
 *
 *   - mask the entry, order it, write the address/data triple, order it,
 *     unmask - a posted message write observed while the vector is unmasked
 *     can arm a half-written entry (PCIe spec 7.7.2.1);
 *   - hold the function masked (MASKALL) while the entries are programmed,
 *     unmask every entry, and only then raise ENABLE with MASKALL cleared in
 *     a single config write: endpoints sample the table as ENABLE rises and
 *     one that captures a masked state never re-samples;
 *   - the payload written into an entry is the ITS event id, which is also
 *     the entry index - the mapping the ITS glue is written against.
 *
 * @author zhugengyu
 * @date   16.09.2026
 */

#ifndef FREEWIFICAM_PCIE_MSIX_H
#define FREEWIFICAM_PCIE_MSIX_H

#include <stdint.h>

#include "msi.h"

#include "pcie_ecam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How many vectors one function may be given. The ITS hands out 48 LPIs in
 * total and the doorbell's event id must stay inside the device's ITT (64
 * entries), so a handful is the honest cap for a storage endpoint. */
#define PCIE_MSIX_VECTOR_MAX 4

/* Allocate message vectors for a function, program its MSI-X table with them
 * and enable MSI-X. Returns the number of vectors armed (>= 1), or a negative
 * ARM_DRIVER_ERROR*. On success the caller owns the vectors and must arm each
 * `intid` with IRQ_SetHandler + IRQ_Enable. */
int32_t pcie_msix_alloc(const struct pcie_ecam_func *dev, uint32_t nvec_max,
		       MSI_VECTOR *vectors);

/* Disable MSI-X on the function and withdraw its translations. */
int32_t pcie_msix_release(const struct pcie_ecam_func *dev);

#ifdef __cplusplus
}
#endif

#endif /* FREEWIFICAM_PCIE_MSIX_H */