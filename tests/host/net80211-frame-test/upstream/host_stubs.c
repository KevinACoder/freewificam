/*
 * @file    host_stubs.c
 * @brief   The one host-side symbol the re-anchored library tests need.
 *
 * The library's own test runner supplied panic() through its retired include
 * shims; on this port the same symbol comes from here so that the two test
 * sources stay byte-identical to their pin.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	abort();
}
