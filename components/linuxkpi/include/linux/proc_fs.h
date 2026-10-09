/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/proc_fs.h
 * @brief   The include a driver's debug surface names; the surface is switched off for
 *          this build, and no declaration is reached.
 *
 * @design declaration-only compatibility face, deliberately empty of declarations. The
 *         interface (include/linux/proc_fs.h) declares the /proc registration API and
 *         `struct proc_dir_entry`; this project has no filesystem, and the vendored
 *         debug surface that would use it is compiled out - the suite's configuration
 *         leaves CONFIG_PROC_DEBUG undefined, which is what the vendored bytes gate
 *         every use of these on. Declaring a record no caller can complete would be a
 *         guess, so the header exists only to satisfy the include by name (the vendored
 *         proc header includes it unconditionally). Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_PROC_FS_H_
#define _LINUXKPI_LINUX_PROC_FS_H_

#include "../../linuxkpi_core.h"

struct proc_dir_entry;

#endif /* _LINUXKPI_LINUX_PROC_FS_H_ */
