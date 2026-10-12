/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    pcie_nvme_fs.c
 * @brief   Cases pcie-nvme-fs (format + 1 GiB burn-in) and pcie-nvme-fs-verify
 *          (mount without format + checksum read-back): the NVMe + exFAT data
 *          path on the board.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case rows of the suite's table (tests/board/common/board_suite.h);
 *          the bodies drive the storage stack through its own faces only -
 *          the block-device seam (ports/fatfs/blkdev.h) and the mount face
 *          (ports/fatfs/fs.h) - so what they exercise on the board is the same
 *          interface every storage consumer will use.
 *
 * Two cases, one data path, two cold boots:
 *
 *   pcie-nvme-fs        formats the NVMe namespace as exFAT, mounts it, writes
 *                       one 1 GiB file sequentially in 64 KiB chunks, reads it
 *                       back, verifies it byte-exact and checksums it, and
 *                       checks that the completions consumed over the window
 *                       equal the message interrupts delivered. 1 GiB is past
 *                       the FAT32 file ceiling, so a pass is also a witness that
 *                       the volume really is exFAT.
 *   pcie-nvme-fs-verify mounts the same volume WITHOUT formatting it, reads the
 *                       same 1 GiB file back and checksums it. It exists so a
 *                       second (and third) cold boot can prove the volume the
 *                       first boot wrote survived the power loss - the file is
 *                       the same and the checksum must be the same.
 *
 * The burn-in buffer is one static 64 KiB chunk, page-aligned so the driver can
 * describe it with PRP entries instead of its bounce buffer.
 *
 * Anchors (the judge checks each on PASS):
 *   pcie-nvme-fs: bound nvme0 blocks=<n> sector=<n>
 *   pcie-nvme-fs: formatted exFAT sector=4096
 *   pcie-nvme-fs: mounted type=exFAT sector=4096
 *   pcie-nvme-fs: write bytes=<n> ms=<n> kib=<n>
 *   pcie-nvme-fs: read bytes=<n> ms=<n> kib=<n>
 *   pcie-nvme-fs: verify ok checksum=<8 hex digits>
 *   pcie-nvme-fs: conservation irqs=<n> completions=<n>
 *   pcie-nvme-fs-verify: mounted type=exFAT sector=4096
 *   pcie-nvme-fs-verify: read bytes=<n> ms=<n> kib=<n>
 *   pcie-nvme-fs-verify: verify ok checksum=<8 hex digits>
 *
 * Every step that can fail prints a reason and returns a FAIL verdict; no step
 * waits forever, and an absent NVMe is a verdict with a reason, not a park.
 */

#include <stdint.h>
#include <string.h>

#include "board_suite.h"
#include "pcie_cases.h"
#include "pcie_nvme_fs.h"

#include "blkdev.h"
#include "fs.h"

#include "ff.h"

#include "dwc_nvme.h"
#include "nvme.h"

#include "generic_timer.h"

#include "log.h"

/* The volume and the file the two cases share: the verify case reads exactly
 * the file the burn-in wrote, under the same name. */
#define PCIE_FS_VOLUME		0U
#define PCIE_FS_FILE		"0:/burn.bin"

/* The burn-in size and its chunk. 1 GiB is past the FAT32 file ceiling (so the
 * run witnesses exFAT) and 64 KiB keeps a request inside a handful of driver
 * chunks while staying well under the driver's per-command bound. */
#define PCIE_FS_BYTES		(1U << 30)
#define PCIE_FS_CHUNK		(64U * 1024U)

/* The floor each phase must clear, in KiB/s. */
#define PCIE_FS_MIN_KIB		4096U

/* One shared chunk buffer, page-aligned. */
static uint8_t pcie_fs_buf[PCIE_FS_CHUNK] __attribute__((aligned(4096)));

/* The deterministic pattern both cases write and verify: a hash of the byte's
 * file offset, so the reader regenerates it instead of storing it. The same
 * generator the `stor wr` command uses (ports/fatfs/fatfs_cmds.c), so a byte
 * range written here and one written there compare equal. */
static uint8_t pcie_fs_pattern( uint32_t index )
{
	uint32_t seed = index * 2654435761u + 12345u;

	return (uint8_t)( seed >> 16 );
}

