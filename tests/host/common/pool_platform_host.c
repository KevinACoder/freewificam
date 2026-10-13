/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    pool_platform_host.c
 * @brief   The host implementation of the skbuff pool's platform hooks: the
 *          critical section, the log sink and the heap backing.
 *
 * @design adapter - the same split and the same shape as the bridge suite's
 *         lkpi_platform_host.c: the pool body (components/linuxkpi/skbuff/
 *         skbuff_pool.c) is platform-pure and a host build defines
 *         LKPI_SKB_POOL_NO_TARGET_HOOKS, so the host side of the binding is
 *         here.  It lives under tests/host/common/ because every suite that
 *         links the pool needs exactly this binding, and a second copy of it
 *         would be a second way to say the same thing.
 *         It is its own translation unit because <stdlib.h> and the
 *         interface's <linux/kernel.h> both declare abs(); a file that
 *         includes both does not compile.
 * @date    10.10.2026
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "skbuff/skbuff_pool.h"

unsigned long
lkpi_spl_irqsave(void)
{

	return 0;
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

	/* the pool's refusals and forensic notes; the suite's own evidence is on
	 * stdout, so they go to stderr where they cannot be mistaken for it */
	va_start(ap, fmt);
	fputs("lkpi_skb: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
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

/* the fragment release's page binding (skbuff_pool.h): the harness has no page
 * allocator of its own - a page in these suites is whatever the test hands the
 * pool - so the reference's return is a no-op here */
void
lkpi_spl_page_put(struct page *page)
{

	(void)page;
}
