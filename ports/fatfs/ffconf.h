/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    ffconf.h
 * @brief   FatFs R0.16 configuration for this project - the shadow config header.
 *
 * FatFs reaches its configuration as `#include "ffconf.h"`, resolved relative to
 * the including file first. That is why the vendored tree (components/fatfs/)
 * deliberately carries no ffconf.h: the one here is the only one that exists, and
 * it is the whole of this integration's filesystem feature set. Nothing under
 * components/fatfs/ is modified - the same arrangement the lwIP adapter uses with
 * its lwipopts.h and the shell adapter with its config header.
 *
 * FFCONF_DEF must equal FF_DEFINED in the vendored ff.h, or FatFs refuses to
 * compile against a mismatched configuration revision.
 *
 * Values that were decided rather than left at upstream's default carry the
 * reason; everything else is upstream's default.
 */

#define FFCONF_DEF	80386	/* Revision ID of this configuration; must match
				 * FF_DEFINED in the vendored ff.h */

/*--- Function Configurations -------------------------------------------*/

#define FF_FS_READONLY	0	/* 0: read/write. The storage stack exists to
				 * be written to (the write/verify and format
				 * commands), so the read-only build is not it */

#define FF_FS_MINIMIZE	0	/* 0: all basic functions */

#define FF_USE_FIND	1	/* 1: f_findfirst/f_findnext, used by the
				 * `stor ls`/`stor find` view and cheap */

#define FF_USE_MKFS	1	/* 1: f_mkfs. The volume is formatted on the
				 * board (there is no other way to get an exFAT
				 * volume onto a blank namespace), so the format
				 * path is part of the delivered stack, not a
				 * host-side tool */

#define FF_USE_FASTSEEK	0	/* 0: no file seek acceleration table */

#define FF_USE_EXPAND	0	/* 0: no f_expand */

#define FF_USE_CHMOD	1	/* 1: f_chmod/f_utime; harmless, and a file
				 * command set that cannot clear the read-only
				 * bit is a nuisance */

#define FF_USE_LABEL	0	/* 0: no volume label API */

#define FF_USE_FORWARD	0	/* 0: no f_forward streaming */

#define FF_USE_STRFUNC	1	/* 1: f_puts/f_gets/f_printf, used by the
				 * text side of the `stor` command family */

#define FF_PRINT_LLI	1	/* 1: `%llu` in f_printf (the byte counts are
				 * 64-bit) */
#define FF_PRINT_FLOAT	0	/* 0: no floating point in f_printf */
#define FF_STRF_ENCODE	0	/* 0: ANSI/OEM in the stream functions */

/*--- Locale and Namespace Configurations --------------------------------*/

/* Code page 437 (US): the volume holds this project's own files with ASCII
 * names, and 437 keeps ffunicode.c's OEM conversion tables small. */
#define FF_CODE_PAGE	437

/* LFN on, dynamic buffer (mode 2). Mode 2 is the one that pairs with the
 * thread-safe configuration below: with FF_FS_REENTRANT on, FatFs rejects mode 1
 * outright (a static name buffer would be shared between threads on different
 * volumes). exFAT requires LFN (ff.c errors out on FF_FS_EXFAT without it), so
 * this is not optional here either. */
#define FF_USE_LFN	2
#define FF_MAX_LFN	255
#define FF_LFN_UNICODE	0	/* 0: ANSI/OEM in the API (ASCII names) */
#define FF_LFN_BUF	255
#define FF_SFN_BUF	12

#define FF_FS_RPATH	0	/* 0: no relative paths; callers pass
				 * volume-qualified paths ("0:/dir/file") */
#define FF_PATH_DEPTH	10

/* Four logical volumes, one per block device the seam can register. Only volume
 * 0 is bound today (the NVMe controller), but the array is sized for the seam's
 * capacity so a second device is a registration, not a rebuild. */
#define FF_VOLUMES	4
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"0","1","2","3"

/* One physical drive per volume: volume n is block device n (the seam's
 * numbering). No partition table is interpreted - the namespace is formatted as
 * a single volume - so FF_MULTI_PARTITION stays off and there is no VolToPart
 * table to keep in step with the registry. */
#define FF_MULTI_PARTITION	0

/* The large window: the filesystem sector is fixed at 4096 bytes.
 *
 * This is the throughput decision. FatFs's sector size is the granularity of
 * every window move, so it dominates sequential throughput; the device's own
 * logical block is 512 bytes (an NVMe namespace), and the diskio layer maps one
 * 4096-byte filesystem sector onto eight device blocks. Because the two bounds
 * are equal, FatFs never asks the device for its sector size (GET_SECTOR_SIZE is
 * not called) - the mapping factor is a compile-time constant, stated in one
 * place (ports/fatfs/diskio.c). */
#define FF_MIN_SS	4096
#define FF_MAX_SS	4096
#define FF_LBA64	0	/* 0: 32-bit LBA. At 4096-byte sectors the 32-bit
				 * limit is 16 TiB, far past this board's media;
				 * keeping LBA_t natural-width avoids 64-bit
				 * arithmetic in the block path */
#define FF_MIN_GPT	0x10000000
#define FF_USE_TRIM	0

/*--- System Configurations ---------------------------------------------*/

#define FF_FS_TINY	0	/* 0: each volume keeps its own window buffer
				 * (no per-API shared window) */

/* exFAT on: the milestone's volume format. It is what the "large window" is for
 * - exFAT is the format that accepts a 4096-byte sector without the FAT32
 * cluster-count ceiling, and it carries the >4 GiB file sizes a video dump
 * wants. */
#define FF_FS_EXFAT	1

/* No RTC is ported. FatFs then stamps files with one fixed date instead of
 * calling get_fattime() - the function is deliberately absent rather than
 * returning a lie. */
#define FF_FS_NORTC	1
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026

#define FF_FS_CRTIME	0	/* 0: no creation-time field handling */
#define FF_FS_NOFSINFO	0	/* 0: use the free-cluster hints in FSINFO */
#define FF_FS_LOCK	0	/* 0: no file lock table (single writer per
				 * volume in this build) */

/* Volume mutexes, implemented on CMSIS-RTOS2 in fatfs_os.c. Without them a shell
 * command and another context could interleave f_read/f_write on one volume and
 * corrupt the window buffer - the failure looks like random file content, not
 * like a race. */
#define FF_FS_REENTRANT	1
#define FF_FS_TIMEOUT	1000	/* ms; the CMSIS mutex acquire timeout */
