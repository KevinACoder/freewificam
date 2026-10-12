/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    diskio.c
 * @brief   FatFs's physical drive layer: a logical volume is a block device.
 *
 * This is the other half of the integration (the first is ffconf.h): FatFs asks
 * for sectors by physical drive number and this file translates to blkdev.h,
 * where the storage driver has already stopped being distinguishable. Volume n
 * is block device n - the seam's numbering.
 *
 * The one interesting fact is the unit change. FF_MIN_SS == FF_MAX_SS == 4096,
 * so a FatFs sector is 4096 bytes and a FatFs sector number is in units of 4096
 * bytes, while the seam's unit is the device's own logical block (512 bytes for
 * an NVMe namespace). This is the single place the two block sizes meet:
 *
 *     device lba    = sector * (4096 / device_sector_size)   == sector * 8
 *     device blocks = count  * (4096 / device_sector_size)   == count  * 8
 *
 * Because the filesystem sector size is fixed, FatFs never calls GET_SECTOR_SIZE
 * - the factor is derived from the device capability at call time, and asserted
 * to be exact.
 *
 * Note what is NOT here: no cache, no bounce buffer, no alignment handling. The
 * driver owns that, and duplicating it would only add a place for the two copies
 * to disagree.
 */

#include <stdint.h>

#include "ff.h"
#include "diskio.h"

#include "blkdev.h"

/* The fixed filesystem sector size this build is configured for (ffconf.h). */
#define DISKIO_FS_SECTOR	4096

_Static_assert(FF_MAX_SS == DISKIO_FS_SECTOR && FF_MIN_SS == DISKIO_FS_SECTOR,
	       "diskio assumes the fixed 4096-byte filesystem sector");
_Static_assert(FF_VOLUMES >= 1, "at least one logical volume is needed");

/* What disk_status() reports. FatFs polls this, so it is kept rather than
 * re-derived (which would mean touching the device on every status call). */
static DSTATUS disk_state[FF_VOLUMES];

/* Translate a FatFs (sector, count) request into a device (lba, bytes) request.
 * The factor is 4096 / device_sector_size; it must be exact, or the request
 * cannot be expressed in whole device blocks. */
static DRESULT disk_map(BYTE pdrv, LBA_t sector, UINT count, uint64_t *lba,
			uint32_t *bytes)
{
	BLKDEV_CAPABILITIES caps;
	uint32_t mul;

	if (blkdev_get_capabilities(pdrv, &caps) != BLKDEV_OK) {
		return RES_ERROR;
	}
	if (caps.sector_size == 0 ||
	    (DISKIO_FS_SECTOR % caps.sector_size) != 0) {
		return RES_ERROR;
	}

	mul = DISKIO_FS_SECTOR / caps.sector_size;	/* 4096 / 512 == 8 */
	*lba = (uint64_t)sector * mul;
	*bytes = (uint32_t)((uint64_t)count * DISKIO_FS_SECTOR);

	return RES_OK;
}

DSTATUS disk_status(BYTE pdrv)
{
	if (pdrv >= FF_VOLUMES) {
		return STA_NOINIT | STA_NODISK;
	}

	return disk_state[pdrv];
}

