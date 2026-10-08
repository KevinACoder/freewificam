/* SPDX-License-Identifier: BSD-2-Clause */
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
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_PRINTK_H_
#define _LINUXKPI_LINUX_PRINTK_H_

#include "../../linuxkpi_core.h"

/* The prefix constants (DUMP_PREFIX_*) already live in the contract header, which
 * this file includes; only the dumper itself is missing there. */

/*
 * Dump a buffer as hex, one line per `rowsize` bytes, prefixing each line according to
 * `prefix_type`. `ascii` asks for the printable column.
 */
void print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
		    int rowsize, int groupsize, const void *buf, size_t len,
		    bool ascii);

#endif /* _LINUXKPI_LINUX_PRINTK_H_ */
