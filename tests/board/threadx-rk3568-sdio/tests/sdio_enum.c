/**
 * @file    sdio_enum.c
 * @brief   Case sdio-enum: the card is enumerated and its identity is read, its
 *          descriptors are read, and the function is enabled and configured.
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
 * to be, read the card's common CIS pointer and the first bytes it points at,
 * enable the function, set the function's block size and read the register back,
 * write a second block size and restore the first.
 *
 * The order carries one decision worth naming: the CIS is read one byte at a
 * time, which is how a card's CIS is meant to be read, and the function is
 * enabled BEFORE anything addresses it with an extended transfer. A function
 * that has not been enabled does not answer one, and the card here answers a
 * refused transfer by leaving the host's data path armed - the failure then
 * looks like a broken data path rather than a disabled function.
 *
 * Anchors (the judge checks each on PASS):
 *   sdio: card <vid>:<pid>, <n> IO func(s), CCCR v<a>.<b>, bus <k> kHz (<mode>)
 *   sdio: func1 enabled ioex=<value>
 *   sdio: func1 ready iorx=<value> polls=<n>
 *   sdio: func1 block size set=512 read=512
 *   sdio: func1 fbr write 256 read=256 restore 512 read=512
 *   sdio: SDIO READY
 */

#include "board_suite.h"
#include "sdio_cases.h"

#include "lkpi_sdio_backend.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

/* The card registers this case reads and writes, by their SDIO addresses. */
#define SDIO_CCCR_IO_ENABLE	0x02U	/* IOEx, one bit per function */
#define SDIO_CCCR_IO_READY	0x03U	/* IORx, one bit per function */
#define SDIO_CCCR_BUS_IFACE	0x07U	/* bus interface: the width in use */
#define SDIO_CCCR_COMMON_CIS	0x09U	/* the common CIS pointer, three bytes */
#define SDIO_FBR_BLOCK_SIZE	0x10U	/* the function's block size, two bytes */

#define SDIO_ENUM_BLOCK_SIZE	512U
#define SDIO_ENUM_OTHER_SIZE	256U
#define SDIO_ENUM_CIS_BYTES	8U

/* How long the function may take to report itself ready after it is enabled.
 * The card's own ceiling is in its function CIS; this is the ceiling the
 * specification's default implies, and the poll is bounded so a card that never
 * answers is reported rather than waited on forever. */
#define SDIO_ENUM_READY_POLLS	50U
#define SDIO_ENUM_READY_GAP_MS	10U

/* A function's basic registers live in function 0's address space, at one
 * 0x100-sized block per function. Reading a function's block-size register is
 * therefore a function-0 read of 0x100 * func + 0x10, which is also where the
 * imported stack writes it. */
#define SDIO_FBR_WINDOW(func)	((unsigned int)(func) * 0x100U)

static int read_block_size( unsigned int func, unsigned int *out )
{
	unsigned int base = SDIO_FBR_WINDOW( func ) + SDIO_FBR_BLOCK_SIZE;
	uint8_t low = 0U;
	uint8_t high = 0U;
	int st;

	st = lkpi_sdio_be_io_direct( 0U, base, false, &low );
	if( st != 0 ) {
		return st;
	}
	st = lkpi_sdio_be_io_direct( 0U, base + 1U, false, &high );
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
	uint8_t cis[SDIO_ENUM_CIS_BYTES];
	uint8_t cccr = 0U;
	uint8_t bus_iface = 0U;
	unsigned int size = 0U;
	unsigned int other = 0U;
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

	/* The bus interface register is the width the card believes it is on, and
	 * the host's own record is the width it programmed: the two must agree, and
	 * a mismatch here is exactly what a data phase that never starts looks
	 * like from the outside. */
	(void)lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_BUS_IFACE, false, &bus_iface );
	LOG_INFO( "sdio: identity sdio_rev=%u cccr_rev=%u ocr=%#x width=%u "
		  "card_bus_iface=%#04x fn0_max_block=%u memory=%u multi_block=%u\n",
		  info.sdio_version, info.cccr_version, info.ocr, info.bus_width,
		  bus_iface, info.fn0_max_block, info.memory_present, info.multi_block );

	/* The card's common CIS pointer, then the first bytes it points at - read
	 * one byte at a time, which is the access the CIS is defined for. */
	for( unsigned int i = 0U; i < sizeof( pointer ); i++ ) {
		st = lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_COMMON_CIS + i, false,
					     &pointer[i] );
		if( st != 0 ) {
			board_case_fail( "reading the card's common CIS pointer failed" );
			return;
		}
	}
	cis_addr = (uint32_t)pointer[0] | ((uint32_t)pointer[1] << 8) |
		   ((uint32_t)pointer[2] << 16);
	LOG_INFO( "sdio: common cis@%#x\n", cis_addr );

	for( unsigned int i = 0U; i < sizeof( cis ); i++ ) {
		st = lkpi_sdio_be_io_direct( 0U, cis_addr + i, false, &cis[i] );
		if( st != 0 ) {
			board_case_fail( "reading the card's common CIS failed" );
			return;
		}
	}
	LOG_INFO( "sdio: cis bytes %02x %02x %02x %02x\n", cis[0], cis[1], cis[2],
		  cis[3] );

	/*
	 * Enable function 1, and then wait for it to say it is ready.
	 *
	 * The enable bit and the ready bit are two different things: a card may
	 * accept the enable and take a while before its function can service
	 * transfers, and while it cannot, the common registers keep answering
	 * (they are function 0's) so a host that only reads those never notices.
	 * Everything that addresses the function with an extended transfer depends
	 * on this having completed, so it comes before the block-size work below
	 * and before the next case's transfers.
	 */
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
	LOG_ANCHOR( "sdio: func1 enabled ioex=%#04x\n", cccr );
	if( ( cccr & 0x02U ) == 0U ) {
		board_case_fail( "the function's enable bit did not stick" );
		return;
	}

	uint8_t ready = 0U;
	unsigned int waited = 0U;

	while( waited < SDIO_ENUM_READY_POLLS ) {
		st = lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_IO_READY, false, &ready );
		if( st != 0 ) {
			board_case_fail( "reading the IO ready register failed" );
			return;
		}
		if( ( ready & 0x02U ) != 0U ) {
			break;
		}
		osDelay( SDIO_ENUM_READY_GAP_MS );
		waited++;
	}

	LOG_ANCHOR( "sdio: func1 ready iorx=%#04x polls=%u\n", ready, waited );
	if( ( ready & 0x02U ) == 0U ) {
		board_case_fail( "the function never reported itself ready" );
		return;
	}

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

	LOG_ANCHOR( "sdio: SDIO READY\n" );
}

BOARD_TEST( "sdio-enum", sdio_enum_chain );
