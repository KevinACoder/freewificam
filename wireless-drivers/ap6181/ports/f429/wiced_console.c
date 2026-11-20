/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wiced_console.c - the vendor driver's printf, on the board's console.
 *
 * WPRINT_WWD_* expands to printf.  The platform binds vsnprintf/snprintf but
 * not printf (its own console is uart_early_*), so without this the driver's
 * diagnostics would be dropped.  It exists only when the profile asks for the
 * driver log, and it is deliberately bounded: no floating point, and a fixed
 * line buffer on the caller's stack.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#include "freewificam_config.h"

#if defined(CONFIG_WICED_AP6181_LOG) && (CONFIG_WICED_AP6181_LOG == 1)

void uart_early_puts(const char *s);

int
printf(const char *fmt, ...)
{
	char buf[192];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n > 0)
		uart_early_puts(buf);
	return n;
}

#endif
