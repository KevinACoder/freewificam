/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   nvme_internal.h
 * @brief  NVMe driver: internal entry points, internal to drivers/.
 *
 * The rest of the project sees drivers/nvme/nvme.h (ARM_DRIVER_NVMe). This
 * header carries what the block-device binding needs before it can address a
 * controller: bring-up and a count. Same split the RK3568 carrier's driver used.
 *
 * @author zhugengyu
 * @date   16.09.2026
 */

#ifndef FREEWIFICAM_NVME_INTERNAL_H
#define FREEWIFICAM_NVME_INTERNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Controllers the core can manage. This board has one NVMe endpoint, on
 * pcie2x1. */
#define NVME_CTRL_MAX 2

/* Bring up the PCIe backend if it is not up, scan for class 0108h functions
 * and probe each one: controller enable, admin queue, MSI-X, I/O queue pair,
 * namespace geometry. Idempotent; a controller whose link or BAR never
 * appeared is simply reported and skipped. */
void nvme_init(void);

/* Controllers that came up. */
uint32_t nvme_ctrl_count(void);

/* Message interrupts this controller has taken (the delivery evidence a
 * polled completion cannot show). Zero when the controller came up polled. */
uint32_t nvme_irq_count(uint32_t ctrl);


#ifdef __cplusplus
}
#endif

#endif /* FREEWIFICAM_NVME_INTERNAL_H */