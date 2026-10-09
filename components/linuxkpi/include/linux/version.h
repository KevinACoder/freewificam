/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/version.h
 * @brief   The kernel version the face's declarations are transcribed from.
 *
 * @design declaration-only compatibility face. Sources gate their own code on
 *         LINUX_VERSION_CODE and KERNEL_VERSION, so the value decides which branch of
 *         every such gate compiles - it is a contract with the vendored bytes, not a
 *         decoration. It is pinned to the interface generation this face follows (7.3,
 *         the kernel the vendored drivers' ports were prepared against), which is also
 *         the value that selects the modernised branches of the vendored vendor driver.
 *         The interface generates both spellings into its build directory and includes
 *         them from here; this face states them once, here. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_VERSION_H_
#define _LINUXKPI_LINUX_VERSION_H_

#include "../../linuxkpi_core.h"

#define	KERNEL_VERSION(a, b, c)	(((a) << 16) + ((b) << 8) + ((c) > 255 ? 255 : (c)))

#define	LINUX_VERSION_CODE	KERNEL_VERSION(7, 3, 0)

/* The matching release string: the interface's utsrelease header carries it, and
 * linux/utsname.h reports it through utsname()->release. */
#define	UTS_RELEASE		"7.3.0"

#endif /* _LINUXKPI_LINUX_VERSION_H_ */
