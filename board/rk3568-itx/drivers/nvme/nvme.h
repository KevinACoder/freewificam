/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   nvme.h
 * @brief  NVMe controller interface: the ARM_DRIVER_NVMe ops shape.
 *
 * CMSIS-Driver covers generic block storage (Driver_Storage.h) but has no NVMe
 * class. This keeps the CMSIS ops-struct idiom - a version record, a capability
 * query, an initialise/uninitialise pair, the transfer calls and an event
 * callback - so a driver implements one struct and a future standard header can
 * replace this one without touching callers.
 *
 * The board's dwc_nvme driver implements this shape; the controller count and
 * bring-up entry points it needs before a caller can address controller N live
 * in that driver's own header (dwc_nvme.h), next to the implementation.
 *
 * CONTRACT:
 *  - Calls are task-context only.
 *  - LBA-addressed; GetCapabilities() is authoritative for the logical block
 *    size, so a caller never assumes 512.
 *  - Completion is asynchronous. The callback runs in interrupt context and
 *    must not block; a buffer may not be reused until its completion has been
 *    signalled.
 *  - The caller owns every buffer; the driver owns the queue pair.
 */

#ifndef FREEWIFICAM_NVME_H
#define FREEWIFICAM_NVME_H

#include <stdbool.h>
#include <stdint.h>

#include "Driver_Common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVME_API_VERSION ARM_DRIVER_VERSION_MAJOR_MINOR(0, 1)

#define NVME_EVENT_READ_DONE	(1UL << 0)
#define NVME_EVENT_WRITE_DONE	(1UL << 1)
#define NVME_EVENT_ERROR	(1UL << 2)

typedef void (*NVME_SignalEvent_t)(uint32_t event, uint32_t ctrl);

typedef struct _NVME_CAPABILITIES {
	uint64_t sector_count;	/* total logical blocks */
	uint32_t sector_size;	/* bytes per logical block, normally 512 */
	uint32_t max_transfer;	/* largest single transfer, in bytes */
	uint32_t queue_depth;	/* submission queue entries */
	uint32_t timeout_ms;	/* controller recommended timeout */
} NVME_CAPABILITIES;

typedef struct _ARM_DRIVER_NVME {
	ARM_DRIVER_VERSION (*GetVersion)(void);
	NVME_CAPABILITIES (*GetCapabilities)(uint32_t ctrl);
	int32_t (*Initialize)(uint32_t ctrl, NVME_SignalEvent_t cb_event);
	int32_t (*Uninitialize)(uint32_t ctrl);
	int32_t (*PowerControl)(uint32_t ctrl, uint32_t state);

	/* Controller identity, from the Identify Controller response. `model`
	 * must hold room for 40 bytes plus a terminator. */
	int32_t (*Identify)(uint32_t ctrl, char *model, uint32_t model_size);

	int32_t (*ReadBlocks)(uint32_t ctrl, uint64_t lba, void *data,
			      uint32_t block_count);
	int32_t (*WriteBlocks)(uint32_t ctrl, uint64_t lba, const void *data,
			       uint32_t block_count);
	int32_t (*Flush)(uint32_t ctrl);

	/* Polled completion query for synchronous users: the number of commands
	 * completed since the last call. */
	uint32_t (*GetTransferStatus)(uint32_t ctrl);
} ARM_DRIVER_NVME;

#ifdef __cplusplus
}
#endif

#endif /* FREEWIFICAM_NVME_H */
