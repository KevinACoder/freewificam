/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/cleanup.h
 * @brief   The scope-based cleanup and lock-guard helpers a driver's unwind paths
 *          are written with.
 *
 * @design compatibility face, grown per consumer. The scope-guard vocabulary
 *         (CLASS, guard, scoped_guard, the rcu class) is part of the core contract
 *         every file of this face includes; this header adds the definition macros
 *         a consumer uses to *declare* a guard class or a cleanup function
 *         (DEFINE_CLASS, DEFINE_GUARD, DEFINE_FREE and the __free() attribute),
 *         transcribed from include/linux/cleanup.h. The one placement choice worth
 *         naming: the interface defines DEFINE_FREE(kfree) in linux/slab.h, but
 *         the consumer here reaches __free(kfree) through this header (its
 *         translation unit includes mvm.h, which includes this header, and never
 *         linux/slab.h), so the kfree cleanup is stated here where it is reached,
 *         once. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_CLEANUP_H_
#define _LINUXKPI_LINUX_CLEANUP_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/cleanup.h:184: the nonnull annotation on a guard class's
 * constructor prototype; this face's compiler class takes the attribute form. */
#ifndef __nonnull_args
#define	__nonnull_args(args...)	__attribute__((__nonnull__(args)))
#endif

/* [LNX] include/linux/cleanup.h:160-161 */
#define	DEFINE_FREE(_name, _type, _free)				\
	static __always_inline void __free_##_name(void *p)		\
	{ _type _T = *(_type *)p; _free; }

/* [LNX] include/linux/cleanup.h:163 */
#define	__free(_name)		__cleanup(__free_##_name)

/* [LNX] include/linux/cleanup.h:260-269: the destructor/constructor pair a named
 * class is built from. */
#define	DEFINE_CLASS(_name, _type, _exit, _init, _init_args...)		\
	typedef _type class_##_name##_t;				\
	typedef _type lock_##_name##_t;					\
	static __always_inline void class_##_name##_destructor(_type *p)	\
		__no_context_analysis					\
	{ _type _T = *p; _exit; }					\
	static __always_inline _type class_##_name##_constructor(_init_args)	\
		__no_context_analysis					\
	{ _type t = _init; return t; }

/* [LNX] include/linux/cleanup.h:361-362 */
#define	__DEFINE_CLASS_IS_CONDITIONAL(_name, _is_cond)			\
	static __maybe_unused const bool class_##_name##_is_conditional = _is_cond

/* [LNX] include/linux/cleanup.h:379-397: the pointer and error accessors a guard
 * class publishes for the scoped and conditional forms. */
#define	__DEFINE_GUARD_LOCK_PTR(_name, _exp)				\
	static __always_inline void *class_##_name##_lock_ptr(class_##_name##_t *_T) \
	{								\
		void *_ptr = (void *)(__force unsigned long)*(_exp);	\
		if (IS_ERR(_ptr))					\
			_ptr = NULL;					\
		return _ptr;						\
	}								\
	static __always_inline int class_##_name##_lock_err(class_##_name##_t *_T) \
	{								\
		long _rc = (__force unsigned long)*(_exp);		\
		if (!_rc)						\
			_rc = -EBUSY;					\
		if (!IS_ERR_VALUE(_rc))					\
			_rc = 0;					\
		return _rc;						\
	}

/* [LNX] include/linux/cleanup.h:399-401 */
#define	DEFINE_CLASS_IS_GUARD(_name)					\
	__DEFINE_CLASS_IS_CONDITIONAL(_name, false);			\
	__DEFINE_GUARD_LOCK_PTR(_name, _T)

/* [LNX] include/linux/cleanup.h:403-406 */
#define	DEFINE_GUARD(_name, _type, _lock, _unlock)			\
	static __always_inline __nonnull_args(1)			\
	_type class_##_name##_constructor(_type _T);			\
	DEFINE_CLASS(_name, _type, _unlock, ({ _lock; _T; }), _type _T);	\
	DEFINE_CLASS_IS_GUARD(_name)

/* [LNX] include/linux/slab.h:599: stated here because that is the header the
 * consumer reaches it through (see the @design note). */
DEFINE_FREE(kfree, void *, if (!IS_ERR_OR_NULL(_T)) kfree(_T))

#endif /* _LINUXKPI_LINUX_CLEANUP_H_ */
