/*
 * @file
 * @brief once(9) stub: run immediately.
 */

#ifndef _COMPAT_SYS_ONCE_H_
#define _COMPAT_SYS_ONCE_H_

#include <sys/cdefs.h>

/* ONCE_DECL declares the once-control; RUN_ONCE runs fn exactly once. */
#define ONCE_DECL(n) int n
#define RUN_ONCE(ctl, fn) ((*(ctl)) ? 0 : ((*(ctl)) = 1, (fn)(), 0))

#endif /* _COMPAT_SYS_ONCE_H_ */
