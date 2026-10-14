/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    emmc_bw.c
 * @brief   Case emmc-bw: the bus's own number - a bounded window of sequential
 *          reads and writes, reported against the kernel's measurement of the
 *          same card at the same bus mode.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h);
 *          the bounded-window shape follows the wireless data-plane case
 *          (tests/board/threadx-rk3568-rtl8189/tests/wlan8189_dataplane.c: one
 *          fixed window per direction, the window's own byte count reported
 *          beside the rate).
 *
 * Why this case exists: the storage line's number is a comparison point for
 * every other bus on this board. The eMMC runs 50 MHz over eight lines (a
 * 50 MB/s raw ceiling) and the SDIO WiFi part runs 50 MHz over four (25 MB/s),
 * so a card that delivers its share of the eight-line ceiling says how much of
 * a wireless result is the bus and how much is the stack above it.
 *
 * The window is bounded twice: by a fixed byte count (256 MiB, so the case's
 * cost is the same on every boot) and by the file it works on (`0:/bw.bin`,
 * a second file inside the same volume, so this case cannot disturb the file
 * the persistence cases verify).
 *
 * Anchors (the judge checks each on PASS):
 *   emmc-bw: mode HS bus 8-bit clock=<n>MHz
 *   emmc-bw: write bytes=<n> ms=<n> kib=<n>
 *   emmc-bw: read bytes=<n> ms=<n> kib=<n>
 *   emmc-bw: reference linux-52mhz-read=<n>kib
 */

#include <stdint.h>
#include <string.h>

#include "board_suite.h"
#include "emmc_cases.h"
#include "emmc_fs.h"

#include "fs.h"

#include "ff.h"

#include "sdmmc_mmc.h"

#include "log.h"

/* The measured window: 256 MiB in 64 Kbyte chunks. */
#define EMMC_BW_BYTES		(256U * 1024U * 1024U)
#define EMMC_BW_CHUNK		(64U * 1024U)

/* The file this case works on, inside the volume the file cases formatted. */
#define EMMC_BW_FILE		"0:/bw.bin"

/* The kernel's sequential-read measurement on this same card at the same bus
 * mode (the lab's linux-upstream round: 43.7 MB/s at 52 MHz, HS, eight lines).
 * It is carried into the transcript so the log states its own comparison basis
 * instead of leaving the reader to find it. */
#define EMMC_BW_REFERENCE_KIB	44749U

static uint8_t emmc_bw_buf[EMMC_BW_CHUNK] __attribute__((aligned(4096)));

static void emmc_bw_fill( uint32_t base, uint32_t len )
{
	uint32_t i;

	for( i = 0U; i < len; i++ ) {
		emmc_bw_buf[i] = emmc_pattern( base + i );
	}
}

static int emmc_bw_write( uint32_t *ms_out )
{
	FIL fp;
	FRESULT fr;
	uint32_t done;
	uint32_t start_ms;

	fr = f_open( &fp, EMMC_BW_FILE, FA_CREATE_ALWAYS | FA_WRITE );
	if( fr != FR_OK ) {
		LOG_ERROR( "emmc-bw: cannot create %s (FR_%d)\n", EMMC_BW_FILE, (int)fr );
		return -1;
	}

	start_ms = emmc_now_ms();
	for( done = 0U; done < EMMC_BW_BYTES; ) {
		uint32_t chunk = EMMC_BW_BYTES - done;
		UINT wrote = 0U;

		if( chunk > EMMC_BW_CHUNK ) {
			chunk = EMMC_BW_CHUNK;
		}
		emmc_bw_fill( done, chunk );
		fr = f_write( &fp, emmc_bw_buf, chunk, &wrote );
		if( fr != FR_OK || wrote != chunk ) {
			break;
		}
		done += chunk;
	}
	fr = f_close( &fp );
	*ms_out = ( emmc_now_ms() - start_ms ) + 1U;

	if( fr != FR_OK || done != EMMC_BW_BYTES ) {
		LOG_ERROR( "emmc-bw: write stopped after %lu bytes (FR_%d)\n",
			   (unsigned long)done, (int)fr );
		return -1;
	}

	return 0;
}

static int emmc_bw_read( uint32_t *ms_out )
{
	FIL fp;
	FRESULT fr;
	uint32_t done;
	uint32_t start_ms;

	fr = f_open( &fp, EMMC_BW_FILE, FA_READ );
	if( fr != FR_OK ) {
		LOG_ERROR( "emmc-bw: cannot open %s (FR_%d)\n", EMMC_BW_FILE, (int)fr );
		return -1;
	}

	start_ms = emmc_now_ms();
	for( done = 0U; done < EMMC_BW_BYTES; ) {
		uint32_t chunk = EMMC_BW_BYTES - done;
		UINT got = 0U;

		if( chunk > EMMC_BW_CHUNK ) {
			chunk = EMMC_BW_CHUNK;
		}
		fr = f_read( &fp, emmc_bw_buf, chunk, &got );
		if( fr != FR_OK || got != chunk ) {
			break;
		}
		done += chunk;
	}
	*ms_out = ( emmc_now_ms() - start_ms ) + 1U;
	(void)f_close( &fp );

	if( fr != FR_OK || done != EMMC_BW_BYTES ) {
		LOG_ERROR( "emmc-bw: read stopped after %lu bytes (FR_%d)\n",
			   (unsigned long)done, (int)fr );
		return -1;
	}

	return 0;
}

void emmc_bw_chain( void )
{
	struct sdmmc_mmc_info info;
	uint32_t ms;

	if( emmc_bringup() != 0 ) {
		board_case_fail( "the card did not come up" );
		return;
	}

	if( sdmmc_mmc_info_get( &info ) != 0 ) {
		board_case_fail( "no card identity after bring-up" );
		return;
	}

	LOG_ANCHOR( "emmc-bw: mode %s bus %u-bit clock=%uMHz\n",
		    ( info.timing == 1U ) ? "HS" : "other",
		    (unsigned)( ( info.width == 2U ) ? 8U : 4U ),
		    (unsigned)( info.clock_hz / 1000000U ) );

	if( fs_init() != 0 ) {
		board_case_fail( "the filesystem layer did not bind the block device" );
		return;
	}
	if( !fs_is_mounted( EMMC_FS_VOLUME ) && fs_mount( EMMC_FS_VOLUME ) != 0 ) {
		board_case_fail( "the volume is not mounted - run the file case first" );
		return;
	}

	if( emmc_bw_write( &ms ) != 0 ) {
		board_case_fail( "the measured write window failed" );
		return;
	}
	LOG_ANCHOR( "emmc-bw: write bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)EMMC_BW_BYTES, (unsigned long)ms,
		    (unsigned long)emmc_kib( EMMC_BW_BYTES, ms ) );

	if( emmc_bw_read( &ms ) != 0 ) {
		board_case_fail( "the measured read window failed" );
		return;
	}
	LOG_ANCHOR( "emmc-bw: read bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)EMMC_BW_BYTES, (unsigned long)ms,
		    (unsigned long)emmc_kib( EMMC_BW_BYTES, ms ) );

	LOG_ANCHOR( "emmc-bw: reference linux-52mhz-read=%ukib\n",
		    (unsigned)EMMC_BW_REFERENCE_KIB );
}

BOARD_TEST( "emmc-bw", emmc_bw_chain );
