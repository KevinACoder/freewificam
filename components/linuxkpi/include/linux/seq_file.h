/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/seq_file.h
 * @brief   The sequential-file record and operation table, as the interface's header
 *          of this name introduces them.
 *
 * @design declaration-only compatibility face, deliberately incomplete: the interface
 *         (include/linux/seq_file.h) defines `struct seq_file` and `struct
 *         seq_operations` in full, but the compiled set only ever holds pointers to
 *         them - the vendored /proc surface stores a show function and an operation
 *         table in its own descriptor and hands the record back untouched, and that
 *         whole surface is switched off for this build anyway. Forward declarations are
 *         therefore the honest statement: a caller that dereferences either type must
 *         bring a real shape, and no caller here does. The interface's own include of
 *         linux/mutex.h is kept so the header set reaches the mutex API the way it does
 *         on a Linux kernel. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SEQ_FILE_H_
#define _LINUXKPI_LINUX_SEQ_FILE_H_

#include "../../linuxkpi_core.h"

#include <linux/mutex.h>
/* [LNX] include/linux/seq_file.h includes <linux/string.h>; the face keeps the
 * interface's own include, which is also the path by which the compiled set reaches the
 * string helper declarations. */
#include <linux/string.h>

struct seq_file;
struct seq_operations;

/*
 * [LNX] include/linux/seq_file.h:118: `void seq_printf(struct seq_file *m, const char
 * *fmt, ...);`. The vendored debug header expands it from its own logging macros, so the
 * name has to resolve; it is declared with the interface's signature and no body,
 * because formatting into a sequential file needs the record and the output routine
 * behind it, which this project has no use for (the whole /proc surface is compiled out
 * for this build) - a caller that reaches it asks the linker for the real thing.
 */
void seq_printf(struct seq_file *m, const char *fmt, ...);

#endif /* _LINUXKPI_LINUX_SEQ_FILE_H_ */
