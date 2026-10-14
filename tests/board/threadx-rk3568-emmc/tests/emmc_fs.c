/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    emmc_fs.c
 * @brief   Case emmc-fs: format the card as exFAT, write one 1 GiB file
 *          sequentially, read it back, verify it byte-exact and checksum it.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h); the
 *          body drives the storage stack through its own faces only - the
 *          block-device seam (ports/fatfs/blkdev.h) and the mount face
 *          (ports/fatfs/fs.h) - so what it exercises on the board is the same
 *          interface every storage consumer will use.
 *
 * This case formats: it is the one case in the suite that destroys what is on
 * the card, and the round that runs it says so in its ledger. The volume is the
 * whole device (the storage line's shape on this board, the same one the NVMe
 * line uses), the filesystem sector is 4096 bytes, and the file is 1 GiB - past
 * the FAT32 file ceiling, so a pass is also a witness that the volume really is
 * exFAT.
 *
 * The verify case beside this one reads the same file back after a cold boot
 * without formatting, which is what turns "the write worked" into "the write
 * survived the power loss".
 *
 * Anchors (the judge checks each on PASS):
 *   emmc-fs: bound emmc0 blocks=<n> sector=<n>
 *   emmc-fs: formatted exFAT sector=4096
 *   emmc-fs: mounted type=exFAT sector=4096
 *   emmc-fs: write bytes=<n> ms=<n> kib=<n>
 *   emmc-fs: read bytes=<n> ms=<n> kib=<n>
 *   emmc-fs: verify ok checksum=<8 hex digits>
 *   emmc-fs: conservation tx=<n> rx=<n> boundaries=<n>
 */

#include <stdint.h>
#include <string.h>

#include "board_suite.h"
#include "emmc_cases.h"
#include "emmc_fs.h"

#include "blkdev.h"
#include "fs.h"

#include "ff.h"

#include "dwc_mshc.h"
#include "sdmmc_mmc.h"

#include "log.h"

/* One shared chunk buffer, page-aligned so the stack hands the controller the
 * caller's buffer instead of copying through its staging buffer. */
static uint8_t emmc_fs_buf[EMMC_FS_CHUNK] __attribute__((aligned(4096)));

/* Fill the chunk that covers the file offset `base`. */
static void emmc_fs_fill( uint32_t base, uint32_t len )
{
	uint32_t i;

	for( i = 0U; i < len; i++ ) {
		emmc_fs_buf[i] = emmc_pattern( base + i );
	}
}

/* Write the whole file sequentially. Returns 0 on success, -1 otherwise (with
 * *ms_out holding the elapsed time either way). */
static int emmc_fs_write( uint32_t *ms_out )
{
	FIL fp;
	FRESULT fr;
	uint32_t done;
	uint32_t start_ms;

	fr = f_open( &fp, EMMC_FS_FILE, FA_CREATE_ALWAYS | FA_WRITE );
	if( fr != FR_OK ) {
		LOG_ERROR( "emmc-fs: cannot create %s (FR_%d)\n", EMMC_FS_FILE, (int)fr );
		return -1;
	}

	start_ms = emmc_now_ms();
	for( done = 0U; done < EMMC_FS_BYTES; ) {
		uint32_t chunk = EMMC_FS_BYTES - done;
		UINT wrote = 0U;

		if( chunk > EMMC_FS_CHUNK ) {
			chunk = EMMC_FS_CHUNK;
		}
		emmc_fs_fill( done, chunk );
		fr = f_write( &fp, emmc_fs_buf, chunk, &wrote );
		if( fr != FR_OK || wrote != chunk ) {
			break;
		}
		done += chunk;
	}
	/* f_close flushes the volume: a close failure is a real write error. */
	fr = f_close( &fp );
	*ms_out = ( emmc_now_ms() - start_ms ) + 1U;

	if( fr != FR_OK ) {
		LOG_ERROR( "emmc-fs: write stopped after %lu bytes (FR_%d)\n",
			   (unsigned long)done, (int)fr );
		return -1;
	}
	if( done != EMMC_FS_BYTES ) {
		LOG_ERROR( "emmc-fs: write was short, %lu bytes\n", (unsigned long)done );
		return -1;
	}

	return 0;
}

int emmc_fs_read_verify( uint32_t *hash_out, uint32_t *ms_out, int *mismatch_out )
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

	fr = f_open( &fp, EMMC_FS_FILE, FA_READ );
	if( fr != FR_OK ) {
		LOG_ERROR( "emmc-fs: cannot open %s (FR_%d)\n", EMMC_FS_FILE, (int)fr );
		return -1;
	}

	start_ms = emmc_now_ms();
	for( done = 0U; done < EMMC_FS_BYTES; ) {
		uint32_t chunk = EMMC_FS_BYTES - done;
		UINT got = 0U;
		uint32_t i;

		if( chunk > EMMC_FS_CHUNK ) {
			chunk = EMMC_FS_CHUNK;
		}
		(void)memset( emmc_fs_buf, 0, chunk );
		fr = f_read( &fp, emmc_fs_buf, chunk, &got );
		if( fr != FR_OK || got != chunk ) {
			break;
		}
		for( i = 0U; i < chunk; i++ ) {
			if( emmc_fs_buf[i] != emmc_pattern( done + i ) ) {
				mismatch = (int)( done + i );
				break;
			}
		}
		if( mismatch >= 0 ) {
			break;
		}
		hash = emmc_fnv1a( emmc_fs_buf, chunk, hash );
		done += chunk;
	}
	*ms_out = ( emmc_now_ms() - start_ms ) + 1U;
	(void)f_close( &fp );

	*hash_out = hash;
	*mismatch_out = mismatch;

	if( fr != FR_OK ) {
		LOG_ERROR( "emmc-fs: read failed after %lu bytes (FR_%d)\n",
			   (unsigned long)done, (int)fr );
		return -1;
	}
	if( done != EMMC_FS_BYTES && mismatch < 0 ) {
		LOG_ERROR( "emmc-fs: read was short, %lu bytes\n", (unsigned long)done );
		return -2;
	}

	return 0;
}