static uint32_t pcie_fs_fnv1a( const uint8_t *data, uint32_t len, uint32_t hash )
{
	uint32_t i;

	for( i = 0U; i < len; i++ ) {
		hash ^= data[i];
		hash *= 16777619u;
	}

	return hash;
}

/* Fill the chunk that covers the file offset `base`. */
static void pcie_fs_fill( uint32_t base, uint32_t len )
{
	uint32_t i;

	for( i = 0U; i < len; i++ ) {
		pcie_fs_buf[i] = pcie_fs_pattern( base + i );
	}
}

/* Milliseconds from the board's generic counter. The frequency is invariant, so
 * two readings subtract to an elapsed time. */
static uint32_t pcie_fs_now_ms( void )
{
	uint32_t freq = gtimer_freq_get();

	if( freq == 0U ) {
		return 0U;
	}

	return (uint32_t)( ( gtimer_count_get() * 1000u ) / freq );
}

/* KiB/s over `bytes` in `ms` (ms is never zero: the callers add one). */
static uint32_t pcie_fs_kib( uint32_t bytes, uint32_t ms )
{
	return (uint32_t)( ( (uint64_t)bytes / 1024u ) * 1000u / ms );
}

/* Write the whole file sequentially. Returns 0 on success, -1 otherwise (with
 * *ms_out holding the elapsed time either way). */
static int pcie_fs_write( uint32_t *ms_out )
{
	FIL fp;
	FRESULT fr;
	uint32_t done;
	uint32_t start_ms;

	fr = f_open( &fp, PCIE_FS_FILE, FA_CREATE_ALWAYS | FA_WRITE );
	if( fr != FR_OK ) {
		LOG_ERROR( "pcie-nvme-fs: cannot create %s (FR_%d)\n", PCIE_FS_FILE, (int)fr );
		return -1;
	}

	start_ms = pcie_fs_now_ms();
	for( done = 0U; done < PCIE_FS_BYTES; ) {
		uint32_t chunk = PCIE_FS_BYTES - done;
		UINT wrote = 0U;

		if( chunk > PCIE_FS_CHUNK ) {
			chunk = PCIE_FS_CHUNK;
		}
		pcie_fs_fill( done, chunk );
		fr = f_write( &fp, pcie_fs_buf, chunk, &wrote );
		if( fr != FR_OK || wrote != chunk ) {
			break;
		}
		done += chunk;
	}
	/* f_close flushes the volume: a close failure is a real write error. */
	fr = f_close( &fp );
	*ms_out = ( pcie_fs_now_ms() - start_ms ) + 1U;

	if( fr != FR_OK ) {
		LOG_ERROR( "pcie-nvme-fs: write stopped after %lu bytes (FR_%d)\n",
			   (unsigned long)done, (int)fr );
		return -1;
	}
	if( done != PCIE_FS_BYTES ) {
		LOG_ERROR( "pcie-nvme-fs: write was short, %lu bytes\n", (unsigned long)done );
		return -1;
	}

	return 0;
}

/* Read the whole file back through the reader's path, verify it byte-exact
 * against the pattern and checksum it. Returns 0 on a full, matching read (with
 * *hash_out, *ms_out and *mismatch_out set); -1 on an I/O error; -2 on a short
 * read. A mismatch is reported through *mismatch_out (>= 0), not the return
 * value, so the caller can name the offset. */
