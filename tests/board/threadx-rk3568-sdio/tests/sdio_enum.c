/**
 * @file    sdio_enum.c
 * @brief   Case sdio-enum: the card is enumerated and its identity is read, and
 *          the function's registers answer through the seam.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h); the
 *          body drives the card through the SDIO seam only
 *          (components/linuxkpi/sdio/lkpi_sdio_backend.h), so what it exercises
 *          on the board is the same interface the driver layer will use.
 *
 * The chain, in order: install the backend over this port's card service, bring
 * the card up (power, the identification clock, CMD0/CMD5/CMD3/CMD7, CCCR and
 * CIS, the bus-width switch, the timing switch), print what the card turned out
 * to be, read the function's CIS pointer and the first bytes of its CIS, set the
 * block size and read the function register back, write a second block size and
 * restore the first, and enable the function through the CCCR.
 *
 * Anchors (the judge checks each on PASS):
 *   sdio: card <vid>:<pid>, <n> IO func(s), CCCR v<a>.<b>, bus <k> kHz (<mode>)
 *   sdio: func1 block size set=512 read=512
 *   sdio: func1 fbr write 256 read=256 restore 512 read=512
 *   sdio: SDIO READY
 */

#include "board_suite.h"
#include "sdio_cases.h"

#include "lkpi_sdio_backend.h"

#include "log.h"

/* The card registers this case reads and writes, by their SDIO addresses. */
#define SDIO_CCCR_IO_ENABLE	0x02U	/* IOEx, one bit per function */
#define SDIO_FBR_BLOCK_SIZE	0x10U	/* the function's block size, two bytes */
#define SDIO_FBR_CIS_POINTER	0x09U	/* the function's CIS pointer, three bytes */

#define SDIO_ENUM_BLOCK_SIZE	512U
#define SDIO_ENUM_OTHER_SIZE	256U

static int read_block_size( unsigned int func, unsigned int *out )
{
	uint8_t low = 0U;
	uint8_t high = 0U;
	int st;

	st = lkpi_sdio_be_io_direct( func, SDIO_FBR_BLOCK_SIZE, false, &low );
	if( st != 0 ) {
		return st;
	}
	st = lkpi_sdio_be_io_direct( func, SDIO_FBR_BLOCK_SIZE + 1U, false, &high );
	if( st != 0 ) {
		return st;
	}

	*out = (unsigned int)low | ((unsigned int)high << 8);

	return 0;
}

