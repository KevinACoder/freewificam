/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/overflow.h
 * @brief   The on-stack flexible-array helper a driver uses to build a record
 *          with a trailing array whose length is a compile-time constant.
 *
 * @design declaration-only compatibility face, grown per driver. What a compiled
 *         consumer names is the DEFINE_RAW_FLEX() family and the two size
 *         accessors that read a defined instance back. The record shapes are
 *         transcribed from the interface (include/linux/overflow.h:477-497,
 *         515-516, 533-536 and include/linux/compiler_types.h:525-529); the size
 *         sum the interface states through struct_size() is stated here directly,
 *         because the port has no overflow-checking size_mul family behind it and
 *         the consumer passes a compile-time count (the interface's own
 *         _Static_assert keeps that requirement). Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_OVERFLOW_H_
#define _LINUXKPI_LINUX_OVERFLOW_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/stddef.h:24: the size of one member. */
#ifndef sizeof_field
#define	sizeof_field(TYPE, MEMBER)	sizeof((((TYPE *)0)->MEMBER))
#endif

/* [LNX] include/linux/compiler_types.h:525-529: the size of the whole object and
 * of its trailing member, as the compiler can see them. The dynamic form is what
 * the interface prefers where the compiler has it. */
#ifndef __struct_size
#if __has_builtin(__builtin_dynamic_object_size)
#define	__struct_size(p)	__builtin_dynamic_object_size(p, 0)
#define	__member_size(p)	__builtin_dynamic_object_size(p, 1)
#else
#define	__struct_size(p)	__builtin_object_size(p, 0)
#define	__member_size(p)	__builtin_object_size(p, 1)
#endif
#endif

/* struct_size_t and struct_size ([LNX] include/linux/overflow.h:452-453) live in
 * the contract header, next to the rest of the size arithmetic the whole compiled
 * set shares; this face names them through it rather than restating them. */

/* [LNX] include/linux/overflow.h:477-485: the helper the family builds on. The
 * union gives the instance its storage and the named pointer gives the caller the
 * typed view, so `__struct_size(name)` reports the whole record. */
#define	__DEFINE_FLEX(type, name, member, count, trailer...)		\
	_Static_assert(__builtin_constant_p(count),			\
	    "onstack flex array members require compile-time const count"); \
	union {								\
		u8 bytes[struct_size_t(type, member, count)];		\
		type obj;						\
	} name##_u trailer;						\
	type *name = (type *)&name##_u

/* [LNX] include/linux/overflow.h:496-497,515-516,533-534 */
#define	_DEFINE_FLEX(type, name, member, count, initializer...)		\
	__DEFINE_FLEX(type, name, member, count, = { .obj initializer })

#define	DEFINE_RAW_FLEX(type, name, member, count)			\
	__DEFINE_FLEX(type, name, member, count, = { })

#define	DEFINE_FLEX(TYPE, NAME, MEMBER, COUNTER, COUNT)			\
	_DEFINE_FLEX(TYPE, NAME, MEMBER, COUNT, = { .COUNTER = COUNT, })

#endif /* _LINUXKPI_LINUX_OVERFLOW_H_ */
