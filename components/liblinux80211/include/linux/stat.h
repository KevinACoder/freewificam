/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/stat.h
 * @brief   The file-mode bits the module-parameter and sysfs faces name.
 *
 * @design compatibility face: the kernel's stat.h declares the inode and
 *         attribute records plus the permission bits.  This port has no
 *         filesystem, so only the bit vocabulary is carried - it is what a
 *         `module_param(..., 0444)`-style line and a sysfs attribute mode are
 *         written with.  The values are the interface's (octal), not a local
 *         choice.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_STAT_H_
#define _LINUXKPI_LINUX_STAT_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/uapi/linux/stat.h: the owner/group/other permission bits. */
#define	S_IRWXU		00700
#define	S_IRUSR		00400
#define	S_IWUSR		00200
#define	S_IXUSR		00100

#define	S_IRWXG		00070
#define	S_IRGRP		00040
#define	S_IWGRP		00020
#define	S_IXGRP		00010

#define	S_IRWXO		00007
#define	S_IROTH		00004
#define	S_IWOTH		00002
#define	S_IXOTH		00001

/* [LNX] include/linux/stat.h: the "any read/write/execute" aliases kernel
 * sources use next to a module_param permission argument. */
#define	S_IRUGO		(S_IRUSR | S_IRGRP | S_IROTH)
#define	S_IWUGO		(S_IWUSR | S_IWGRP | S_IWOTH)
#define	S_IXUGO		(S_IXUSR | S_IXGRP | S_IXOTH)
#define	S_IRWXUGO	(S_IRWXU | S_IRWXG | S_IRWXO)

#endif /* _LINUXKPI_LINUX_STAT_H_ */
