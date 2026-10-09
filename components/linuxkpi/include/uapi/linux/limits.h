/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    uapi/linux/limits.h
 * @brief   The value limits the interface publishes to its users: path, name and
 *          buffer sizes.
 *
 * @design declaration-only compatibility face, transcribed constant for constant from
 *         the interface's uapi header (include/uapi/linux/limits.h). The vendored
 *         driver's platform layer takes this header by name for its kernel-version
 *         branch, and the values are contract with anything that formats or bounds a
 *         name; a limit with the wrong value is worse than a missing one, so they are
 *         copied exactly and nothing else is added. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_UAPI_LINUX_LIMITS_H_
#define _LINUXKPI_UAPI_LINUX_LIMITS_H_

#include "../../../linuxkpi_core.h"

#define	NR_OPEN		1024

#define	NGROUPS_MAX	65536	/* supplemental group IDs are available */
#define	ARG_MAX		131072	/* # bytes of args + environ for exec() */
#define	LINK_MAX	127	/* # links a file may have */
#define	MAX_CANON	255	/* size of the canonical input queue */
#define	MAX_INPUT	255	/* size of the type-ahead buffer */
#define	NAME_MAX	255	/* # chars in a file name */
#define	PATH_MAX	4096	/* # chars in a path name including nul */
#define	PIPE_BUF	4096	/* # bytes in atomic write to a pipe */
#define	XATTR_NAME_MAX	255	/* # chars in an extended attribute name */
#define	XATTR_SIZE_MAX	65536	/* # bytes in an extended attribute value (64k) */
#define	XATTR_LIST_MAX	65536	/* # bytes in an extended attribute namelist (64k) */

#define	RTSIG_MAX	32

#endif /* _LINUXKPI_UAPI_LINUX_LIMITS_H_ */
