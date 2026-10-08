/**
 * @file    board_suite.h
 * @brief   In-image regression test table and runner (shared board-suite face).
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  linker-section registry table - ref components/cherrysh/csh.h:283-306
 *          (a case registers itself where it is defined; the iterator never
 *          asks where entries came from) and arch/aarch64/baremetal.ld:54-73
 *          (KEEP + SUBALIGN(8): the section is walked as a flat array with a
 *          fixed stride, so padding between entries would hide every entry
 *          after the first).
 *
 * One image carries one table. Adding a case is a new entry plus a source file
 * on the image's test source list - the runner below never changes. The table
 * is data: the suite's declaration file and its judge carry the same case-name
 * list, and the build checks the three sources agree.
 */

#ifndef BOARD_SUITE_H
#define BOARD_SUITE_H

#include <stddef.h>

/** One regression case: its judgment-channel name and its body. */
struct board_test {
	const char *name;   /* [a-z0-9-]+; the name printed in the CASE line */
	void (*fn)( void ); /* body: prints its own anchors, calls board_case_fail() */
};

/** Register one case into the image's test table (linker section BOARD_TESTS). */
#define BOARD_TEST( name_, fn_ ) \
	__attribute__(( used, section( "BOARD_TESTS" ) )) \
	static const struct board_test board_test_##fn_ = { name_, fn_ }

/* Table bounds, provided by the linker script (arch/aarch64/baremetal.ld). */
extern const struct board_test __board_tests_start[];
extern const struct board_test __board_tests_end[];

/** Mark the running case failed; the runner prints the CASE line as FAIL and
 *  the reason as a note line. */
void board_case_fail( const char *reason );

/** The runner thread entry: select window, start line, table walk, sentinel,
 *  then park (the shell and the tick keep running). */
void board_suite_run( void *argument );

#endif /* BOARD_SUITE_H */