DSTATUS disk_initialize(BYTE pdrv)
{
	BLKDEV_CAPABILITIES caps;

	if (pdrv >= FF_VOLUMES) {
		return STA_NOINIT | STA_NODISK;
	}

	/* The device is already up: it was probed when it was bound. What is
	 * left is to report whether it can be used. */
	if (blkdev_get_capabilities(pdrv, &caps) != BLKDEV_OK) {
		disk_state[pdrv] = STA_NOINIT | STA_NODISK;
		return disk_state[pdrv];
	}

	disk_state[pdrv] = 0;
	if (!caps.media_present) {
		disk_state[pdrv] |= STA_NODISK | STA_NOINIT;
	}
	if (!caps.writable) {
		disk_state[pdrv] |= STA_PROTECT;
	}

	return disk_state[pdrv];
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
	uint64_t lba;
	uint32_t bytes;

	if (pdrv >= FF_VOLUMES || (disk_state[pdrv] & STA_NOINIT)) {
		return RES_NOTRDY;
	}
	if (disk_map(pdrv, sector, count, &lba, &bytes) != RES_OK) {
		return RES_ERROR;
	}

	/* The whole request goes down as one call: the driver splits it into
	 * command-sized chunks itself, and doing it here as well would only mean
	 * two places to keep in agreement. */
	if (blkdev_read(pdrv, lba, buff, bytes) != BLKDEV_OK) {
		return RES_ERROR;
	}

	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
	uint64_t lba;
	uint32_t bytes;

	if (pdrv >= FF_VOLUMES || (disk_state[pdrv] & STA_NOINIT)) {
		return RES_NOTRDY;
	}
	if (disk_state[pdrv] & STA_PROTECT) {
		return RES_WRPRT;
	}
	if (disk_map(pdrv, sector, count, &lba, &bytes) != RES_OK) {
		return RES_ERROR;
	}

	if (blkdev_write(pdrv, lba, buff, bytes) != BLKDEV_OK) {
		return RES_ERROR;
	}

	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
	BLKDEV_CAPABILITIES caps;
	uint32_t mul;

	if (pdrv >= FF_VOLUMES) {
		return RES_PARERR;
	}

	switch (cmd) {
	case CTRL_SYNC:
		/* A hint, as FatFs documents it: a device with no write cache
		 * may answer OK without doing anything. */
		return (blkdev_flush(pdrv) == BLKDEV_OK) ? RES_OK : RES_ERROR;

	case GET_SECTOR_COUNT:
		if (blkdev_get_capabilities(pdrv, &caps) != BLKDEV_OK) {
			return RES_ERROR;
		}
		if (caps.sector_size == 0 ||
		    (DISKIO_FS_SECTOR % caps.sector_size) != 0) {
			return RES_ERROR;
		}
		mul = DISKIO_FS_SECTOR / caps.sector_size;
		/* The count is expressed in the filesystem's 4096-byte sectors,
		 * so the device's 512-byte block count is divided down. */
		*(LBA_t *)buff = (caps.sector_count > (uint64_t)0xffffffffu * mul)
					 ? (LBA_t)0xffffffffu
					 : (LBA_t)(caps.sector_count / mul);
		return RES_OK;

	case GET_SECTOR_SIZE:
		/* The fixed filesystem sector size, not the device's block. It
		 * is a constant in this build: with FF_MIN_SS == FF_MAX_SS FatFs
		 * never calls this, but the answer must still be the size FatFs
		 * addresses in (4096), never the 512-byte device block. */
		*(WORD *)buff = (WORD)DISKIO_FS_SECTOR;
		return RES_OK;

	case GET_BLOCK_SIZE:
		if (blkdev_get_capabilities(pdrv, &caps) != BLKDEV_OK) {
			return RES_ERROR;
		}
		if (caps.sector_size == 0 ||
		    (DISKIO_FS_SECTOR % caps.sector_size) != 0) {
			return RES_ERROR;
		}
		mul = DISKIO_FS_SECTOR / caps.sector_size;
		/* Erase block size in filesystem sectors; 1 (unknown) is the
		 * documented way to say "no opinion" and is what f_mkfs aligns
		 * against. */
		*(DWORD *)buff = (caps.erase_block_size != 0)
					 ? (DWORD)(caps.erase_block_size / mul)
					 : 1;
		if (*(DWORD *)buff == 0) {
			*(DWORD *)buff = 1;
		}
		return RES_OK;

	default:
		return RES_PARERR;
	}
}

/* FatFs asks for the current time only when it stamps a file and only when
 * FF_FS_NORTC == 0. No RTC is ported, so this build uses the fixed timestamp
 * instead - the function is deliberately absent rather than returning a lie. */
#if FF_FS_NORTC == 0
#error "no RTC is ported: set FF_FS_NORTC to 1 or implement get_fattime()"
#endif
