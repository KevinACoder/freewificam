/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/pid.h
 * @brief   The process-id record and the lookup a driver's thread-signalling path names.
 *
 * @design declaration-only compatibility face. The interface (include/linux/pid.h)
 *         describes `struct pid` - the kernel's reference-counted process-id object, with
 *         its numbers, its tasks and its namespace - and the lookup helpers over it. The
 *         vendored platform layer reaches the record only as a handle: it looks one up
 *         from a numeric id and hands it to the signal path, never reading a field. The
 *         record is therefore declared, not defined, and the lookup is declared with the
 *         interface's signature and no body, because producing a pid object needs the
 *         port's task table - this project's kernels identify threads, they do not have
 *         the kernel's pid namespace machinery. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_PID_H_
#define _LINUXKPI_LINUX_PID_H_

#include "../../linuxkpi_core.h"

struct pid;

/* [LNX] include/linux/pid.h:125: extern struct pid *find_vpid(int nr); */
extern struct pid *find_vpid(int nr);

#endif /* _LINUXKPI_LINUX_PID_H_ */
