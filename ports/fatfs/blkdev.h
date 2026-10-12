/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    blkdev.h
 * @brief   The block-device seam: one ops struct, one registry.
 *
 * This is the layer between the storage drivers (today the board's NVMe
 * controller) and every consumer of raw blocks (FatFs's diskio today, a
 * file-serving path or a disk tool tomorrow). The driver's own interface is
 * LBA-addressed and unit-specific; this header is the small common vocabulary it
 * is reduced to, so nothing above it knows which controller a volume sits on.
 *
 * The idiom is the project's usual one: an ops struct of function pointers plus
 * a registration point, dispatched through a registry rather than wired at
 * compile time - so which driver feeds which logical device is a registration
 * decision, not an #ifdef.
 *
 * CONTRACT:
 *  - Calls are task-context only and may block: a transfer waits for its
 *    completion inside the driver.
 *  - `lba` and `bytes` are in units of the device's own logical block
 *    (BLKDEV_CAPABILITIES.sector_size, normally 512). A `bytes` that is not a
 *    whole number of blocks is a parameter error. The 4096-byte filesystem
 *    sector FatFs uses is the adapter's business, not the seam's: diskio.c is
 *    where the two block sizes meet.
 *  - The caller owns every buffer; the driver neither frees nor retains it.
 *  - A device is writable unless BLKDEV_CAPABILITIES.writable says otherwise.
 *  - Return values are 0 (BLKDEV_OK) or a negative BLKDEV_ERR_* code.
 */

#ifndef FREEWIFICAM_PORTS_FATFS_BLKDEV_H
#define FREEWIFICAM_PORTS_FATFS_BLKDEV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes. 0 is success; every failure is a negative code so a caller can
 * test `< 0` without knowing the family. */
#define BLKDEV_OK		0
#define BLKDEV_ERR_PARAM	(-1)	/* bad index, alignment or size */
#define BLKDEV_ERR_IO		(-2)	/* the device refused or timed out */
#define BLKDEV_ERR_NODEV	(-3)	/* nothing registered / no medium */

/* Devices the registry can hold at once. One NVMe controller today; the spare
 * slots are for a USB mass-storage device or a second namespace later. */
#define BLKDEV_MAX		8

typedef struct {
	uint64_t sector_count;		/* device logical blocks */
	uint32_t sector_size;		/* bytes per logical block (the LBA unit) */
	uint32_t max_transfer;		/* largest single request, bytes */
	uint32_t erase_block_size;	/* device blocks, 0 when unknown */
	uint8_t writable;
	uint8_t media_present;		/* 0 = no medium (fixed disks: 1) */
} BLKDEV_CAPABILITIES;

/* One driver can offer several units, so every call names the device by the
 * registry index and the ops receive the driver's own context pointer. */
typedef struct {
	int (*get_capabilities)(void *ctx, BLKDEV_CAPABILITIES *caps);
	int (*read)(void *ctx, uint64_t lba, void *buf, uint32_t bytes);
	int (*write)(void *ctx, uint64_t lba, const void *buf, uint32_t bytes);
	/* Push any cache inside the device. A hint, as in FatFs's CTRL_SYNC: a
	 * device with no write cache may answer BLKDEV_OK without doing
	 * anything. */
	int (*flush)(void *ctx);
} BLKDEV_OPS;

/* --- registration (driver/binder side) ------------------------------------- */

/* Offer one unit of a driver to the registry. Returns the logical device index
 * (>= 0), or a negative BLKDEV_ERR_*. The registry copies nothing but the ops
 * pointer, the context pointer and the name, so all three must outlive the call
 * (static data). */
int blkdev_register(const BLKDEV_OPS *ops, void *ctx, const char *name);

/* Bind the board's NVMe controller 0 as device 0. Idempotent; returns BLKDEV_OK
 * when the controller came up and was registered, BLKDEV_ERR_NODEV when it did
 * not. This is the single place the storage driver is named. */
int blkdev_bind_nvme(void);

/* --- consumption (diskio and any other reader) ----------------------------- */

uint32_t blkdev_count(void);

/* Name of a registered device ("nvme0"), for reporting. NULL when out of range. */
const char *blkdev_name(uint32_t index);

int blkdev_get_capabilities(uint32_t index, BLKDEV_CAPABILITIES *caps);
int blkdev_read(uint32_t index, uint64_t lba, void *buf, uint32_t bytes);
int blkdev_write(uint32_t index, uint64_t lba, const void *buf, uint32_t bytes);
int blkdev_flush(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* FREEWIFICAM_PORTS_FATFS_BLKDEV_H */
