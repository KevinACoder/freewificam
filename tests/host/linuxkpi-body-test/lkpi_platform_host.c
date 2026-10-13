/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_platform_host.c
 * @brief   The host implementation of the linuxkpi platform hooks the pool body
 *          declares, plus the log sink this suite installs.
 *
 * @design adapter - the pool body's platform bindings (skbuff_pool.h:124-133)
 *         are the only OS-facing surface under test here, and this file is their
 *         host adapter: the critical section is a single-threaded no-op with the
 *         mask's contract (an operation cannot be split), the allocator is libc,
 *         and the log sink routes the component's lines to stdout. The board's
 *         own adapter is the pool file's target bottom (osCriticalEnter/Exit,
 *         osMemoryAlloc/Free); this file exists so the host link of the same body
 *         has the same bindings. Reference for the shape: the sibling real-pool
 *         adapter (tests/host/skbuff-allocation-free-stress/skbk_real.c).
 * @date    08.10.2026
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linuxkpi_impl.h"

#include "skbuff/skbuff_pool.h"

/* the suite's sink, installed at start-up */
static void lkpi_platform_sink(ulog_level_t level, const char *line);

/* ------------------------------------------------------------------ */
/* pool platform hooks                                                 */
/* ------------------------------------------------------------------ */

unsigned long
lkpi_spl_irqsave(void)
{

	return 0;		/* single-threaded host run */
}

void
lkpi_spl_irqrestore(unsigned long flags)
{

	(void)flags;
}

void
lkpi_spl_log(const char *fmt, ...)
{
	va_list ap;

	fputs("lkpi_skb: ", stdout);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	fflush(stdout);
}

void *
lkpi_spl_malloc(unsigned int size)
{

	return malloc(size != 0 ? size : 1u);
}

void
lkpi_spl_free(void *p)
{

	free(p);
}

/*
 * The fragment release's page binding (skbuff_pool.h): this harness counts the
 * references the pool hands back, because the fragment cases assert on exactly
 * that - a page's reference goes back once, when the payload dies, and not
 * before. The page memory itself belongs to the test that made the block.
 */
static unsigned long host_page_puts;

unsigned long
lkpi_host_page_puts(void)
{

	return host_page_puts;
}

void
lkpi_spl_page_put(struct page *page)
{

	(void)page;
	host_page_puts++;
}

/* ------------------------------------------------------------------ */
/* the suite's start-up                                                */
/* ------------------------------------------------------------------ */

/*
 * The pair the board layer performs at bring-up (register the console sink,
 * start the core services) has its host spelling here: the sink writes the
 * component's lines to stdout, and lkpi_core_init() creates the system
 * workqueue the USB engine's deferred callbacks ride on. The suite's main()
 * calls this once before it starts the kernel.
 */
void
lkpi_body_test_platform_init(void)
{

	lkpi_log_sink_register(lkpi_platform_sink);
	lkpi_core_init();
}

static void
lkpi_platform_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	fputs("lkpi: ", stdout);
	fputs(line, stdout);
	if (line[0] != '\0' && line[strlen(line) - 1] != '\n')
		fputc('\n', stdout);
	fflush(stdout);
}
