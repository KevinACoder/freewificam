/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   dwc_nvme.h
 * @brief  NVMe driver: internal entry points, internal to drivers/.
 *
 * The rest of the project sees drivers/nvme/nvme.h (ARM_DRIVER_NVMe). This
 * header carries what the block-device binding needs before it can address a
 * controller: bring-up and a count. Same split as dwc_ahci.h.
 *
 * @author zhugengyu
 * @date   16.09.2026
 */

#ifndef FREEWEBCAMERA_DWC_NVME_H
#define FREEWEBCAMERA_DWC_NVME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Controllers the core can manage. This board has one NVMe endpoint, on
 * pcie2x1. */
#define DWC_NVME_CTRL_MAX 2

/* Bring up the PCIe backend if it is not up, scan for class 0108h functions
 * and probe each one: controller enable, admin queue, MSI-X, I/O queue pair,
 * namespace geometry. Idempotent; a controller whose link or BAR never
 * appeared is simply reported and skipped. */
void dwc_nvme_init(void);

/* Controllers that came up. */
uint32_t dwc_nvme_ctrl_count(void);

/* Message interrupts this controller has taken (the delivery evidence a
 * polled completion cannot show). Zero when the controller came up polled. */
uint32_t dwc_nvme_irq_count(uint32_t ctrl);

/* The INTID the completion vector was armed with (0 when polled). */
uint32_t dwc_nvme_irq_intid(uint32_t ctrl);

/* Identify Controller MDTS in bytes (0 = no limit). */
uint32_t dwc_nvme_max_transfer(uint32_t ctrl);

/* Asynchronous I/O (benchmark path).  Up to NVME_IO_QD commands in flight;
 * each submit returns the slot index (the command's cid), and reap returns 1
 * for a completion (with the cid), 0 for the timeout, or a negative error.
 * The synchronous Driver_NVME face must not run while these are in flight. */
int32_t dwc_nvme_submit_async(uint32_t ctrl, uint8_t opcode, uint64_t lba,
			      void *buf, uint32_t bytes, uint16_t *cid);
int32_t dwc_nvme_reap(uint32_t ctrl, uint16_t *cid, uint32_t timeout_ms);
uint32_t dwc_nvme_async_inflight(uint32_t ctrl);


#ifdef __cplusplus
}
#endif

#endif /* FREEWEBCAMERA_DWC_NVME_H */