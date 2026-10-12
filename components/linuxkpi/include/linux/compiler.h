/* SPDX-License-Identifier: BSD-3-Clause */
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

/*
 * The interface's spelling of `inline` ([LNX] include/linux/compiler_types.h:235:
 * `#define inline inline __gnu_inline __inline_maybe_unused notrace`, with
 * __gnu_inline from compiler_attributes.h:165). It is carried here because it is a
 * semantic switch, not a decoration: without the __gnu_inline marker a plain `inline`
 * function has C99 external linkage, and GCC then diagnoses every reference it makes
 * to an internal-linkage helper ("is static but used in inline function ... which is
 * not static") - a diagnostic the interface's own spelling does not produce, so the
 * vendored bytes are compiled here with the interface's spelling. The __unused part
 * is the interface's __inline_maybe_unused and only silences the not-used diagnostic
 * for an inline the compiler chooses not to emit. `__inline` (one underscore) is left
 * alone on purpose: the interface reserves it for the bare keyword (compiler_types.h:
 * "the spelling __inline is strictly reserved for referring to the bare keyword").
 */
#ifndef __gnu_inline
#define	__gnu_inline		__attribute__((__gnu_inline__))
#endif
#ifndef __inline_maybe_unused
#define	__inline_maybe_unused	__maybe_unused
#endif
#define	inline			inline __gnu_inline __inline_maybe_unused
#define	__inline__		inline

#endif /* _LINUXKPI_LINUX_COMPILER_H_ */
