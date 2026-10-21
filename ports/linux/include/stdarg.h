/*
 * @file
 * @brief Variadic names for the kernel build.
 *
 * The kernel spells the va_list family in <linux/stdarg.h>; the compiler's own
 * <stdarg.h> is outside the kernel's -nostdinc search. The imported sources use
 * the standard names.
 */

#ifndef _WLAN_LINUX_STDARG_H_
#define _WLAN_LINUX_STDARG_H_

#include <linux/stdarg.h>

#endif /* _WLAN_LINUX_STDARG_H_ */