void sdio_enum_chain( void )
{
	struct lkpi_sdio_be_card_info info;
	uint8_t pointer[3];
	uint8_t cis[8];
	uint8_t cccr = 0U;
	unsigned int size = 0U;
	uint32_t cis_addr;
	int st;

	lkpi_sdio_fsl_install();

	st = lkpi_sdio_be_card_init( &info );
	if( st != 0 ) {
		LOG_ERROR( "sdio: the card did not come up (st=%d)\n", st );
		board_case_fail( "card bring-up failed - see the lines above" );
		return;
	}

	/*
	 * The identity line. The version pair is printed from the CCCR/SDIO
	 * revision byte as it stands: the register carries the SDIO revision in its
	 * high nibble and the CCCR revision in its low one, and the anchor keeps one
	 * fixed form so two rounds on the same card are comparable. Both revisions
	 * decoded follow on the informational line.
	 */
	LOG_ANCHOR( "sdio: card %x:%x, %u IO func(s), CCCR v%u.%u, bus %u kHz (%s)\n",
		    info.vendor, info.device, info.func_count,
		    (unsigned)( info.cccr_version >> 4 ),
		    (unsigned)( info.cccr_version & 0x0fU ),
		    (unsigned)( info.clock_hz / 1000U ),
		    info.high_speed ? "HS" : "DS" );
	LOG_INFO( "sdio: identity sdio_rev=%u cccr_rev=%u ocr=%#x width=%u "
		  "fn0_max_block=%u memory=%u multi_block=%u\n",
		  info.sdio_version, info.cccr_version, info.ocr, info.bus_width,
		  info.fn0_max_block, info.memory_present, info.multi_block );

	/* The function's CIS pointer, then the first bytes of the CIS it points at:
	 * the descriptors are read before the function is configured. */
	for( unsigned int i = 0U; i < sizeof( pointer ); i++ ) {
		st = lkpi_sdio_be_io_direct( 1U, SDIO_FBR_CIS_POINTER + i, false,
					     &pointer[i] );
		if( st != 0 ) {
			board_case_fail( "reading the function's CIS pointer failed" );
			return;
		}
	}
	cis_addr = (uint32_t)pointer[0] | ((uint32_t)pointer[1] << 8) |
		   ((uint32_t)pointer[2] << 16);

	st = lkpi_sdio_be_io_extended( 1U, cis_addr, cis, sizeof( cis ),
				       LKPI_SDIO_BE_F_INCR );
	if( st != 0 ) {
		board_case_fail( "reading the function's CIS failed" );
		return;
	}
	LOG_INFO( "sdio: func1 cis@%#x bytes %02x %02x %02x\n", cis_addr, cis[0],
		  cis[1], cis[2] );

	/* Set the block size, then read the function register back. */
	st = lkpi_sdio_be_set_block_size( 1U, SDIO_ENUM_BLOCK_SIZE );
	if( st != 0 ) {
		board_case_fail( "setting the function's block size failed" );
		return;
	}
	st = read_block_size( 1U, &size );
	if( st != 0 ) {
		board_case_fail( "reading the function's block size failed" );
		return;
	}
	LOG_ANCHOR( "sdio: func1 block size set=%u read=%u\n",
		    (unsigned)SDIO_ENUM_BLOCK_SIZE, size );
	if( size != SDIO_ENUM_BLOCK_SIZE ) {
		board_case_fail( "the block size read back is not the one that was set" );
		return;
	}

	/* A second value, read back, then the first one restored: the register
	 * takes a write and the write takes effect. */
	st = lkpi_sdio_be_set_block_size( 1U, SDIO_ENUM_OTHER_SIZE );
	if( st != 0 ) {
		board_case_fail( "setting the alternate block size failed" );
		return;
	}
	unsigned int other = 0U;
	st = read_block_size( 1U, &other );
	if( st != 0 ) {
		board_case_fail( "reading the alternate block size failed" );
		return;
	}

	st = lkpi_sdio_be_set_block_size( 1U, SDIO_ENUM_BLOCK_SIZE );
	if( st != 0 ) {
		board_case_fail( "restoring the block size failed" );
		return;
	}
	st = read_block_size( 1U, &size );
	if( st != 0 ) {
		board_case_fail( "reading the restored block size failed" );
		return;
	}
	LOG_ANCHOR( "sdio: func1 fbr write %u read=%u restore %u read=%u\n",
		    (unsigned)SDIO_ENUM_OTHER_SIZE, other,
		    (unsigned)SDIO_ENUM_BLOCK_SIZE, size );
	if( ( other != SDIO_ENUM_OTHER_SIZE ) || ( size != SDIO_ENUM_BLOCK_SIZE ) ) {
		board_case_fail( "the block-size register did not take the written values" );
		return;
	}

	/* Enable the function through the card's common control register. */
	st = lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_IO_ENABLE, false, &cccr );
	if( st != 0 ) {
		board_case_fail( "reading the IO enable register failed" );
		return;
	}
	cccr |= 0x02U; /* function 1 */
	st = lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_IO_ENABLE, true, &cccr );
	if( st != 0 ) {
		board_case_fail( "writing the IO enable register failed" );
		return;
	}
	st = lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_IO_ENABLE, false, &cccr );
	if( st != 0 ) {
		board_case_fail( "reading the IO enable register back failed" );
		return;
	}
	LOG_INFO( "sdio: func1 io enable cccr=%#04x\n", cccr );
	if( ( cccr & 0x02U ) == 0U ) {
		board_case_fail( "the function's enable bit did not stick" );
		return;
	}

	LOG_ANCHOR( "sdio: SDIO READY\n" );
}

BOARD_TEST( "sdio-enum", sdio_enum_chain );
