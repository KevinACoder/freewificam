/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * platform_toolchain.h - the compiler-attribute vocabulary the WICED sources
 * use, written for this port.
 *
 * The vendor ships one copy of this header per sample project (under User/);
 * the driver core includes it unconditionally, so the port owns it.  Only the
 * macros the driver actually uses are defined, and the libc/sbrk helpers the
 * vendor platform's newlib glue declares are deliberately absent: this port
 * links the platform's own libc and never calls them.
 */

#ifndef AP6181_PLATFORM_TOOLCHAIN_H
#define AP6181_PLATFORM_TOOLCHAIN_H

#include <stddef.h>

#ifndef WEAK
#define WEAK			__attribute__((weak))
#endif
#ifndef MAY_BE_UNUSED
#define MAY_BE_UNUSED		__attribute__((unused))
#endif
#ifndef NORETURN
#define NORETURN		__attribute__((noreturn))
#endif
#ifndef ALIGNED_PRE
#define ALIGNED_PRE(size)
#endif
#ifndef ALIGNED
#define ALIGNED(size)		__attribute__((aligned(size)))
#endif
#ifndef SECTION
#define SECTION(name)		__attribute__((section(name)))
#endif
#ifndef NEVER_INLINE
#define NEVER_INLINE		__attribute__((noinline))
#endif
#ifndef ALWAYS_INLINE_PRE
#define ALWAYS_INLINE_PRE
#endif
#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE		__attribute__((always_inline))
#endif
#ifndef INLINE_ASM
#define INLINE_ASM		__asm__
#endif

#endif /* AP6181_PLATFORM_TOOLCHAIN_H */