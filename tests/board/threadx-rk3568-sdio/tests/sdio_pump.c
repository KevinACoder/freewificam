/**
 * @file    sdio_pump.c
 * @brief   Case sdio-pump: the card's interrupt line stays quiet across a
 *          polling window, and the card is still answering afterwards.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h);
 *          the body drives the card through the SDIO seam only.
 *
 * This port brings the card up without a card interrupt: the socket's interrupt
 * line is not wired, and the design polls instead. What the case witnesses is
 * the other half of that decision - that polling a quiet card is genuinely
 * quiet. Sixteen polls spread over the window must all report an empty pending
 * word and none may error, and the card must still answer a register read at
 * the end, so "no interrupt" is not the same thing as "the card stopped
 * talking".
 *
 * Anchors (the judge checks each on PASS):
 *   sdio: pump 16 polls quiet
 *   sdio: pump done, cccr=%#04x
 */

#include "board_suite.h"
#include "sdio_cases.h"

#include "lkpi_sdio_backend.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#define SDIO_CCCR_REVISION	0x00U
#define SDIO_PUMP_POLLS		16U
#define SDIO_PUMP_GAP_MS	10U

void sdio_pump_chain( void )
{
	unsigned int noisy = 0U;
	unsigned int errors = 0U;
	uint8_t cccr = 0U;
	uint8_t probe = 0U;
	int st;

	/* The card is up from the enum case; a register read that fails says so
	 * without this case having to know how the port keeps its card. */
	if( lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_REVISION, false, &probe ) != 0 ) {
		board_case_fail( "the card is not up (the enum case must run first)" );
		return;
	}

	for( unsigned int i = 0U; i < SDIO_PUMP_POLLS; i++ ) {
		uint8_t pending = 0xffU;

		st = lkpi_sdio_be_irq_pending( &pending );
		if( st != 0 ) {
			errors++;
		} else if( pending != 0U ) {
			noisy++;
		}
		osDelay( SDIO_PUMP_GAP_MS );
	}

	LOG_ANCHOR( "sdio: pump %u polls quiet\n", (unsigned)SDIO_PUMP_POLLS );
	if( ( noisy != 0U ) || ( errors != 0U ) ) {
		LOG_ERROR( "sdio: %u polls reported a pending interrupt, %u errored\n",
			   noisy, errors );
		board_case_fail( "the quiet line was not quiet" );
		return;
	}

	/* The card still answers: an empty interrupt word must not be read as a
	 * card that has stopped responding. */
	st = lkpi_sdio_be_io_direct( 0U, SDIO_CCCR_REVISION, false, &cccr );
	if( st != 0 ) {
		board_case_fail( "the card stopped answering after the polling window" );
		return;
	}
	LOG_ANCHOR( "sdio: pump done, cccr=%#04x\n", cccr );
	if( cccr == 0U || cccr == 0xffU ) {
		board_case_fail( "the card's revision register reads as no answer" );
		return;
	}
}

BOARD_TEST( "sdio-pump", sdio_pump_chain );
