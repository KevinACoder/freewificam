/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/sprintf.h
 * @brief   The formatted-string entry points the interface declares that this
 *          component's own C surface does not already carry.
 *
 * @design declaration-only compatibility face. The interface (include/linux/sprintf.h)
 *         owns the whole sprintf family; this component's formatted output already has
 *         one home - the contract header's snprintf/vsnprintf declarations, which the
 *         image's own printf library implements - and the interface's include of this
 *         header from linux/kernel.h is kept so a unit taking kernel.h sees the same set
 *         it sees on a Linux kernel. Only the names the contract header lacks are stated
 *         here, each with the interface's signature, so nothing is spelled twice. They
 *         are declarations without bodies: the component's printf library offers its own
 *         entry-point spelling, so a caller that reaches these asks the port for the
 *         binding rather than getting a silently different formatter.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SPRINTF_H_
#define _LINUXKPI_LINUX_SPRINTF_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/sprintf.h: `__printf(2, 3) int sprintf(char *buf, const char *fmt,
 * ...);` and its va_list twin. The format-string checking attribute is the contract
 * header's __printf. */
__printf(2, 3) int sprintf(char *buf, const char *fmt, ...);
__printf(2, 0) int vsprintf(char *buf, const char *fmt, va_list args);

#endif /* _LINUXKPI_LINUX_SPRINTF_H_ */
