/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/compiler_types.h
 * @brief   The compiler attributes that depend on the compiler version, as the
 *          interface's compiler_types.h states them.
 *
 * @design the interface keeps two compiler headers: compiler_attributes.h for the
 *         attributes every supported compiler has (carried here verbatim), and
 *         compiler_types.h for the ones that depend on the compiler, the
 *         configuration or the architecture - the "counted by" family, which tells
 *         the compiler that a flexible-array member's bound lives in a sibling
 *         field. compiler.h includes compiler_types.h, so on a Linux build every
 *         source that names one of these macros has it through the ordinary
 *         include chain.
 *
 *         This port's contract header includes compiler_attributes.h itself (that
 *         is where the always-there attributes live), so the version-dependent
 *         family is stated here and included from the same place. The shapes and
 *         the version gates are the interface's: `__counted_by__` is a compiler
 *         attribute that GCC only gained recently, so a compiler without it gets
 *         an empty expansion - the bounds check is a diagnostics feature, never a
 *         code-generation one, and an empty expansion is exactly what the
 *         interface does for such a compiler. The endianness split for
 *         `__counted_by_le/be` follows the target's byte order as the interface
 *         does.
 *
 *         The values are the compiler's; nothing here is a port decision beyond
 *         which compiler this build uses.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_COMPILER_TYPES_H_
#define _LINUXKPI_LINUX_COMPILER_TYPES_H_

#include "../../linuxkpi_core.h"

/*
 * [LNX] include/linux/compiler_types.h:376-378: "the flexible array member this
 * attribute sits on has as many elements as the named sibling field says". The
 * attribute is a diagnostic; a compiler without it compiles the same program.
 */
#if __has_attribute(__counted_by__)
# define __counted_by(member)		__attribute__((__counted_by__(member)))
#else
# define __counted_by(member)
#endif

/* [LNX] include/linux/compiler_types.h:392-394: the pointer form, used where the
 * counted object is not a flexible array but an allocated block. */
#if __has_attribute(__counted_by__)
# define __counted_by_ptr(member)	__attribute__((__counted_by__(member)))
#else
# define __counted_by_ptr(member)
#endif

/* [LNX] include/linux/compiler_types.h:410-418: the endianness-qualified forms.
 * A little-endian target binds the count in its little-endian field; the other
 * direction is then not stated, which is what the interface's empty arm means. */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define __counted_by_le(member)	__counted_by(member)
# define __counted_by_be(member)
#else
# define __counted_by_le(member)
# define __counted_by_be(member)	__counted_by(member)
#endif

#endif /* _LINUXKPI_LINUX_COMPILER_TYPES_H_ */
