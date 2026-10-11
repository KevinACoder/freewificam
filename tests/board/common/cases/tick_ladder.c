/**
 * @file    tick_ladder.c
 * @brief   Case tick-ladder: the 1 kHz tick advances on whole seconds.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case body of the regression table (tests/board/common/board_suite.h);
 *          the ladder format is the bring-up smoke's (tests/board/
 *          threadx-rk3568-smoke/main.c:66-73) and the first-beat alignment is
 *          the fix found on the board there (sleep to the clock's next whole
 *          second, so the three beats read t=1000/2000/3000 rather than
 *          t=1001/2002/3003).
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#define LADDER_BEATS 3U
#define LADDER_STEP  1000U
/* A 1000-tick sleep returns on the tick after the interval elapsed, so the
 * measured delta is 1000..1002 ticks (the bring-up smoke measured 1001 on this
 * board: t=1000/2001/3002). The ladder fails only when the tick stops
 * advancing at ~1 kHz. */
#define LADDER_SLACK 2U

static void tick_ladder( void )
{
	uint32_t prev = 0;

	/* Align the first beat to the clock's next whole second. */
	osDelay( 1000U - ( osKernelGetTickCount() % 1000U ) );

	for( unsigned i = 0; i < LADDER_BEATS; i++ ) {
		uint32_t now;
		uint32_t delta;

		if( i != 0U ) {
			osDelay( LADDER_STEP );
		}
		now = osKernelGetTickCount();
		LOG_INFO( "tick: t=%lu\n", ( unsigned long )now );
		if( i == 0U ) {
			prev = now;
			continue;
		}
		delta = now - prev;
		if( delta < LADDER_STEP || delta > LADDER_STEP + LADDER_SLACK ) {
			board_case_fail( "ladder delta outside 1000..1002 ticks" );
			return;
		}
		prev = now;
	}
}

BOARD_TEST( "tick-ladder", tick_ladder );
