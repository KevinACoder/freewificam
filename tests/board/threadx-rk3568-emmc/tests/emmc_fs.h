/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    emmc_fs.h
 * @brief   What the two file cases share: the volume, the file, and the
 *          read-back routine the verify case reuses.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  the PCIe/NVMe suite's pcie_nvme_fs.h, with the storage line
 *          replaced by the eMMC one.
 */

#ifndef EMMC_FS_H
#define EMMC_FS_H

#include <stdint.h>

/* The volume and the file the two cases share: the verify case reads exactly
 * the file the burn-in wrote, under the same name. */
#define EMMC_FS_VOLUME		0U
#define EMMC_FS_FILE		"0:/burn.bin"

/* The burn-in size and its chunk. 1 GiB is past the FAT32 file ceiling (so the
 * run witnesses exFAT) and 64 Kbytes keeps one request inside a single staging
 * copy while staying well under the driver's per-command bound. */
#define EMMC_FS_BYTES		(1U << 30)
#define EMMC_FS_CHUNK		(64U * 1024U)

/* The floor each phase must clear, in KiB/s. The bus is 50 MHz over eight
 * lines, so its raw ceiling is 50 MB/s and this floor is a third of it: low
 * enough that a working path cannot miss it, high enough that a path which
 * silently degraded to a few megabytes a second is refused. The comparison the
 * round's ledger carries is against the kernel's own measurement on this card
 * at the same bus mode (43.7 MB/s sequential read), not against this floor. */
#define EMMC_FS_MIN_READ_KIB	16384U
#define EMMC_FS_MIN_WRITE_KIB	8192U

/* Read the shared file back, verify it byte-exact against the pattern and
 * checksum it. Returns 0 on a full, matching read (with *hash_out, *ms_out and
 * *mismatch_out set); -1 on an I/O error; -2 on a short read. A mismatch is
 * reported through *mismatch_out (>= 0), not the return value, so the caller can
 * name the offset. */
int emmc_fs_read_verify( uint32_t *hash_out, uint32_t *ms_out, int *mismatch_out );

/* The verify case's body: mount without formatting, read the file back, compare
 * and checksum. */
void emmc_fs_verify_chain( void );

#endif /* EMMC_FS_H */
