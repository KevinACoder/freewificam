/*
 * @file
 * @brief The C99 boolean names for the kernel build.
 *
 * The kernel declares `bool` in <linux/types.h> and the false/true enumerators
 * in <linux/stddef.h>; the imported sources include <stdbool.h>.  Both kernel
 * homes are loaded first (the enumerators before the macros, the typedef
 * before either), then the standard macros are supplied.
 */

#ifndef _WLAN_LINUX_STDBOOL_H_
#define _WLAN_LINUX_STDBOOL_H_

#include <linux/types.h>
#include <linux/stddef.h>

#ifndef bool
#define bool	_Bool
#endif
#ifndef true
#define true	1
#endif
#ifndef false
#define false	0
#endif
#define __bool_true_false_are_defined	1

#endif /* _WLAN_LINUX_STDBOOL_H_ */
