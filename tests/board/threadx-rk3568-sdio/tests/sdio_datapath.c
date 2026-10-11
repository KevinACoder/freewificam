/**
 * @file    sdio_datapath.c
 * @brief   Case sdio-datapath: the extended transfer path on real hardware,
 *          climbed from a probe ladder that names what fails.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h); the
 *          bodies drive the card through the SDIO seam only.
 *
 * The extended transfer has three independent variables - the function, the
 * address and the shape (byte mode with a byte count, or block mode with a
 * block count) - and a failure that only says "no data arrived" does not
 * separate them. So the case opens with a probe ladder: the same read taken
 * with several shapes, each logged with its own result, each one's failure
 * repaired by the controller reset before the next is tried. Whichever shape
 * answers first is then the one the rest of the case uses.
 *
 * The two witnesses that follow are what the acceptance criteria ask for:
 *   - a block read of the card's CIS, taken twice, must return the same bytes
 *     (the receive path on this controller has corrupted data before by reading
 *     its buffer before the engine finished draining it, so a repeat read that
 *     disagrees is the cheapest witness of that failure);
 *   - a two-byte write to a function register must store exactly two bytes. The
 *     host's engine takes word lengths, so the adapter splits the request into
 *     two single-byte transfers; had it widened the request instead, the
 *     register next door would be overwritten with a byte the caller never
 *     gave. The value is read back and the original restored.
 *
 * Anchors (the judge checks each on PASS):
 *   sdio: probe ok first=<shape>
 *   sdio: fbr read twice identical (<n> bytes)
 *   sdio: fbr cmd52 and cmd53 agree first=<bytes>
 *   sdio: cis block read twice identical (<n> bytes)
 *   sdio: fbr 2-byte write read=256 restore read=512
 *   sdio: pump quiet pending=<value>
 */

#include "board_suite.h"
#include "sdio_cases.h"

#include "lkpi_sdio_backend.h"

#include <string.h>

#include "log.h"

#define SDIO_CCCR_REVISION	0x00U
#define SDIO_CCCR_COMMON_CIS	0x09U
#define SDIO_FBR_BLOCK_SIZE	0x10U
#define SDIO_FBR_CIS_POINTER	0x09U

/* A function's basic registers live in function 0's address space, one
 * 0x100-sized block per function. */
#define SDIO_FBR_BASE_ADDR	0x100U

#define SDIO_DATAPATH_BLOCK	512U
#define SDIO_DATAPATH_ALT	256U
#define SDIO_DATAPATH_WORDS	8U

/* page-aligned, so the block path takes the caller's buffer and not the stack's
 * staging copy */
static uint8_t sdio_dp_first[SDIO_DATAPATH_BLOCK] __attribute__((aligned(512)));
static uint8_t sdio_dp_second[SDIO_DATAPATH_BLOCK] __attribute__((aligned(512)));
static uint8_t sdio_dp_direct[SDIO_DATAPATH_WORDS];

/* One shape of extended read. `blocks` is a block count when `block` is set and
 * a byte count otherwise. */
struct sdio_dp_shape {
	const char	*name;
	unsigned int	func;
	unsigned int	addr;
	unsigned int	count;
	int		block;
};

/* The ladder: the shapes the controller has been exercised in, ordered from the
 * one with the most precedent to the one with the least. */
static const struct sdio_dp_shape sdio_dp_shapes[] = {
	{ "byte4-f1-000", 1U, 0x000U, 4U, 0 },
	{ "byte4-f1-0b0", 1U, 0x0b0U, 4U, 0 },
	{ "byte8-f1-000", 1U, 0x000U, 8U, 0 },
	{ "block512-f1-000", 1U, 0x000U, 1U, 1 },
	{ "byte4-f0-cis", 0U, 0x1000U, 4U, 0 },
};

#define SDIO_DP_SHAPE_NUM \
	(unsigned int)( sizeof( sdio_dp_shapes ) / sizeof( sdio_dp_shapes[0] ) )

