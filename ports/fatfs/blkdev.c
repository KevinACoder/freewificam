/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    blkdev.c
 * @brief   The block-device registry, and the one place this board's storage
 *          driver is named.
 *
 * Two jobs, deliberately in one file:
 *
 *   - it implements blkdev.h's registry: a table of ops + context + name, filled
 *     at run time (an ops struct plus a single registration point, not a
 *     compile-time device list);
 *   - it is that registration point. The NVMe controller is adapted to the
 *     common block-device ops here, so nothing above this file knows whether a
 *     volume is an NVMe namespace behind a queue pair or something else.
 *     Swapping or adding a driver is a change to this file.
 *
 * The driver handle and the driver's bring-up entry points are declared by hand
 * rather than by including the driver's internal header: what crosses this
 * boundary is the frozen ARM_DRIVER_NVME interface (nvme.h), nothing else. Same
 * rule the lwIP adapter follows for its MAC handles.
 */

#include <stdint.h>

#include "Driver_Common.h"
#include "blkdev.h"
#include "nvme.h"

/* The board's NVMe driver instance (board/rk3568-itx/drivers/nvme/dwc_nvme.c),
 * and its bring-up / count entry points. Declared here by hand so this file
 * depends only on the frozen interface header. */
extern ARM_DRIVER_NVME Driver_NVME;
extern void dwc_nvme_init(void);
extern uint32_t dwc_nvme_ctrl_count(void);

struct blkdev_entry {
	const BLKDEV_OPS *ops;
	void *ctx;
	const char *name;
};

static struct blkdev_entry devices[BLKDEV_MAX];
static uint32_t device_count;

/* --- the NVMe side: a controller is one block device ----------------------- */

/* The context is the controller index, carried as a pointer-sized integer. */
static uint32_t nvme_ctrl_of(void *ctx)
{
	return (uint32_t)(uintptr_t)ctx;
}

static int nvme_capabilities(void *ctx, BLKDEV_CAPABILITIES *caps)
{
	NVME_CAPABILITIES nvme = Driver_NVME.GetCapabilities(nvme_ctrl_of(ctx));

	if (nvme.sector_count == 0 || nvme.sector_size == 0) {
		return BLKDEV_ERR_NODEV;
	}

	caps->sector_count = nvme.sector_count;
	caps->sector_size = nvme.sector_size;
	caps->max_transfer = nvme.max_transfer;
	caps->erase_block_size = 0;	/* not reported by the NVMe path */
	caps->writable = 1;
	caps->media_present = 1;

	return BLKDEV_OK;
}

static int nvme_read(void *ctx, uint64_t lba, void *buf, uint32_t bytes)
{
	BLKDEV_CAPABILITIES caps;

	if (nvme_capabilities(ctx, &caps) != BLKDEV_OK ||
	    caps.sector_size == 0 || (bytes % caps.sector_size) != 0) {
		return BLKDEV_ERR_PARAM;
	}

	return (Driver_NVME.ReadBlocks(nvme_ctrl_of(ctx), lba, buf,
				       bytes / caps.sector_size) == ARM_DRIVER_OK)
		       ? BLKDEV_OK
		       : BLKDEV_ERR_IO;
}

static int nvme_write(void *ctx, uint64_t lba, const void *buf, uint32_t bytes)
{
	BLKDEV_CAPABILITIES caps;

	if (nvme_capabilities(ctx, &caps) != BLKDEV_OK ||
	    caps.sector_size == 0 || (bytes % caps.sector_size) != 0) {
		return BLKDEV_ERR_PARAM;
	}

	return (Driver_NVME.WriteBlocks(nvme_ctrl_of(ctx), lba, buf,
					bytes / caps.sector_size) == ARM_DRIVER_OK)
		       ? BLKDEV_OK
		       : BLKDEV_ERR_IO;
}

static int nvme_flush(void *ctx)
{
	return (Driver_NVME.Flush(nvme_ctrl_of(ctx)) == ARM_DRIVER_OK)
		       ? BLKDEV_OK
		       : BLKDEV_ERR_IO;
}

static const BLKDEV_OPS nvme_ops = {
	.get_capabilities = nvme_capabilities,
	.read = nvme_read,
	.write = nvme_write,
	.flush = nvme_flush,
};

/* --- registry (blkdev.h) --------------------------------------------------- */

int blkdev_register(const BLKDEV_OPS *ops, void *ctx, const char *name)
{
	if (ops == NULL || name == NULL) {
		return BLKDEV_ERR_PARAM;
	}
	if (device_count >= BLKDEV_MAX) {
		return BLKDEV_ERR_IO;
	}

	devices[device_count].ops = ops;
	devices[device_count].ctx = ctx;
	devices[device_count].name = name;

	return (int)device_count++;
}

uint32_t blkdev_count(void)
{
	return device_count;
}

const char *blkdev_name(uint32_t index)
{
	if (index >= device_count) {
		return NULL;
	}

	return devices[index].name;
}

int blkdev_get_capabilities(uint32_t index, BLKDEV_CAPABILITIES *caps)
{
	if (index >= device_count || caps == NULL) {
		return BLKDEV_ERR_PARAM;
	}

	return devices[index].ops->get_capabilities(devices[index].ctx, caps);
}

int blkdev_read(uint32_t index, uint64_t lba, void *buf, uint32_t bytes)
{
	if (index >= device_count) {
		return BLKDEV_ERR_PARAM;
	}

	return devices[index].ops->read(devices[index].ctx, lba, buf, bytes);
}

int blkdev_write(uint32_t index, uint64_t lba, const void *buf, uint32_t bytes)
{
	if (index >= device_count) {
		return BLKDEV_ERR_PARAM;
	}

	return devices[index].ops->write(devices[index].ctx, lba, buf, bytes);
}

int blkdev_flush(uint32_t index)
{
	if (index >= device_count) {
		return BLKDEV_ERR_PARAM;
	}

	return devices[index].ops->flush(devices[index].ctx);
}

/* --- the registration point ------------------------------------------------ */

int blkdev_bind_nvme(void)
{
	if (device_count != 0) {
		return BLKDEV_OK;	/* already bound */
	}

	/* Bring the PCIe backend up (idempotent) and probe the endpoint. A
	 * controller whose link or BAR never appeared is reported and skipped. */
	dwc_nvme_init();
	if (dwc_nvme_ctrl_count() == 0) {
		return BLKDEV_ERR_NODEV;
	}

	if (blkdev_register(&nvme_ops, (void *)(uintptr_t)0, "nvme0") < 0) {
		return BLKDEV_ERR_IO;
	}

	return BLKDEV_OK;
}
