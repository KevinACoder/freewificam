/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    emmc_block.c
 * @brief   Case emmc-block: a block round trip on the raw device - a
 *          deterministic pattern written, read back and verified byte-exact -
 *          plus a transfer that deliberately crosses the controller's SDMA
 *          boundary.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h); the
 *          body drives the line through the block-device seam (blkdev.h) and
 *          reads the controller's census (dwc_mshc.h), so what it exercises is
 *          the same path FatFs rides on.
 *
 * Two things this case exists for:
 *
 *   - The round trip is the data path's own witness: a pattern that the reader
 *     regenerates (not a stored copy) written, read back, compared byte for
 *     byte and checksummed. It runs near the END of the card's address space,
 *     away from the partition table and the boot firmware's own copy of itself
 *     at the start of the device, so a round that runs this case before the
 *     file cases still leaves the card's existing content alone.
 *
 *   - The boundary transfer. The controller's SDMA engine stops when the next
 *     byte would cross a 512 Kbyte-aligned address and waits to be pointed at
 *     the continuation address, so a transfer whose buffer straddles one is the
 *     only kind that exercises the continuation at all. The buffer here is
 *     placed so that the transfer starts 4 Kbytes before a boundary and runs 8
 *     Kbytes, which crosses it by construction; the case refuses a PASS unless
 *     the controller's census recorded the boundary event.
 *
 * Anchors (the judge checks each on PASS):
 *   emmc-block: write bytes=<n> ms=<n> kib=<n>
 *   emmc-block: read bytes=<n> ms=<n> kib=<n>
 *   emmc-block: verify ok checksum=<8 hex digits>
 *   emmc-block: cmd23 issued=<n>
 *   emmc-block: boundary-crossed lba=<n> len=<n> events=<n>
 */

#include <stdint.h>
#include <string.h>

#include "board_suite.h"
#include "emmc_cases.h"

#include "blkdev.h"

#include "dwc_mshc.h"
#include "sdmmc_mmc.h"

#include "log.h"

/* The round trip: 8 MiB in 64 Kbyte chunks, taken from the end of the card.
 * The window is a fixed distance below the card's last block, so it is the same
 * region on every boot of a given card and cannot collide with the card's first
 * megabytes. */
#define EMMC_BLOCK_BYTES	(8U * 1024U * 1024U)
#define EMMC_BLOCK_CHUNK	(64U * 1024U)
#define EMMC_BLOCK_TAIL_BLOCKS	0x20000U	/* 64 MiB of headroom */

/* The boundary transfer's shape: 4 Kbytes before a 512 Kbyte boundary, running
 * 8 Kbytes, so it crosses it once. */
#define EMMC_SDMA_BOUNDARY	(512U * 1024U)
#define EMMC_BOUNDARY_PRE	4096U
#define EMMC_BOUNDARY_LEN	8192U

/* One shared chunk buffer for the round trip, and one window large enough that
 * a 512 Kbyte boundary with room on both sides always exists inside it. */
static uint8_t emmc_block_buf[EMMC_BLOCK_CHUNK] __attribute__((aligned(4096)));
static uint8_t emmc_boundary_buf[EMMC_SDMA_BOUNDARY + EMMC_BOUNDARY_PRE + 4096U]
	__attribute__((aligned(4096)));

/* Fill the chunk that covers the device offset `base`. */
static void emmc_block_fill( uint32_t base, uint32_t len )
{
	uint32_t i;

	for( i = 0U; i < len; i++ ) {
		emmc_block_buf[i] = emmc_pattern( base + i );
	}
}

/* Write the whole window, chunk by chunk. Returns 0 on success. */
static int emmc_block_write( uint64_t lba, uint32_t *ms_out )
{
	uint32_t done;
	uint32_t start_ms = emmc_now_ms();

	for( done = 0U; done < EMMC_BLOCK_BYTES; done += EMMC_BLOCK_CHUNK ) {
		emmc_block_fill( done, EMMC_BLOCK_CHUNK );
		if( blkdev_write( 0U, lba + ( done / 512U ), emmc_block_buf,
				  EMMC_BLOCK_CHUNK ) != BLKDEV_OK ) {
			LOG_ERROR( "emmc-block: write failed at offset %lu\n",
				   (unsigned long)done );
			return -1;
		}
	}

	*ms_out = ( emmc_now_ms() - start_ms ) + 1U;

	return 0;
}

/* Read the whole window back and verify it byte-exact against the pattern, in
 * the same chunking. Returns 0 on a full, matching read; -1 on an I/O error;
 * -2 on a mismatch (with the offset printed). */
static int emmc_block_read_verify( uint64_t lba, uint32_t *hash_out, uint32_t *ms_out )
{
	uint32_t done;
	uint32_t start_ms = emmc_now_ms();
	uint32_t hash = 2166136261u;

	for( done = 0U; done < EMMC_BLOCK_BYTES; done += EMMC_BLOCK_CHUNK ) {
		uint32_t i;

		(void)memset( emmc_block_buf, 0, EMMC_BLOCK_CHUNK );
		if( blkdev_read( 0U, lba + ( done / 512U ), emmc_block_buf,
				 EMMC_BLOCK_CHUNK ) != BLKDEV_OK ) {
			LOG_ERROR( "emmc-block: read failed at offset %lu\n",
				   (unsigned long)done );
			return -1;
		}

		for( i = 0U; i < EMMC_BLOCK_CHUNK; i++ ) {
			if( emmc_block_buf[i] != emmc_pattern( done + i ) ) {
				LOG_ERROR( "emmc-block: mismatch at device offset %lu "
					   "(got 0x%02x want 0x%02x)\n",
					   (unsigned long)( done + i ),
					   (unsigned)emmc_block_buf[i],
					   (unsigned)emmc_pattern( done + i ) );
				return -2;
			}
		}

		hash = emmc_fnv1a( emmc_block_buf, EMMC_BLOCK_CHUNK, hash );
	}

	*hash_out = hash;
	*ms_out = ( emmc_now_ms() - start_ms ) + 1U;

	return 0;
}