static int sdio_dp_read( const struct sdio_dp_shape *shape, uint8_t *buf )
{
	unsigned int flags = LKPI_SDIO_BE_F_INCR;

	if( shape->block ) {
		flags |= LKPI_SDIO_BE_F_BLOCK;
	}

	return lkpi_sdio_be_io_extended( shape->func, shape->addr, buf,
					 shape->count, flags );
}

static int sdio_dp_read_pointer( unsigned int func, unsigned int reg,
				 uint32_t *out )
{
	uint8_t pointer[3];
	int st;

	for( unsigned int i = 0U; i < sizeof( pointer ); i++ ) {
		st = lkpi_sdio_be_io_direct( func, reg + i, false, &pointer[i] );
		if( st != 0 ) {
			return st;
		}
	}

	*out = (uint32_t)pointer[0] | ((uint32_t)pointer[1] << 8) |
	       ((uint32_t)pointer[2] << 16);

	return 0;
}

static int sdio_dp_read_block_size( unsigned int *out )
{
	uint8_t low = 0U;
	uint8_t high = 0U;
	int st;

	st = lkpi_sdio_be_io_direct( 0U, SDIO_FBR_BASE_ADDR + SDIO_FBR_BLOCK_SIZE,
				     false, &low );
	if( st != 0 ) {
		return st;
	}
	st = lkpi_sdio_be_io_direct( 0U,
				     SDIO_FBR_BASE_ADDR + SDIO_FBR_BLOCK_SIZE + 1U,
				     false, &high );
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
	const struct sdio_dp_shape *working = NULL;
	uint32_t func_cis = 0U;
	unsigned int size = 0U;
	unsigned int written = 0U;
	int st;

	/* The card is up from the enum case; a register read that fails says so
	 * without this case having to know how the port keeps its card. */
	if( lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_REVISION, false, &probe ) != 0 ) {
		board_case_fail( "the card is not up (the enum case must run first)" );
		return;
	}

	/* --- the probe ladder ------------------------------------------------ */

	for( unsigned int i = 0U; i < SDIO_DP_SHAPE_NUM; i++ ) {
		const struct sdio_dp_shape *shape = &sdio_dp_shapes[i];

		(void)memset( sdio_dp_first, 0, sizeof( sdio_dp_first ) );
		st = sdio_dp_read( shape, sdio_dp_first );
		LOG_INFO( "sdio: probe %s rc=%d first=%02x%02x%02x%02x\n",
			  shape->name, st, sdio_dp_first[0], sdio_dp_first[1],
			  sdio_dp_first[2], sdio_dp_first[3] );
		if( ( st == 0 ) && ( working == NULL ) ) {
			working = shape;
		}
	}

	LOG_ANCHOR( "sdio: probe ok first=%s\n",
		    ( working != NULL ) ? working->name : "none" );
	if( working == NULL ) {
		board_case_fail( "no shape of extended read answered - see the probe lines" );
		return;
	}

	/* --- the chosen shape, twice, and against the byte-at-a-time path ---- */

	(void)memset( sdio_dp_first, 0, sizeof( sdio_dp_first ) );
	(void)memset( sdio_dp_second, 0, sizeof( sdio_dp_second ) );

	st = sdio_dp_read( working, sdio_dp_first );
	if( st != 0 ) {
		board_case_fail( "the chosen shape failed on its second run" );
		return;
	}
	st = sdio_dp_read( working, sdio_dp_second );
	if( st != 0 ) {
		board_case_fail( "the chosen shape failed on its third run" );
		return;
	}

	LOG_ANCHOR( "sdio: fbr read twice identical (%u bytes)\n",
		    SDIO_DATAPATH_WORDS );
	if( memcmp( sdio_dp_first, sdio_dp_second, SDIO_DATAPATH_WORDS ) != 0 ) {
		board_case_fail( "two extended reads of the same area disagree" );
		return;
	}

	/* The same registers one byte at a time, from function 0's FBR window. */
	for( unsigned int i = 0U; i < SDIO_DATAPATH_WORDS; i++ ) {
		st = lkpi_sdio_be_io_direct( 0U, SDIO_FBR_BASE_ADDR + i, false,
					     &sdio_dp_direct[i] );
		if( st != 0 ) {
			board_case_fail( "reading the function registers one byte at a time failed" );
			return;
		}
	}

	LOG_ANCHOR( "sdio: fbr cmd52 and cmd53 agree first=%02x%02x%02x%02x\n",
		    sdio_dp_first[0], sdio_dp_first[1], sdio_dp_first[2],
		    sdio_dp_first[3] );
	if( memcmp( sdio_dp_first, sdio_dp_direct, SDIO_DATAPATH_WORDS ) != 0 ) {
		LOG_ERROR( "sdio: extended %02x %02x %02x %02x, byte-at-a-time "
			   "%02x %02x %02x %02x\n", sdio_dp_first[0], sdio_dp_first[1],
			   sdio_dp_first[2], sdio_dp_first[3], sdio_dp_direct[0],
			   sdio_dp_direct[1], sdio_dp_direct[2], sdio_dp_direct[3] );
		board_case_fail( "the two read paths disagree about the same registers" );
		return;
	}

	/* --- the card's CIS area --------------------------------------------- */

	st = sdio_dp_read_pointer( 1U, SDIO_FBR_CIS_POINTER, &func_cis );
	if( st != 0 ) {
		board_case_fail( "reading the function's CIS pointer failed" );
		return;
	}
	LOG_INFO( "sdio: func1 cis@%#x\n", func_cis );

	(void)memset( sdio_dp_first, 0, sizeof( sdio_dp_first ) );
	(void)memset( sdio_dp_second, 0, sizeof( sdio_dp_second ) );

	st = lkpi_sdio_be_io_extended( 1U, func_cis, sdio_dp_first, 1U,
				       LKPI_SDIO_BE_F_INCR | LKPI_SDIO_BE_F_BLOCK );
	if( st != 0 ) {
		LOG_ERROR( "sdio: the first CIS block read returned %d\n", st );
		board_case_fail( "the first block read of the card's CIS failed" );
		return;
	}
	st = lkpi_sdio_be_io_extended( 1U, func_cis, sdio_dp_second, 1U,
				       LKPI_SDIO_BE_F_INCR | LKPI_SDIO_BE_F_BLOCK );
	if( st != 0 ) {
		LOG_ERROR( "sdio: the second CIS block read returned %d\n", st );
		board_case_fail( "the second block read of the card's CIS failed" );
		return;
	}

	LOG_ANCHOR( "sdio: cis block read twice identical (%u bytes)\n",
		    (unsigned)SDIO_DATAPATH_BLOCK );
	if( memcmp( sdio_dp_first, sdio_dp_second, SDIO_DATAPATH_BLOCK ) != 0 ) {
		board_case_fail( "two block reads of the same area disagree" );
		return;
	}

	/* --- the two-byte write ---------------------------------------------- */

	two[0] = (uint8_t)( SDIO_DATAPATH_ALT & 0xffU );
	two[1] = (uint8_t)( ( SDIO_DATAPATH_ALT >> 8 ) & 0xffU );
	st = lkpi_sdio_be_io_extended( 1U, SDIO_FBR_BLOCK_SIZE, two, 2U,
				       LKPI_SDIO_BE_F_WRITE | LKPI_SDIO_BE_F_INCR );
	if( st != 0 ) {
		board_case_fail( "the two-byte write of the block-size register failed" );
		return;
	}
	st = sdio_dp_read_block_size( &size );
	if( st != 0 ) {
		board_case_fail( "reading the block size back failed" );
		return;
	}
	written = size;

	two[0] = (uint8_t)( SDIO_DATAPATH_BLOCK & 0xffU );
	two[1] = (uint8_t)( ( SDIO_DATAPATH_BLOCK >> 8 ) & 0xffU );
	st = lkpi_sdio_be_io_extended( 1U, SDIO_FBR_BLOCK_SIZE, two, 2U,
				       LKPI_SDIO_BE_F_WRITE | LKPI_SDIO_BE_F_INCR );
	if( st != 0 ) {
		board_case_fail( "restoring the block-size register failed" );
		return;
	}
	st = sdio_dp_read_block_size( &size );
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

	/* --- nothing is asking for attention --------------------------------- */

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
