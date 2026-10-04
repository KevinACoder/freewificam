/*
 * @file
 * @brief Error numbers: chain to the toolchain's <sys/errno.h>
 * (include_next, the same discipline as cdefs.h) so newlib's own
 * errno.h finds its codes instead of this shadow's guard.
 */

#ifndef _COMPAT_SYS_ERRNO_H_
#define _COMPAT_SYS_ERRNO_H_

#include_next <sys/errno.h>

/* EMOVEFD is not in newlib's errno.h: upstream defines it as -6 (a
 * positive errno value is impossible anyway, so the sentinel is safe).
 * fd_clone() returns it to tell the open path "the vnode is installed,
 * this file took its place" - the audio(4) code KASSERTs on it. */
#ifndef EMOVEFD
#define EMOVEFD		-6
#endif


#endif /* _COMPAT_SYS_ERRNO_H_ */
