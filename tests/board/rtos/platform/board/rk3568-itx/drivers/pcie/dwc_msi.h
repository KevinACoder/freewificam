/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   dwc_msi.h
 * @brief  MSI (the non-X form) capability programming: one message vector for a
 *         function, from the board's message-interrupt domain to the endpoint's
 *         capability registers.
 *
 * This is the second half of the board's interrupt face. MSI-X carries its
 * vectors in a table inside a BAR (dwc_msix.h); plain MSI carries one address and
 * one data word in the capability itself, so the programming is a config-space
 * write sequence rather than a table walk. The consumer this exists for is a
 * function whose driver asks for a single message interrupt (pci_enable_msi), not
 * a table of vectors - multi-message MSI is deliberately not built.
 *
 * The allocation still comes from the same domain: the ITS hands out the LPI and
 * the event id, and this code writes them into the capability. The message data
 * must be the ITS event id - a payload carrying the LPI number would translate to
 * nothing.
 *
 * @author zhugengyu
 * @date   12.10.2026
 */

#ifndef FREEWIFICAM_DWC_MSI_H
#define FREEWIFICAM_DWC_MSI_H

#include <stdint.h>

#include "Driver_Common.h"
#include "msi.h"

#include "dwc_pcie.h"

/* Vectors this layer will arm. The design carries one message per function (the
 * capability's MME field encodes a power of two, and the first consumer asks for
 * exactly one), so the ceiling is small on purpose. */
#define DWC_MSI_VECTOR_MAX	4

/* Allocate up to nvec_max vectors for `dev` from the message-interrupt domain and
 * program the endpoint's MSI capability with them. Returns the number of vectors
 * armed (>= 1) or a negative ARM_DRIVER_ERROR*. On success the caller owns the
 * vectors and must arm each intid with IRQ_SetHandler + IRQ_Enable. */
int32_t dwc_msi_alloc(const struct dwc_pcie_dev *dev, uint32_t nvec_max,
		      MSI_VECTOR *vectors);

/* Disable the function's MSI and withdraw its mappings. */
int32_t dwc_msi_release(const struct dwc_pcie_dev *dev);

#endif /* FREEWIFICAM_DWC_MSI_H */
