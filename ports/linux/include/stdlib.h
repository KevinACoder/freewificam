/*
 * @file
 * @brief C stdlib names for the kernel build.
 *
 * Only the exit path: the imported sources give up through abort() when an
 * internal invariant fails.  The implementation parks the task in
 * ports/linux/osal/osal_abort_linux.c.  Allocation is not taken from here -
 * the NetBSD malloc(9) shell maps it onto the port's allocation seam.
 */

#ifndef _WLAN_LINUX_STDLIB_H_
#define _WLAN_LINUX_STDLIB_H_

void abort(void) __attribute__((__noreturn__));

static inline int abs(int value)
{
	return value < 0 ? -value : value;
}

#endif /* _WLAN_LINUX_STDLIB_H_ */
