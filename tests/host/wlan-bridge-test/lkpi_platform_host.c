/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_platform_host.c
 * @brief   The host implementation of the linuxkpi platform hooks this chain
 *          needs: the pool's bindings (critical section, heap, log) and the
 *          suite's start-up pair (log sink + core services).
 *
 * @design adapter - same shape as the body suite's platform file
 *         (tests/host/linuxkpi-body-test/lkpi_platform_host.c). The net80211
 *         half keeps its own log seam (wlan_osal_log), which the port suite
 *         prints; this file only serves the linuxkpi half.
 * @date    08.10.2026
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linuxkpi_impl.h"

#include "skbuff/skbuff_pool.h"

static void lkpi_platform_sink(ulog_level_t level, const char *line);

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

	(void)fmt;
	(void)ap;
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

void
lkpi_wlan_test_platform_init(void)
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

/* ------------------------------------------------------------------ */
/* the diagnostic face the driver references but a host build has no   */
/* filesystem for: debugfs is a no-op and the hex dump prints           */
/* ------------------------------------------------------------------ */

#include "linux/debugfs.h"
#include "linux/fs.h"
#include "linux/printk.h"

struct dentry *
debugfs_create_dir(const char *name, struct dentry *parent)
{

	(void)name;
	(void)parent;
	return NULL;		/* no debugfs on a host build */
}

struct dentry *
debugfs_create_file_short(const char *name, umode_t mode,
    struct dentry *parent, void *data, const void *aux,
    const struct debugfs_short_fops *fops)
{

	(void)name; (void)mode; (void)parent; (void)data; (void)aux; (void)fops;
	return NULL;
}

ssize_t
simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos,
    const void *from, size_t available)
{

	(void)to; (void)count; (void)ppos; (void)from; (void)available;
	return 0;
}

void
print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
    int rowsize, int groupsize, const void *buf, size_t len, bool ascii)
{

	(void)level; (void)prefix_str; (void)prefix_type; (void)rowsize;
	(void)groupsize; (void)buf; (void)len; (void)ascii;
}
