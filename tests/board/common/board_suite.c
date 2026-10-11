/**
 * @file    board_suite.c
 * @brief   The shared regression runner: select window, table walk, sentinel.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  registry-driven batch runner - ref tools/mock/run.sh:97-211 (walk a
 *          registry, run every entry, close with one summary line); the
 *          judgment channel is the board line protocol (CASE lines plus the
 *          BOARD-SUITE sentinel), consumed by the suite's judge.
 *
 * The image defines BOARD_SUITE_NAME (the suite's directory name). Link order
 * is execution order, so the image's Makefile lists the test sources
 * explicitly and appends late-running cases (USB bring-up) at the end.
 *
 * A case that hangs costs the sentinel: the judge then reports an incomplete
 * run, and every CASE line already printed stays in the transcript for
 * localization. The runner deliberately carries no watchdog - one timeout
 * mechanism (the host runner's) is enough.
 */

#include <stdint.h>

#include "board_suite.h"
#include "board_select.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"
#include "uart.h"

#ifndef BOARD_SUITE_NAME
#error "the image must define BOARD_SUITE_NAME"
#endif

/* How long the select window stays open after the select anchor. The host
 * runner sends only=/skip= inside it; silence means "run everything", so a
 * missed directive costs time, never correctness. */
#define BOARD_SELECT_WINDOW_MS 5000U
#define BOARD_SELECT_LINE_MAX  128U
#define BOARD_LIST_MAX         384U

/* The running case's failure reason. The runner thread owns it; cases run one
 * at a time by construction. */
static const char *board_case_reason;

void board_case_fail( const char *reason )
{
	board_case_reason = reason;
}

/* Window line assembly state (single reader: the runner thread). */
static char board_win_buf[BOARD_SELECT_LINE_MAX];
static size_t board_win_len;
static int board_win_overlong;

/* Poll the console once; 1 when a complete line sits in board_win_buf.
 * Overlong lines are consumed and dropped - the window keeps waiting. */
static int board_poll_line( void )
{
	int c;

	while( uart_early_try_getc( &c ) ) {
		if( c == '\r' || c == '\n' ) {
			if( board_win_overlong ) {
				board_win_len = 0;
				board_win_overlong = 0;
				continue;
			}
			if( board_win_len == 0 ) {
				continue;
			}
			board_win_buf[board_win_len] = '\0';
			return 1;
		}
		if( board_win_len + 1U < sizeof( board_win_buf ) ) {
			board_win_buf[board_win_len++] = ( char )c;
		} else {
			board_win_overlong = 1;
		}
	}
	return 0;
}

/* The start line: the selection echoed verbatim plus the selected case names
 * in execution order. Built in one fixed stack buffer (no allocation). */
static void board_print_start( const struct board_select *sel, const char *spec,
			       unsigned cases )
{
	char list[BOARD_LIST_MAX];
	size_t off = 0;

	list[0] = '\0';
	for( const struct board_test *t = __board_tests_start; t < __board_tests_end; t++ ) {
		int n;

		if( !board_select_match( sel, t->name ) ) {
			continue;
		}
		n = snprintf( list + off, sizeof( list ) - off, "%s%s", off ? "," : "", t->name );
		if( n < 0 || ( size_t )n >= sizeof( list ) - off ) {
			break;
		}
		off += ( size_t )n;
	}
	LOG_ANCHOR( "BOARD-SUITE %s start cases=%u sel=%s list=%s\n",
		   BOARD_SUITE_NAME, cases, spec, list );
}

void board_suite_run( void *argument )
{
	struct board_select sel;
	const char *spec = "all";
	unsigned iters = BOARD_SELECT_WINDOW_MS / 10U;
	unsigned cases = 0;
	unsigned ok = 0;
	unsigned failed = 0;
	int selected = 0;

	(void)argument;

	LOG_ANCHOR( "BOARD-SUITE %s select window_ms=%u\n",
		   BOARD_SUITE_NAME, ( unsigned )BOARD_SELECT_WINDOW_MS );

	board_win_len = 0;
	board_win_overlong = 0;
	for( unsigned i = 0; i < iters && !selected; i++ ) {
		if( board_poll_line() ) {
			if( board_select_parse( board_win_buf, &sel ) == 0 ) {
				char *s = board_win_buf;

				while( *s == ' ' || *s == '\t' ) {
					s++;
				}
				spec = s;
				selected = 1;
				continue;
			}
			LOG_INFO( "BOARD-SUITE-note select ignored: %s\n", board_win_buf );
			board_win_len = 0;
		}
		osDelay( 10U );
	}
	if( !selected ) {
		sel.have_only = 0;
		sel.have_skip = 0;
		sel.only[0] = '\0';
		sel.skip[0] = '\0';
	}

	for( const struct board_test *t = __board_tests_start; t < __board_tests_end; t++ ) {
		if( board_select_match( &sel, t->name ) ) {
			cases++;
		}
	}
	board_print_start( &sel, spec, cases );
	if( cases == 0 ) {
		LOG_INFO( "BOARD-SUITE-note select empty\n" );
	}

	for( const struct board_test *t = __board_tests_start; t < __board_tests_end; t++ ) {
		if( !board_select_match( &sel, t->name ) ) {
			continue;
		}
		board_case_reason = 0;
		t->fn();
		if( board_case_reason ) {
			failed++;
			LOG_INFO( "BOARD-SUITE-note case %s: %s\n", t->name, board_case_reason );
			LOG_ANCHOR( "CASE %s/%s FAIL\n", BOARD_SUITE_NAME, t->name );
		} else {
			ok++;
			LOG_ANCHOR( "CASE %s/%s PASS\n", BOARD_SUITE_NAME, t->name );
		}
	}

	LOG_ANCHOR( "BOARD-SUITE %s result=%s cases=%u ok=%u exit=%u\n",
		   BOARD_SUITE_NAME, failed ? "fail" : "pass", cases, ok, failed );

	/* Park: the shell and the tick keep running, the transcript stays
	 * readable, and the host sees a stable image until the next reboot. */
	for( ;; ) {
		osDelay( 1000U );
	}
}
