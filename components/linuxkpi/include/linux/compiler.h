/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/compiler.h
 * @brief   The include a driver names for the compiler directives; the directives
 *          themselves are the component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/compiler.h
 *         and the compiler/ directory it pulls in) defines the attribute wrappers
 *         (__always_inline, __packed, __aligned, __section, __printf, __visible,
 *         likely/unlikely and the rest), and those are already stated once in the
 *         contract header (components/linuxkpi/linuxkpi_core.h, its macro block). A
 *         second statement here would be the duplicate mechanism the project forbids,
 *         so this header only satisfies the include by name. Reference for the face
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_COMPILER_H_
#define _LINUXKPI_LINUX_COMPILER_H_

#include "../../linuxkpi_core.h"

/* The one attribute of the interface's compiler_attributes.h that a consumer here
 * needs and the contract header does not already carry: the "this call does not
 * return" marker on the kernel-thread exit path ([LNX] include/linux/
 * compiler_attributes.h: __noreturn is __attribute__((__noreturn__))). */
#ifndef __noreturn
#define	__noreturn		__attribute__((__noreturn__))
#endif

#endif /* _LINUXKPI_LINUX_COMPILER_H_ */
