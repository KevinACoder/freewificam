/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/utsname.h
 * @brief   The system-name record a driver reads the running release string from.
 *
 * @design declaration-only compatibility face with one first-party body. The interface's
 *         accessor walks into the calling task's UTS namespace; this project runs one
 *         flat image with no namespaces and no per-task kernel state, so the face keeps a
 *         single system-name record whose release string is the version the face declares
 *         (linux/version.h) and the accessor returns it. Field widths follow the
 *         interface's uapi header (__NEW_UTS_LEN + 1). Consumers read the release string
 *         for a version banner only; the accessor is exposed as an inline so no
 *         translation unit needs a symbol. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_UTSNAME_H_
#define _LINUXKPI_LINUX_UTSNAME_H_

#include "../../linuxkpi_core.h"
#include "version.h"

#define	LKPI_NEW_UTS_LEN	64

struct new_utsname {
	char sysname[LKPI_NEW_UTS_LEN + 1];
	char nodename[LKPI_NEW_UTS_LEN + 1];
	char release[LKPI_NEW_UTS_LEN + 1];
	char version[LKPI_NEW_UTS_LEN + 1];
	char machine[LKPI_NEW_UTS_LEN + 1];
	char domainname[LKPI_NEW_UTS_LEN + 1];
};

static inline struct new_utsname *utsname(void)
{
	static struct new_utsname lkpi_utsname = {
		.sysname = "Linux",
		.nodename = "",
		.release = UTS_RELEASE,
		.version = "",
		.machine = "",
		.domainname = "",
	};

	return &lkpi_utsname;
}

#endif /* _LINUXKPI_LINUX_UTSNAME_H_ */
