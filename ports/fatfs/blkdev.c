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
 *   - it is that registration point. The storage driver this image carries is
 *     adapted to the common block-device ops here, so nothing above this file
 *     knows whether a volume is an NVMe namespace behind a queue pair, a
 *     soldered eMMC behind the SD/MMC stack, or something else. Swapping or
 *     adding a driver is a change to this file.
 *
 * Which driver that is is selected by the build (BLKDEV_BACKEND_NVME or
 * BLKDEV_BACKEND_EMMC), because an image carries exactly one storage line and
 * linking the other one's driver would not resolve. A build that names neither
 * is a configuration error and is refused at compile time rather than silently
 * binding nothing.
 *
 * The driver handle and the driver's bring-up entry points are declared by hand
 * or through the driver's own stack-free face rather than by including the
 * driver's internal headers: what crosses this boundary is a frozen interface -
 * the ARM_DRIVER_NVME struct for the NVMe line, sdmmc_mmc.h for the eMMC one -
 * and nothing else. Same rule the lwIP adapter follows for its MAC handles.
 */

#include <stddef.h>
#include <stdint.h>

#include "blkdev.h"

#if defined(BLKDEV_BACKEND_NVME)

#include "Driver_Common.h"
#include "nvme.h"

/* The board's NVMe driver instance (board/rk3568-itx/drivers/nvme/dwc_nvme.c),
 * and its bring-up / count entry points. Declared here by hand so this file
 * depends only on the frozen interface header. */
extern ARM_DRIVER_NVME Driver_NVME;
extern void dwc_nvme_init(void);
extern uint32_t dwc_nvme_ctrl_count(void);

#elif defined(BLKDEV_BACKEND_EMMC)

#include "sdmmc_mmc.h"

#else
#error "blkdev.c needs BLKDEV_BACKEND_NVME or BLKDEV_BACKEND_EMMC: an image's storage backend is a build-time decision"
#endif

struct blkdev_entry {
	const BLKDEV_OPS *ops;
	void *ctx;
	const char *name;
};

static struct blkdev_entry devices[BLKDEV_MAX];
static uint32_t device_count;

#if defined(BLKDEV_BACKEND_NVME)
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
#endif /* BLKDEV_BACKEND_NVME */

#if defined(BLKDEV_BACKEND_EMMC)
/* --- the eMMC side: the soldered card is one block device ------------------ */

/*
 * The largest request this backend hands down as one transfer. The SD/MMC
 * stack copies a transfer through its staging buffer whenever the caller's
 * buffer is not page-aligned, and that buffer is exactly this size, so a larger
 * request would be a copy past its end. The size is the port's own
 * configuration (ports/fsl_sdmmc/sdmmc_mmc_card.c); this constant states what
 * the seam promises the layers above, and the two are checked against each
 * other by the board suite's own transfer sizes.
 */
#define BLKDEV_EMMC_MAX_TRANSFER	65536U

static int emmc_capabilities(void *ctx, BLKDEV_CAPABILITIES *caps)
{
	struct sdmmc_mmc_info info;

	(void)ctx;

	if (sdmmc_mmc_info_get(&info) != 0 || info.blocks == 0U) {
		return BLKDEV_ERR_NODEV;
	}

	caps->sector_count = info.blocks;
	caps->sector_size = SDMMC_MMC_SECTOR_SIZE;
	caps->max_transfer = BLKDEV_EMMC_MAX_TRANSFER;
	caps->erase_block_size = 0;	/* not read out of the card's EXT_CSD yet */
	caps->writable = 1;
	caps->media_present = 1;

	return BLKDEV_OK;
}

/* One direction of the transfer, in staging-buffer-sized chunks: the driver
 * below takes a whole request in one command, and its staging buffer is what
 * bounds how large that request may be. */
static int emmc_transfer(uint64_t lba, void *buf, uint32_t bytes, int write)
{
	while (bytes != 0U) {
		uint32_t chunk = (bytes > BLKDEV_EMMC_MAX_TRANSFER) ?
					 BLKDEV_EMMC_MAX_TRANSFER : bytes;
		uint32_t blocks = chunk / SDMMC_MMC_SECTOR_SIZE;
		int ret;

		if (blocks == 0U) {
			return BLKDEV_ERR_PARAM;
		}

		ret = write ? sdmmc_mmc_write(lba, buf, blocks) :
			      sdmmc_mmc_read(lba, buf, blocks);
		if (ret != 0) {
			return BLKDEV_ERR_IO;
		}

		lba += blocks;
		buf = (uint8_t *)buf + chunk;
		bytes -= chunk;
	}

	return BLKDEV_OK;
}

static int emmc_read(void *ctx, uint64_t lba, void *buf, uint32_t bytes)
{
	BLKDEV_CAPABILITIES caps;

	if (emmc_capabilities(ctx, &caps) != BLKDEV_OK ||
	    (bytes % caps.sector_size) != 0) {
		return BLKDEV_ERR_PARAM;
	}

	return emmc_transfer(lba, buf, bytes, 0);
}

static int emmc_write(void *ctx, uint64_t lba, const void *buf, uint32_t bytes)
{
	BLKDEV_CAPABILITIES caps;

	if (emmc_capabilities(ctx, &caps) != BLKDEV_OK ||
	    (bytes % caps.sector_size) != 0) {
		return BLKDEV_ERR_PARAM;
	}

	return emmc_transfer(lba, (void *)(uintptr_t)buf, bytes, 1);
}

static int emmc_flush(void *ctx)
{
	(void)ctx;

	return (sdmmc_mmc_flush() == 0) ? BLKDEV_OK : BLKDEV_ERR_IO;
}

static const BLKDEV_OPS emmc_ops = {
	.get_capabilities = emmc_capabilities,
	.read = emmc_read,
	.write = emmc_write,
	.flush = emmc_flush,
};
#endif /* BLKDEV_BACKEND_EMMC */

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

int blkdev_bind(void)
{
	if (device_count != 0) {
		return BLKDEV_OK;	/* already bound */
	}

#if defined(BLKDEV_BACKEND_NVME)
	/* Bring the PCIe backend up (idempotent) and probe the endpoint. A
	 * controller whose link or BAR never appeared is reported and skipped. */
	dwc_nvme_init();
	if (dwc_nvme_ctrl_count() == 0) {
		return BLKDEV_ERR_NODEV;
	}

	if (blkdev_register(&nvme_ops, (void *)(uintptr_t)0, "nvme0") < 0) {
		return BLKDEV_ERR_IO;
	}
#else
	/* Bring the card up (idempotent: the stack's own initialization ladder)
	 * and register it. A card that did not come up is reported and skipped. */
	if (sdmmc_mmc_card_start() != 0) {
		return BLKDEV_ERR_NODEV;
	}

	if (blkdev_register(&emmc_ops, NULL, "emmc0") < 0) {
		return BLKDEV_ERR_IO;
	}
#endif

	return BLKDEV_OK;
}
