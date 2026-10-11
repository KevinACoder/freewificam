/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * host_shim.c - the one console entry the port files reach on the host.
 *
 * The imported units print through SDMMC_LOG, which the shadow of fsl_common.h
 * binds to this project's log outlet. On the board that outlet stamps a line
 * onto the console; here it goes to stderr, so a diagnostic from the adapter
 * cannot be mistaken for one of the suite's own anchor lines.
 */

#include <stdarg.h>
#include <stdio.h>

#include "log.h"

void log_emit(ulog_level_t severity, const char *fmt, ...)
{
	va_list ap;

	(void)severity;

	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
}
