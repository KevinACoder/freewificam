/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/atomic.h
 * @brief   The atomic operations the interface declares that this component's atomic
 *          section does not already state.
 *
 * @design declaration-only compatibility face. The interface (include/linux/atomic.h
 *         over the arch fallback and instrumented headers) defines the atomic type and
 *         its whole operation set; the type and the operations this component serves are
 *         already stated once in the contract header (components/linuxkpi/linuxkpi_core.h,
 *         its atomic section: atomic_t plus atomic_read/set/add/sub/inc/dec and their
 *         _return forms), and they are the port's model - plain arithmetic on a
 *         single-core port, with the libcall fallbacks the impl adds. The three
 *         operations stated here are the ones the vendored bytes name that the contract
 *         header does not carry; their bodies follow that same model rather than
 *         inventing a second one, and they say so. A port that gains real atomic
 *         primitives replaces the bodies here together with the contract header's, not
 *         just one of the two. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_ATOMIC_H_
#define _LINUXKPI_LINUX_ATOMIC_H_

#include "../../linuxkpi_core.h"

/*
 * [LNX] include/linux/atomic/atomic-instrumented.h:120-122 and 286-288:
 * `static __always_inline int atomic_add_return(int i, atomic_t *v)` and its subtract
 * twin. The interface's body adds/subtracts and returns the new value; this port's
 * atomic model is the contract header's plain arithmetic, so the body is the addition
 * followed by the read that the contract header's atomic_read performs.
 */
static __always_inline __unused int
atomic_add_return(int i, atomic_t *v)
{
	lkpi_atomic_add(i, v);
	return atomic_read(v);
}

static __always_inline __unused int
atomic_sub_return(int i, atomic_t *v)
{
	lkpi_atomic_sub(i, v);
	return atomic_read(v);
}

/*
 * [LNX] include/linux/atomic/atomic-instrumented.h:1514-1516:
 * `static __always_inline bool atomic_add_unless(atomic_t *v, int a, int u)`.
 * The interface adds `a` only when the current value is not `u`, and reports whether it
 * added; the compare-then-add on this port is the contract header's
 * lkpi_atomic_cmpxchg loop, which is the same operation the interface's arch fallback
 * performs.
 */
static __always_inline __unused bool
atomic_add_unless(atomic_t *v, int a, int u)
{
	int old = atomic_read(v);

	for (;;) {
		if (old == u)
			return false;
		if (lkpi_atomic_cmpxchg(v, old, old + a) == old)
			return true;
		old = atomic_read(v);
	}
}

#endif /* _LINUXKPI_LINUX_ATOMIC_H_ */
