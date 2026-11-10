/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   host_log.c
 * @brief  The linuxkpi line sink for the host tier: the component formats a
 *         whole line (with its newline); the sink writes it to stdout.
 *
 * The board registers a stamped console sink; the host registers stdio.  This
 * is the second implementation the seam in linuxkpi_impl.h is proven with.
 */
#include <stdio.h>

#include "ulog.h"

void host_log_sink(ulog_level_t level, const char *line);

void host_log_sink(ulog_level_t level, const char *line)
{
	(void)level;
	fputs(line, stdout);
	fflush(stdout);
}
