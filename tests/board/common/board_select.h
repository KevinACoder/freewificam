/**
 * @file    board_select.h
 * @brief   Test-selection directive: grammar, matcher, filter.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  data-driven control - no in-tree template (registered in the
 *          suite's design notes); the matcher is a deliberate minimal glob
 *          ('*' = any run, '?' = one character) over the case-name alphabet,
 *          shared by the image, the host self-test and the judge's
 *          recomputation so one vector file covers all three.
 *
 * Directive grammar (one line, whitespace-separated, at most one of each):
 *   only=<glob>  skip=<glob>        glob := [a-z0-9-*?]{1,32}
 * Silence (window timeout) means "run everything". A malformed line is
 * ignored and the window keeps waiting - a bad directive must never silently
 * turn into "run all", and it must never turn into "run nothing" either.
 */

#ifndef BOARD_SELECT_H
#define BOARD_SELECT_H

#include <stddef.h>

#define BOARD_SELECT_PATTERN_MAX 32U

/** One parsed directive. */
struct board_select {
	char only[BOARD_SELECT_PATTERN_MAX + 1U];
	char skip[BOARD_SELECT_PATTERN_MAX + 1U];
	int have_only;
	int have_skip;
};

/** Parse one directive line (leading/trailing blanks allowed).
 *  @retval 0 accepted; -1 malformed (unknown token, bad character, overlong
 *          glob, repeated keyword, empty glob). */
int board_select_parse( const char *line, struct board_select *out );

/** Whole-name glob match: '*' = any run including empty, '?' = exactly one
 *  character, everything else literal. */
int board_glob_match( const char *pattern, const char *name );

/** Is this case selected? No `only` selects every name; `skip` removes the
 *  names it matches. */
int board_select_match( const struct board_select *sel, const char *name );

#endif /* BOARD_SELECT_H */
