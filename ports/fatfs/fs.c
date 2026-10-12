/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fs.c
 * @brief   The mount face: bind the block devices, mount, format, report.
 *
 * This is where this port's decisions show up, so they are stated here:
 *
 *   - A volume that cannot be mounted is not an error of the binding. The media
 *     arrives with whatever was on it (an old filesystem, a partition table, or
 *     nothing); "no filesystem here" is a fact to report, not a failure to hide.
 *     fs_init() still returns 0 once the binding exists.
 *   - The mapping from volume to device is the seam's: volume n is block device
 *     n, and this file is the only place that pairing is read as a FatFs drive
 *     ("0:", "1:", ...).
 *   - FatFs keeps the mount state in the FATFS object; whether a volume is
 *     mounted is tracked here, because FatFs has no query for it and asking by
 *     attempting a mount would touch the device.
 */

#include <stddef.h>
#include <stdint.h>

#include "ff.h"

#include "blkdev.h"
#include "fs.h"

static FATFS volume_fs[FF_VOLUMES];
static uint8_t volume_mounted[FF_VOLUMES];
static int fs_ready;

/* FatFs's logical drive name. FF_STR_VOLUME_ID == 0 makes it the number, so
 * "0:" .. "3:". Built without a formatter so the port needs no libc. */
static void volume_path(uint32_t volume, char *out)
{
	out[0] = (char)('0' + (volume % 10u));
	out[1] = ':';
	out[2] = '\0';
}

int fs_init(void)
{
	if (fs_ready) {
		return 0;
	}

	if (blkdev_bind_nvme() != BLKDEV_OK) {
		return -1;
	}

	fs_ready = 1;

	return 0;
}

uint32_t fs_volume_count(void)
{
	uint32_t count = blkdev_count();

	return (count > FF_VOLUMES) ? FF_VOLUMES : count;
}

bool fs_is_mounted(uint32_t volume)
{
	if (volume >= FF_VOLUMES) {
		return false;
	}

	return volume_mounted[volume] != 0;
}

int fs_mount(uint32_t volume)
{
	char path[3];
	FRESULT fr;

	if (volume >= fs_volume_count()) {
		return FR_INVALID_DRIVE;
	}
	if (volume_mounted[volume]) {
		return 0;
	}

	volume_path(volume, path);
	fr = f_mount(&volume_fs[volume], path, 1);
	if (fr == FR_OK) {
		volume_mounted[volume] = 1;
	}

	return (int)fr;
}

int fs_unmount(uint32_t volume)
{
	char path[3];
	FRESULT fr;

	if (volume >= FF_VOLUMES) {
		return FR_INVALID_DRIVE;
	}
	if (!volume_mounted[volume]) {
		return 0;
	}

	volume_path(volume, path);
	/* opt == 0 unregisters the work area and flushes the volume. */
	fr = f_mount(NULL, path, 0);
	if (fr == FR_OK) {
		volume_mounted[volume] = 0;
	}

	return (int)fr;
}

int fs_format_exfat(uint32_t volume)
{
	static uint8_t work[4096] __attribute__((aligned(4096)));
	MKFS_PARM opt;
	char path[3];
	FRESULT fr;

	if (volume >= FF_VOLUMES) {
		return FR_INVALID_DRIVE;
	}

	/* The volume is about to be rewritten behind FatFs's back: drop any
	 * mounted view first. */
	(void)fs_unmount(volume);

	opt.fmt = FM_EXFAT;
	opt.n_fat = 0;		/* 0: default (1) */
	opt.align = 0;		/* 0: default (from the device) */
	opt.n_root = 0;		/* 0: default */
	opt.au_size = 0;	/* 0: default (auto) */

	volume_path(volume, path);

	/* The 4096-byte sector is this build's fixed SS(fs), not a parameter of
	 * f_mkfs: it formats with the configured sector size. */
	fr = f_mkfs(path, &opt, work, sizeof(work));

	return (int)fr;
}

static const char *fs_type_from(BYTE fs_type)
{
	switch (fs_type) {
	case FS_FAT12:
		return "FAT12";
	case FS_FAT16:
		return "FAT16";
	case FS_FAT32:
		return "FAT32";
	case FS_EXFAT:
		return "exFAT";
	default:
		return NULL;
	}
}

const char *fs_type_name(uint32_t volume)
{
	if (volume >= FF_VOLUMES || !volume_mounted[volume]) {
		return NULL;
	}

	return fs_type_from(volume_fs[volume].fs_type);
}

uint32_t fs_sector_size(void)
{
	/* The fixed window size this build was configured for (ffconf.h). */
	return (uint32_t)FF_MAX_SS;
}

int fs_usage(uint32_t volume, uint64_t *total_bytes, uint64_t *free_bytes)
{
	FATFS *fs = &volume_fs[volume];
	DWORD free_clusters = 0;
	char path[3];
	FRESULT fr;

	if (volume >= FF_VOLUMES || !volume_mounted[volume] ||
	    total_bytes == NULL || free_bytes == NULL) {
		return -1;
	}

	volume_path(volume, path);
	fr = f_getfree(path, &free_clusters, &fs);
	if (fr != FR_OK) {
		return (int)fr;
	}

	*total_bytes = (uint64_t)(fs->n_fatent - 2) * fs->csize * FF_MAX_SS;
	*free_bytes = (uint64_t)free_clusters * fs->csize * FF_MAX_SS;

	return 0;
}
