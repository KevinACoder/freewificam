/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/printk.h
 * @brief   The message-level prefixes and the hex dumper drivers call directly.
 *
 * @design declaration-only compatibility face; the message levels a driver uses
 *         through the dev_* helpers already live in the component's contract header
 *         (components/linuxkpi/linuxkpi_core.h), so this file carries only what that
 *         header does not: the hex dumper itself, declared as the interface declares it
 *         (include/linux/printk.h). The
 *         body is the project's logging seam, like every other print path.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_PRINTK_H_
#define _LINUXKPI_LINUX_PRINTK_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/printk.h includes <linux/kern_levels.h>; the face keeps the
 * interface's own include so the levels the logging macros put in front of a format
 * string are visible together with printk(). */
#include <linux/kern_levels.h>

/* The prefix constants (DUMP_PREFIX_*) already live in the contract header, which
 * this file includes; only the dumper itself is missing there. */

/*
 * Dump a buffer as hex, one line per `rowsize` bytes, prefixing each line according to
 * `prefix_type`. `ascii` asks for the printable column.
 */
void print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
		    int rowsize, int groupsize, const void *buf, size_t len,
		    bool ascii);

/*
 * printk(): [LNX] include/linux/printk.h:511 states it as a macro over _printk() (the
 * printk-indexing wrapper) and _printk() itself as `int _printk(const char *fmt, ...)`.
 * The vendored bytes reach for the bare printk() inside their own logging macros, so the
 * name has to resolve here. The component's line sink is lkpi_printf() - the same seam
 * the contract header's pr_* helpers already funnel through - and unlike those helpers
 * this mapping appends no newline, which is printk()'s own contract.
 */
#define	printk(fmt, ...)	lkpi_printf(ULOG_INFO_LEVEL, fmt, ##__VA_ARGS__)

#endif /* _LINUXKPI_LINUX_PRINTK_H_ */