void emmc_fs_chain( void )
{
	struct sdmmc_mmc_info info;
	struct dwc_mshc_census census;
	BLKDEV_CAPABILITIES caps;
	uint32_t ms;
	uint32_t hash;
	int mismatch;
	int ret;

	if( emmc_bringup() != 0 ) {
		board_case_fail( "the card did not come up" );
		return;
	}

	if( blkdev_get_capabilities( EMMC_FS_VOLUME, &caps ) != BLKDEV_OK ) {
		board_case_fail( "the block device is not registered" );
		return;
	}
	LOG_ANCHOR( "emmc-fs: bound %s blocks=%lu sector=%u\n",
		    blkdev_name( EMMC_FS_VOLUME ), (unsigned long)caps.sector_count,
		    (unsigned)caps.sector_size );

	if( fs_init() != 0 ) {
		board_case_fail( "the filesystem layer did not bind the block device" );
		return;
	}

	/* Format the whole device, then mount it. Formatting unmounts first, so the
	 * order is format-then-mount and not the other way round. */
	if( fs_format_exfat( EMMC_FS_VOLUME ) != 0 ) {
		board_case_fail( "the format did not complete" );
		return;
	}
	LOG_ANCHOR( "emmc-fs: formatted exFAT sector=%u\n",
		    (unsigned)fs_sector_size() );

	if( fs_mount( EMMC_FS_VOLUME ) != 0 || !fs_is_mounted( EMMC_FS_VOLUME ) ) {
		board_case_fail( "the volume did not mount after formatting" );
		return;
	}
	LOG_ANCHOR( "emmc-fs: mounted type=%s sector=%u\n",
		    fs_type_name( EMMC_FS_VOLUME ), (unsigned)fs_sector_size() );

	if( emmc_fs_write( &ms ) != 0 ) {
		board_case_fail( "the file write failed" );
		return;
	}
	LOG_ANCHOR( "emmc-fs: write bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)EMMC_FS_BYTES, (unsigned long)ms,
		    (unsigned long)emmc_kib( EMMC_FS_BYTES, ms ) );
	if( emmc_kib( EMMC_FS_BYTES, ms ) < EMMC_FS_MIN_WRITE_KIB ) {
		board_case_fail( "the write throughput is below this round's floor" );
		return;
	}

	ret = emmc_fs_read_verify( &hash, &ms, &mismatch );
	if( ret == -1 ) {
		board_case_fail( "the file read failed" );
		return;
	}
	if( mismatch >= 0 ) {
		LOG_ERROR( "emmc-fs: mismatch at file offset %d\n", mismatch );
		board_case_fail( "the file did not read back byte-exact" );
		return;
	}
	if( ret == -2 ) {
		board_case_fail( "the file read was short" );
		return;
	}
	LOG_ANCHOR( "emmc-fs: read bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)EMMC_FS_BYTES, (unsigned long)ms,
		    (unsigned long)emmc_kib( EMMC_FS_BYTES, ms ) );
	LOG_ANCHOR( "emmc-fs: verify ok checksum=%08lx\n", (unsigned long)hash );
	if( emmc_kib( EMMC_FS_BYTES, ms ) < EMMC_FS_MIN_READ_KIB ) {
		board_case_fail( "the read throughput is below this round's floor" );
		return;
	}

	/* The conservation line: what the controller actually moved, in both
	 * directions. A run that wrote and read a gigabyte cannot have moved less
	 * than a gigabyte each way, and a path that silently stopped moving data
	 * would show here even if the file's contents happened to match. */
	dwc_mshc_census_get( &census );
	LOG_ANCHOR( "emmc-fs: conservation tx=%lu rx=%lu boundaries=%lu\n",
		    (unsigned long)census.tx_bytes, (unsigned long)census.rx_bytes,
		    (unsigned long)census.boundary_events );

	if( census.tx_bytes < EMMC_FS_BYTES || census.rx_bytes < EMMC_FS_BYTES ) {
		board_case_fail( "the controller moved less than the file's worth of data" );
		return;
	}

	/* The card's own identity is re-read here so the file case's evidence stands
	 * alone in the transcript (a round that selects only this case still shows
	 * which card and which mode it ran on). */
	if( sdmmc_mmc_info_get( &info ) == 0 ) {
		LOG_INFO( "emmc-fs: card %s mode=%s bus=%u clock=%uMHz\n",
			  info.product,
			  ( info.timing == 1U ) ? "HS" : "other",
			  (unsigned)( ( info.width == 2U ) ? 8U : 4U ),
			  (unsigned)( info.clock_hz / 1000000U ) );
	}
}

BOARD_TEST( "emmc-fs", emmc_fs_chain );
