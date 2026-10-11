/**
 * @file    sdio_datapath.c
 * @brief   Case sdio-datapath: the extended transfer path on real hardware - a
 *          block read of the card's CIS taken twice, and a two-byte write that
 *          the adapter has to split.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h); the
 *          bodies drive the card through the SDIO seam only.
 *
 * Two things are being witnessed here, and both are about the layer between the
 * caller and the host rather than about the card:
 *
 *   - a block read of the same card area twice must return the same bytes. The
 *     receive path on this controller has already corrupted data once by
 *     reading its buffer before the engine finished draining it, so a repeat
 *     read that disagrees is the cheapest possible witness of that failure.
 *   - a two-byte write to a function register must store exactly two bytes. The
 *     host's engine takes word lengths, so the adapter splits the request into
 *     two single-byte transfers; if it ever widened the request instead, the
 *     register next door would be overwritten with a byte the caller never
 *     gave. The case writes, reads back, and restores, so the card is left as
 *     it was found.
 *
 * Anchors (the judge checks each on PASS):
 *   sdio: cis block read twice identical (512 bytes)
 *   sdio: fbr 2-byte write read=256 restore read=512
 *   sdio: pump quiet pending=0x00
 */

#include "board_suite.h"
#include "sdio_cases.h"

#include "lkpi_sdio_backend.h"

#include <string.h>

#include "log.h"

#define SDIO_FBR_BLOCK_SIZE	0x10U
#define SDIO_FBR_CIS_POINTER	0x09U
#define SDIO_CCCR_REVISION	0x00U

#define SDIO_DATAPATH_BLOCK	512U
#define SDIO_DATAPATH_ALT	256U

/* The two block buffers: page-aligned, so the block path takes the caller's
 * buffer and not the stack's staging copy. */
static uint8_t sdio_datapath_first[SDIO_DATAPATH_BLOCK] __attribute__((aligned(512)));
static uint8_t sdio_datapath_second[SDIO_DATAPATH_BLOCK] __attribute__((aligned(512)));

static int sdio_cis_address( uint32_t *out )
{
	uint8_t pointer[3];
	int st;

	for( unsigned int i = 0U; i < sizeof( pointer ); i++ ) {
		st = lkpi_sdio_be_io_direct( 1U, SDIO_FBR_CIS_POINTER + i, false,
					     &pointer[i] );
		if( st != 0 ) {
			return st;
		}
	}

	*out = (uint32_t)pointer[0] | ((uint32_t)pointer[1] << 8) |
	       ((uint32_t)pointer[2] << 16);

	return 0;
}

static int sdio_read_block_size( unsigned int *out )
{
	uint8_t low = 0U;
	uint8_t high = 0U;
	int st;

	st = lkpi_sdio_be_io_direct( 1U, SDIO_FBR_BLOCK_SIZE, false, &low );
	if( st != 0 ) {
		return st;
	}
	st = lkpi_sdio_be_io_direct( 1U, SDIO_FBR_BLOCK_SIZE + 1U, false, &high );
	if( st != 0 ) {
		return st;
	}

	*out = (unsigned int)low | ((unsigned int)high << 8);

	return 0;
}

void sdio_datapath_chain( void )
{
	uint8_t two[2];
	uint8_t pending = 0xffU;
	uint8_t probe = 0U;
	uint32_t cis = 0U;
	unsigned int size = 0U;
	int st;

	/* The card is up from the enum case; a register read that fails says so
	 * without this case having to know how the port keeps its card. */
	if( lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_REVISION, false, &probe ) != 0 ) {
		board_case_fail( "the card is not up (the enum case must run first)" );
		return;
	}

	st = sdio_cis_address( &cis );
	if( st != 0 ) {
		board_case_fail( "reading the function's CIS pointer failed" );
		return;
	}

	/* The same block, twice, through the block-mode path. */
	(void)memset( sdio_datapath_first, 0, sizeof( sdio_datapath_first ) );
	(void)memset( sdio_datapath_second, 0, sizeof( sdio_datapath_second ) );

	st = lkpi_sdio_be_io_extended( 1U, cis, sdio_datapath_first, 1U,
				       LKPI_SDIO_BE_F_INCR | LKPI_SDIO_BE_F_BLOCK );
	if( st != 0 ) {
		LOG_ERROR( "sdio: the first block read returned %d\n", st );
		board_case_fail( "the first block read of the card's CIS failed" );
		return;
	}
	st = lkpi_sdio_be_io_extended( 1U, cis, sdio_datapath_second, 1U,
				       LKPI_SDIO_BE_F_INCR | LKPI_SDIO_BE_F_BLOCK );
	if( st != 0 ) {
		LOG_ERROR( "sdio: the second block read returned %d\n", st );
		board_case_fail( "the second block read of the card's CIS failed" );
		return;
	}

	LOG_ANCHOR( "sdio: cis block read twice identical (%u bytes)\n",
		    (unsigned)SDIO_DATAPATH_BLOCK );
	if( memcmp( sdio_datapath_first, sdio_datapath_second,
		    SDIO_DATAPATH_BLOCK ) != 0 ) {
		board_case_fail( "two block reads of the same area disagree" );
		return;
	}

	/* The two-byte write: through the seam's extended path, so the adapter's
	 * split is what the card sees. */
	two[0] = (uint8_t)( SDIO_DATAPATH_ALT & 0xffU );
	two[1] = (uint8_t)( ( SDIO_DATAPATH_ALT >> 8 ) & 0xffU );
	st = lkpi_sdio_be_io_extended( 1U, SDIO_FBR_BLOCK_SIZE, two, 2U,
				       LKPI_SDIO_BE_F_WRITE | LKPI_SDIO_BE_F_INCR );
	if( st != 0 ) {
		board_case_fail( "the two-byte write of the block-size register failed" );
		return;
	}
	st = sdio_read_block_size( &size );
	if( st != 0 ) {
		board_case_fail( "reading the block size back failed" );
		return;
	}
	unsigned int written = size;

	two[0] = (uint8_t)( SDIO_DATAPATH_BLOCK & 0xffU );
	two[1] = (uint8_t)( ( SDIO_DATAPATH_BLOCK >> 8 ) & 0xffU );
	st = lkpi_sdio_be_io_extended( 1U, SDIO_FBR_BLOCK_SIZE, two, 2U,
				       LKPI_SDIO_BE_F_WRITE | LKPI_SDIO_BE_F_INCR );
	if( st != 0 ) {
		board_case_fail( "restoring the block-size register failed" );
		return;
	}
	st = sdio_read_block_size( &size );
	if( st != 0 ) {
		board_case_fail( "reading the restored block size failed" );
		return;
	}

	LOG_ANCHOR( "sdio: fbr 2-byte write read=%u restore read=%u\n", written,
		    size );
	if( ( written != SDIO_DATAPATH_ALT ) || ( size != SDIO_DATAPATH_BLOCK ) ) {
		board_case_fail( "the two-byte write did not store exactly two bytes" );
		return;
	}

	/* Nothing is asking for attention, and the line reads clean. */
	st = lkpi_sdio_be_irq_pending( &pending );
	if( st != 0 ) {
		board_case_fail( "reading the card's pending-interrupt word failed" );
		return;
	}
	LOG_ANCHOR( "sdio: pump quiet pending=%#04x\n", pending );
	if( pending != 0U ) {
		board_case_fail( "the card reports a pending interrupt on a quiet line" );
		return;
	}
}

BOARD_TEST( "sdio-datapath", sdio_datapath_chain );
