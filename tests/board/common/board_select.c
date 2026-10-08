/**
 * @file    board_select.c
 * @brief   Selection directive parser, glob matcher and filter.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  see board_select.h - the matcher is the single implementation of
 *          the glob semantics; the host self-test compiles this file with
 *          -DBOARD_SELECT_HOSTTEST and replays select-vectors.tsv, and the
 *          suite judges replay the same vector file against their Python
 *          twin.
 */

#include <stddef.h>

#include "board_select.h"

static int board_select_gchar( char c )
{
	return ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' )
		|| c == '-' || c == '*' || c == '?';
}

int board_glob_match( const char *pattern, const char *name )
{
	const char *p = pattern;
	const char *n = name;
	const char *star = 0;
	const char *mark = 0;

	while( *n ) {
		if( *p == '*' ) {
			star = p++;
			mark = n;
			continue;
		}
		if( *p == '?' || *p == *n ) {
			p++;
			n++;
			continue;
		}
		if( star ) {
			p = star + 1;
			n = ++mark;
			continue;
		}
		return 0;
	}
	while( *p == '*' ) {
		p++;
	}
	return *p == '\0';
}

/* Copy one glob token body (after the '='), validating the alphabet and the
 * length; the caller has already checked the keyword. */
static int board_select_copy( char *dst, const char *src )
{
	size_t len = 0;

	while( src[len] ) {
		if( !board_select_gchar( src[len] ) || len >= BOARD_SELECT_PATTERN_MAX ) {
			return -1;
		}
		len++;
	}
	if( len == 0 ) {
		return -1;
	}
	for( size_t i = 0; i <= len; i++ ) {
		dst[i] = src[i];
	}
	return 0;
}

static int board_select_keyword( const char *tok, size_t len, const char *word )
{
	size_t i;

	for( i = 0; i < 5U; i++ ) {
		if( tok[i] != word[i] ) {
			return 0;
		}
	}
	return len > 5U && tok[5] == '=';
}

int board_select_parse( const char *line, struct board_select *out )
{
	const char *p = line;

	out->have_only = 0;
	out->have_skip = 0;
	out->only[0] = '\0';
	out->skip[0] = '\0';

	for( ;; ) {
		while( *p == ' ' || *p == '\t' ) {
			p++;
		}
		if( *p == '\0' ) {
			break;
		}

		const char *tok = p;
		size_t len;

		while( *p && *p != ' ' && *p != '\t' ) {
			p++;
		}
		len = ( size_t )( p - tok );

		if( board_select_keyword( tok, len, "only=" ) ) {
			if( out->have_only || board_select_copy( out->only, tok + 5 ) != 0 ) {
				return -1;
			}
			out->have_only = 1;
		} else if( board_select_keyword( tok, len, "skip=" ) ) {
			if( out->have_skip || board_select_copy( out->skip, tok + 5 ) != 0 ) {
				return -1;
			}
			out->have_skip = 1;
		} else {
			return -1;
		}
	}
	return 0;
}

int board_select_match( const struct board_select *sel, const char *name )
{
	if( sel->have_only && !board_glob_match( sel->only, name ) ) {
		return 0;
	}
	if( sel->have_skip && board_glob_match( sel->skip, name ) ) {
		return 0;
	}
	return 1;
}

#ifdef BOARD_SELECT_HOSTTEST
/* Host self-test: replay the shared vector file through the matcher. Built by
 * the regression image's `make check`; the same file feeds the judges'
 * Python twin, so one vector set pins both implementations. */
#include <stdio.h>

int main( int argc, char **argv )
{
	const char *path = argc > 1 ? argv[1] : "select-vectors.tsv";
	FILE *f = fopen( path, "r" );
	char line[256];
	unsigned checked = 0;
	unsigned failed = 0;

	if( f == 0 ) {
		fprintf( stderr, "FAIL: cannot open %s\n", path );
		return 2;
	}
	while( fgets( line, sizeof( line ), f ) ) {
		char name[64];
		char pattern[64];
		int expect;

		if( line[0] == '#' || line[0] == '\n' ) {
			continue;
		}
		if( sscanf( line, "%63s %63s %d", name, pattern, &expect ) != 3 ) {
			continue;
		}
		int got = board_glob_match( pattern, name );

		checked++;
		if( got != expect ) {
			failed++;
			printf( "FAIL: %s ~ %s expect=%d got=%d\n", name, pattern, expect, got );
		}
	}
	fclose( f );
	printf( "select-vectors: checked=%u failed=%u\n", checked, failed );
	return failed ? 1 : 0;
}
#endif /* BOARD_SELECT_HOSTTEST */
