/**
 * @file    sdio_engine.c
 * @brief   Case sdio-engine: the same card access through the SDIO engine's own
 *          interface, checked against the seam path the other cases use.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h); the
 *          body drives the card through the engine
 *          (components/linuxkpi/sdio/linuxkpi_sdio.c), whose Linux-semantics
 *          interface (linux/mmc/sdio_func.h) is what a driver will call, and
 *          compares each result with the same access taken over the seam
 *          directly - the two must agree byte for byte.
 *
 * Why this case exists: the other cases reach the card through the seam, which
 * is the engine's own backend. This one closes the loop - it proves the engine
 * over the seam produces the same bytes on the real card, which is the step the
 * seam-level evidence cannot make.
 *
 * It runs after sdio-card-enable on purpose: the part answers single-byte
 * transfers from enumeration but refuses extended ones until its own power
 * sequence has run, and this case takes an extended read.
 *
 * Anchors (the judge checks each on PASS):
 *   sdio: engine up funcs=<n> cccr=0x<vv> bus=<k> kHz
 *   sdio: engine readb addr=0x<aa> seam=0x<vv> engine=0x<vv>
 *   sdio: engine ext read seam=<b0b1b2b3> engine=<b0b1b2b3>
 *   sdio: engine claim held=1
 *   sdio: engine matches the seam path
 */

#include <string.h>

#include "board_suite.h"
#include "sdio_cases.h"

#include "lkpi_sdio_backend.h"
#include "linuxkpi_sdio.h"

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

/* The function-1 window address this case reads both ways. */
#define SDIO_ENGINE_ADDR	0x00U
#define SDIO_ENGINE_BYTES	8U

void sdio_engine_chain( void )
{
	struct sdio_func *func1;
	uint8_t seam_byte = 0U;
	uint8_t engine_byte = 0U;
	uint8_t seam_buf[ SDIO_ENGINE_BYTES ];
	uint8_t engine_buf[ SDIO_ENGINE_BYTES ];
	uint8_t held = 0U;
	int err = 0;
	int st;

	if( !lkpi_sdio_be_ready() ) {
		board_case_fail( "no backend is installed (the enum case must run first)" );
		return;
	}

	/*
	 * Bring the engine up over the already-installed backend. The card service
	 * is once-guarded, so this is the same card the earlier cases brought up,
	 * now described by the engine's own host, card and function objects.
	 */
	lkpi_sdio_start();
	if( !lkpi_sdio_ready() ) {
		board_case_fail( "the engine did not bring the card up" );
		return;
	}

	func1 = lkpi_sdio_func( 1U );
	if( func1 == NULL ) {
		board_case_fail( "the engine reports no function 1" );
		return;
	}

	LOG_ANCHOR( "sdio: engine up funcs=%u cccr=0x%02x bus=%u kHz\n",
		    (unsigned)func1->card->sdio_funcs,
		    (unsigned)func1->card->major_rev * 16U + (unsigned)func1->card->minor_rev,
		    (unsigned)( func1->card->host->ios.clock / 1000U ) );

	/* the same single register, once through the seam and once through the
	 * engine's own byte call */
	st = lkpi_sdio_be_io_direct( 1U, SDIO_ENGINE_ADDR, false, &seam_byte );
	if( st != 0 ) {
		board_case_fail( "the seam could not read the function's first register" );
		return;
	}
	engine_byte = sdio_readb( func1, SDIO_ENGINE_ADDR, &err );
	if( err != 0 ) {
		board_case_fail( "the engine's byte read failed" );
		return;
	}

	LOG_ANCHOR( "sdio: engine readb addr=0x%02x seam=0x%02x engine=0x%02x\n",
		    (unsigned)SDIO_ENGINE_ADDR, (unsigned)seam_byte,
		    (unsigned)engine_byte );
	if( seam_byte != engine_byte ) {
		board_case_fail( "the engine's byte read disagrees with the seam" );
		return;
	}

	/* the same extended read: the engine splits and addresses the transfer
	 * itself, and the bytes must be the seam's */
	(void)memset( seam_buf, 0, sizeof( seam_buf ) );
	(void)memset( engine_buf, 0, sizeof( engine_buf ) );

	st = lkpi_sdio_be_io_extended( 1U, SDIO_ENGINE_ADDR, seam_buf,
				       SDIO_ENGINE_BYTES, LKPI_SDIO_BE_F_INCR );
	if( st != 0 ) {
		board_case_fail( "the seam could not read the function's first bytes" );
		return;
	}
	st = sdio_memcpy_fromio( func1, engine_buf, SDIO_ENGINE_ADDR,
				 (int)SDIO_ENGINE_BYTES );
	if( st != 0 ) {
		board_case_fail( "the engine's extended read failed" );
		return;
	}

	LOG_ANCHOR( "sdio: engine ext read seam=%02x%02x%02x%02x "
		    "engine=%02x%02x%02x%02x\n", seam_buf[0], seam_buf[1],
		    seam_buf[2], seam_buf[3], engine_buf[0], engine_buf[1],
		    engine_buf[2], engine_buf[3] );
	if( memcmp( seam_buf, engine_buf, SDIO_ENGINE_BYTES ) != 0 ) {
		board_case_fail( "the engine's extended read disagrees with the seam" );
		return;
	}

	/* the bus claim: taken by this thread and reported as held by it */
	sdio_claim_host( func1 );
	held = lkpi_sdio_claim_held_by_current() ? 1U : 0U;
	sdio_release_host( func1 );

	LOG_ANCHOR( "sdio: engine claim held=%u\n", (unsigned)held );
	if( held != 1U ) {
		board_case_fail( "the engine did not report the claim as held by its taker" );
		return;
	}

	LOG_ANCHOR( "sdio: engine matches the seam path\n" );
}

BOARD_TEST( "sdio-engine", sdio_engine_chain );
