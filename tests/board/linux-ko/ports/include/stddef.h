/*
 * @file
 * @brief The standard type names the imported sources take from <stddef.h>.
 *
 * The kernel build runs with -nostdinc and carries no compiler <stddef.h>; it
 * spells size_t in <linux/types.h>.  This face supplies the few stddef names
 * the NetBSD sources use without pulling the kernel's <linux/stddef.h> in
 * (whose false/true enum would collide with <stdbool.h> in the same unit).
 */

#ifndef _WLAN_LINUX_STDDEF_H_
#define _WLAN_LINUX_STDDEF_H_

#include <linux/types.h>

#ifndef NULL
#define NULL	((void *)0)
#endif

#ifndef offsetof
#define offsetof(type, member)	__builtin_offsetof(type, member)
#endif

#ifndef __PTRDIFF_TYPE__
typedef long ptrdiff_t;
#else
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#endif

#endif /* _WLAN_LINUX_STDDEF_H_ */