/*
 * The boundary transfer: write and read back one small buffer whose transfer
 * straddles a 512 Kbyte boundary. The buffer's own address decides where the
 * transfer starts, because the engine's stop is a fact about the address it was
 * given - the whole point of the check.
 */
static int emmc_block_boundary( uint64_t lba, uint32_t *events_out )
{
	uintptr_t base = (uintptr_t)emmc_boundary_buf;
	uintptr_t boundary = ( ( base / EMMC_SDMA_BOUNDARY ) + 1U ) * EMMC_SDMA_BOUNDARY;
	uintptr_t start = boundary - EMMC_BOUNDARY_PRE;
	uint32_t i;
	struct dwc_mshc_census before;
	struct dwc_mshc_census after;

	/* The window is sized so this always holds; assert it rather than trust it. */
	if( start < base || ( start + EMMC_BOUNDARY_LEN ) > ( base + sizeof( emmc_boundary_buf ) ) ) {
		LOG_ERROR( "emmc-block: the boundary buffer cannot straddle (base=0x%lx)\n",
			   (unsigned long)base );
		return -1;
	}

	for( i = 0U; i < EMMC_BOUNDARY_LEN; i++ ) {
		emmc_boundary_buf[( start - base ) + i] = emmc_pattern( i );
	}

	dwc_mshc_census_get( &before );

	if( blkdev_write( 0U, lba, (const void *)start, EMMC_BOUNDARY_LEN ) != BLKDEV_OK ) {
		LOG_ERROR( "emmc-block: the boundary write failed\n" );
		return -1;
	}

	(void)memset( (void *)start, 0, EMMC_BOUNDARY_LEN );

	if( blkdev_read( 0U, lba, (void *)start, EMMC_BOUNDARY_LEN ) != BLKDEV_OK ) {
		LOG_ERROR( "emmc-block: the boundary read failed\n" );
		return -1;
	}

	for( i = 0U; i < EMMC_BOUNDARY_LEN; i++ ) {
		if( emmc_boundary_buf[( start - base ) + i] != emmc_pattern( i ) ) {
			LOG_ERROR( "emmc-block: boundary mismatch at offset %u\n", (unsigned)i );
			return -1;
		}
	}

	dwc_mshc_census_get( &after );
	*events_out = after.boundary_events - before.boundary_events;

	if( *events_out == 0U ) {
		LOG_ERROR( "emmc-block: no SDMA boundary event was recorded\n" );
		return -1;
	}

	return 0;
}

void emmc_block_chain( void )
{
	struct sdmmc_mmc_info info;
	struct dwc_mshc_census census;
	uint64_t lba;
	uint32_t ms;
	uint32_t hash;
	uint32_t events;
	int ret;

	if( emmc_bringup() != 0 ) {
		board_case_fail( "the card did not come up" );
		return;
	}

	if( sdmmc_mmc_info_get( &info ) != 0 || info.blocks <= EMMC_BLOCK_TAIL_BLOCKS ) {
		board_case_fail( "the card is too small for the round trip's window" );
		return;
	}

	/* The window: the last EMMC_BLOCK_BYTES of the card, aligned to the window
	 * itself so the same blocks are used on every boot. */
	lba = ( ( (uint64_t)info.blocks - EMMC_BLOCK_TAIL_BLOCKS ) / ( EMMC_BLOCK_BYTES / 512U ) ) *
	      ( EMMC_BLOCK_BYTES / 512U );

	if( emmc_block_write( lba, &ms ) != 0 ) {
		board_case_fail( "the block write failed" );
		return;
	}
	LOG_ANCHOR( "emmc-block: write bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)EMMC_BLOCK_BYTES, (unsigned long)ms,
		    (unsigned long)emmc_kib( EMMC_BLOCK_BYTES, ms ) );

	ret = emmc_block_read_verify( lba, &hash, &ms );
	if( ret == -1 ) {
		board_case_fail( "the block read failed" );
		return;
	}
	if( ret == -2 ) {
		board_case_fail( "the block round trip did not match" );
		return;
	}
	LOG_ANCHOR( "emmc-block: read bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)EMMC_BLOCK_BYTES, (unsigned long)ms,
		    (unsigned long)emmc_kib( EMMC_BLOCK_BYTES, ms ) );
	LOG_ANCHOR( "emmc-block: verify ok checksum=%08lx\n", (unsigned long)hash );

	dwc_mshc_census_get( &census );
	LOG_ANCHOR( "emmc-block: cmd23 issued=%lu\n", (unsigned long)census.cmd23 );

	if( census.cmd23 == 0U ) {
		board_case_fail( "no block-count command was issued for the multi-block transfers" );
		return;
	}

	if( emmc_block_boundary( lba, &events ) != 0 ) {
		board_case_fail( "the boundary-crossing transfer did not complete" );
		return;
	}
	LOG_ANCHOR( "emmc-block: boundary-crossed lba=%lu len=%lu events=%lu\n",
		    (unsigned long)lba, (unsigned long)EMMC_BOUNDARY_LEN,
		    (unsigned long)events );
}

BOARD_TEST( "emmc-block", emmc_block_chain );
