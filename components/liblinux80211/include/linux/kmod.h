/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/kmod.h
 * @brief   The module-loading entry point a built-in driver names when it can be
 *          split into a core and per-vendor parts.
 *
 * @design declaration-only compatibility face. The interface (include/linux/kmod.h)
 *         declares request_module()/__request_module() - the "run modprobe for
 *         this name and wait" pair - and the modprobe-path sysctls.
 *
 *         brcmfmac names it exactly once, in fwvid.c:69, and only inside
 *         `#if IS_MODULE(CONFIG_BRCMFMAC)`: when the driver is a module its
 *         per-vendor backends (wcc/cyw/bca) are separate modules that a first
 *         probe may have to load. This image is one built-in driver
 *         (driver_config.h defines CONFIG_BRCMFMAC as 1, never _MODULE), so that
 *         arm is closed and brcmf_fwvid_request_module() is the file's own
 *         `return 0` inline. The declaration is carried for the case the face is
 *         ever switched to a module build; the body (a first-party one, in
 *         linuxkpi_osdep.c) answers -ENOSYS, because this port has no module
 *         loader to reach - it must not answer success for a load that did not
 *         happen.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_KMOD_H_
#define _LINUXKPI_LINUX_KMOD_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/kmod.h: the kernel's request_module() spelling is a macro
 * over the wait/interruptible form, so a caller's format string is passed
 * through unchanged. */
int __request_module(bool wait, const char *fmt, ...);

#define	request_module(mod...)		__request_module(true, mod)

#endif /* _LINUXKPI_LINUX_KMOD_H_ */