static int pcie_fs_read_verify( uint32_t *hash_out, uint32_t *ms_out, int *mismatch_out )
{
	FIL fp;
	FRESULT fr;
	uint32_t done;
	uint32_t start_ms;
	uint32_t hash = 2166136261u;
	int mismatch = -1;

	*hash_out = hash;
	*ms_out = 1U;
	*mismatch_out = -1;

	fr = f_open( &fp, PCIE_FS_FILE, FA_READ );
	if( fr != FR_OK ) {
		LOG_ERROR( "pcie-nvme-fs: cannot open %s (FR_%d)\n", PCIE_FS_FILE, (int)fr );
		return -1;
	}

	start_ms = pcie_fs_now_ms();
	for( done = 0U; done < PCIE_FS_BYTES; ) {
		uint32_t chunk = PCIE_FS_BYTES - done;
		UINT got = 0U;
		uint32_t i;

		if( chunk > PCIE_FS_CHUNK ) {
			chunk = PCIE_FS_CHUNK;
		}
		fr = f_read( &fp, pcie_fs_buf, chunk, &got );
		if( fr != FR_OK || got != chunk ) {
			break;
		}
		for( i = 0U; i < got; i++ ) {
			if( pcie_fs_buf[i] != pcie_fs_pattern( done + i ) ) {
				mismatch = (int)( done + i );
				break;
			}
		}
		if( mismatch >= 0 ) {
			break;
		}
		hash = pcie_fs_fnv1a( pcie_fs_buf, got, hash );
		done += got;
	}
	(void)f_close( &fp );
	*ms_out = ( pcie_fs_now_ms() - start_ms ) + 1U;
	*hash_out = hash;
	*mismatch_out = mismatch;

	if( fr != FR_OK ) {
		return -1;
	}
	/* A mismatch ends the loop early (done < BYTES); it is reported through
	 * *mismatch_out, so it must not be mistaken for a short read. */
	if( mismatch < 0 && done != PCIE_FS_BYTES ) {
		return -2;
	}

	return 0;
}

/* --- case pcie-nvme-fs ----------------------------------------------------- */

