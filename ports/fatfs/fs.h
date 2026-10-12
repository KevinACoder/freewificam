/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fs.h
 * @brief   Project-owned interface: the filesystem over the block devices.
 *
 * The implementation is ports/fatfs/ (the vendored FatFs R0.16 plus the
 * first-party adapter). Nothing of FatFs is visible here - not ff.h, not the
 * drive numbering, not the mount state - so swapping the filesystem library is a
 * change to one port, not to the application. Same shape as the shell and net
 * interfaces, and for the same reason.
 *
 * Volumes are numbered the way the block-device seam numbers its devices: volume
 * n is block device n. fs_init() binds whatever the storage driver registered; a
 * volume with no filesystem on it simply does not mount, which is a reportable
 * state, not an error in the binding.
 *
 * CONTRACT:
 *  - fs_init() is idempotent and callable from a task only (binding probes the
 *    device, which blocks). It returns 0 once the binding exists.
 *  - fs_mount/fs_unmount/fs_format_exfat take a volume index; out of range is a
 *    parameter error. Mounting a volume that already is mounted is a no-op.
 *  - fs_format_exfat() destroys the volume's filesystem and unmounts it first.
 *  - Return values are 0 on success and non-zero on failure (the caller treats
 *    any non-zero as "did not happen"; the numeric code is for logging).
 */

#ifndef FREEWIFICAM_PORTS_FATFS_FS_H
#define FREEWIFICAM_PORTS_FATFS_FS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bind the block devices to the filesystem. Idempotent. */
int fs_init(void);

/* Number of volumes the filesystem layer knows about (bound block devices). */
uint32_t fs_volume_count(void);

/* Mount/unmount one volume. */
int fs_mount(uint32_t volume);
int fs_unmount(uint32_t volume);

/* Format a volume as exFAT with this build's fixed 4096-byte sector. Destructive:
 * unmounts the volume first. */
int fs_format_exfat(uint32_t volume);

bool fs_is_mounted(uint32_t volume);

/* The mounted filesystem's type name ("exFAT", "FAT32", ...), or NULL when the
 * volume is not mounted. */
const char *fs_type_name(uint32_t volume);

/* The filesystem sector size in bytes (the fixed window size, 4096). */
uint32_t fs_sector_size(void);

/* Total and free bytes on a mounted volume. Returns 0 on success. */
int fs_usage(uint32_t volume, uint64_t *total_bytes, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif

#endif /* FREEWIFICAM_PORTS_FATFS_FS_H */