static void pcie_nvme_fs_chain( void )
{
	BLKDEV_CAPABILITIES caps;
	uint32_t irq_before;
	uint32_t irq_after;
	uint32_t comp_window;
	uint32_t write_ms;
	uint32_t read_ms;
	uint32_t hash;
	uint32_t kib;
	int mismatch;
	int rc;
	const char *type;

	/* --- the bus and the block device ------------------------------------ */

	(void)pcie_nvme_bringup();
	if( blkdev_bind_nvme() != BLKDEV_OK ) {
		board_case_fail( "no NVMe controller to bind (the bus or the endpoint is absent)" );
		return;
	}
	if( blkdev_get_capabilities( PCIE_FS_VOLUME, &caps ) != BLKDEV_OK ) {
		board_case_fail( "the bound NVMe device reported no capabilities" );
		return;
	}
	LOG_ANCHOR( "pcie-nvme-fs: bound nvme0 blocks=%llu sector=%u\n",
		    (unsigned long long)caps.sector_count, caps.sector_size );

	/* --- format and mount ------------------------------------------------ */

	if( fs_format_exfat( PCIE_FS_VOLUME ) != 0 ) {
		LOG_ERROR( "pcie-nvme-fs: format failed\n" );
		board_case_fail( "the exFAT format did not complete" );
		return;
	}
	LOG_ANCHOR( "pcie-nvme-fs: formatted exFAT sector=%u\n", fs_sector_size() );

	if( fs_mount( PCIE_FS_VOLUME ) != 0 ) {
		LOG_ERROR( "pcie-nvme-fs: mount failed\n" );
		board_case_fail( "the freshly formatted volume did not mount" );
		return;
	}
	type = fs_type_name( PCIE_FS_VOLUME );
	if( type == NULL || strcmp( type, "exFAT" ) != 0 ) {
		board_case_fail( "the mounted volume is not exFAT" );
		return;
	}
	LOG_ANCHOR( "pcie-nvme-fs: mounted type=%s sector=%u\n", type, fs_sector_size() );

	/* --- the conservation window opens ----------------------------------- */

	irq_before = dwc_nvme_irq_count( PCIE_FS_VOLUME );
	/* GetTransferStatus() returns the completions consumed since the last call
	 * and resets the counter, so this call zeroes the window: the call after
	 * the I/O then returns exactly the completions this window consumed. */
	(void)Driver_NVME.GetTransferStatus( PCIE_FS_VOLUME );

	/* --- write 1 GiB sequentially ---------------------------------------- */

	if( pcie_fs_write( &write_ms ) != 0 ) {
		board_case_fail( "the 1 GiB write did not complete" );
		return;
	}
	kib = pcie_fs_kib( PCIE_FS_BYTES, write_ms );
	LOG_ANCHOR( "pcie-nvme-fs: write bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)PCIE_FS_BYTES, (unsigned long)write_ms, (unsigned long)kib );
	if( kib < PCIE_FS_MIN_KIB ) {
		board_case_fail( "the write throughput is below the 4096 KiB/s floor" );
		return;
	}

	/* --- read it back, byte-exact, checksummed --------------------------- */

	rc = pcie_fs_read_verify( &hash, &read_ms, &mismatch );
	if( rc != 0 ) {
		board_case_fail( (rc == -2) ? "the read-back was short"
					    : "the read-back did not complete" );
		return;
	}
	if( mismatch >= 0 ) {
		LOG_ERROR( "pcie-nvme-fs: mismatch at byte %d\n", mismatch );
		board_case_fail( "the read-back does not match the written pattern" );
		return;
	}
	kib = pcie_fs_kib( PCIE_FS_BYTES, read_ms );
	LOG_ANCHOR( "pcie-nvme-fs: read bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)PCIE_FS_BYTES, (unsigned long)read_ms, (unsigned long)kib );
	if( kib < PCIE_FS_MIN_KIB ) {
		board_case_fail( "the read throughput is below the 4096 KiB/s floor" );
		return;
	}
	LOG_ANCHOR( "pcie-nvme-fs: verify ok checksum=%08lx\n", (unsigned long)hash );

	/* --- conservation over the window ------------------------------------ */

	irq_after = dwc_nvme_irq_count( PCIE_FS_VOLUME );
	comp_window = Driver_NVME.GetTransferStatus( PCIE_FS_VOLUME );

	LOG_ANCHOR( "pcie-nvme-fs: conservation irqs=%u completions=%u\n",
		    irq_after - irq_before, comp_window );

	if( irq_after == irq_before ) {
		board_case_fail( "no message interrupt was delivered during the burn-in" );
		return;
	}
	if( comp_window != irq_after - irq_before ) {
		board_case_fail( "the completion and interrupt deltas disagree" );
		return;
	}
}

/* --- case pcie-nvme-fs-verify ---------------------------------------------- */

/* Registered by the sibling file tests/pcie_nvme_fs_verify.c: the two cases
 * live in two translation units so the image's BOARD_TESTS section lists them
 * in execution order (a single unit's entries are not emitted in source
 * order). This one shares every helper above. */
void pcie_nvme_fs_verify_chain( void )
{
	uint32_t read_ms;
	uint32_t hash;
	uint32_t kib;
	int mismatch;
	int rc;
	const char *type;

	(void)pcie_nvme_bringup();
	if( blkdev_bind_nvme() != BLKDEV_OK ) {
		board_case_fail( "no NVMe controller to bind (the bus or the endpoint is absent)" );
		return;
	}

	/* No format: the whole point is that the volume the previous cold boot
	 * wrote is still there. A mount failure is the persistence verdict. */
	if( fs_mount( PCIE_FS_VOLUME ) != 0 ) {
		LOG_ERROR( "pcie-nvme-fs-verify: mount failed\n" );
		board_case_fail( "the volume did not mount after the cold boot" );
		return;
	}
	type = fs_type_name( PCIE_FS_VOLUME );
	if( type == NULL || strcmp( type, "exFAT" ) != 0 ) {
		board_case_fail( "the mounted volume is not exFAT" );
		return;
	}
	LOG_ANCHOR( "pcie-nvme-fs-verify: mounted type=%s sector=%u\n", type, fs_sector_size() );

	rc = pcie_fs_read_verify( &hash, &read_ms, &mismatch );
	if( rc != 0 ) {
		board_case_fail( (rc == -2) ? "the read-back was short"
					    : "the read-back did not complete" );
		return;
	}
	if( mismatch >= 0 ) {
		LOG_ERROR( "pcie-nvme-fs-verify: mismatch at byte %d\n", mismatch );
		board_case_fail( "the read-back does not match the written pattern" );
		return;
	}
	kib = pcie_fs_kib( PCIE_FS_BYTES, read_ms );
	LOG_ANCHOR( "pcie-nvme-fs-verify: read bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)PCIE_FS_BYTES, (unsigned long)read_ms, (unsigned long)kib );
	LOG_ANCHOR( "pcie-nvme-fs-verify: verify ok checksum=%08lx\n", (unsigned long)hash );
}

BOARD_TEST( "pcie-nvme-fs", pcie_nvme_fs_chain );
