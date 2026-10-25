/*
 * @file
 * @brief C stdio names for the kernel build.
 *
 * Declared instead of pulled from <linux/kernel.h>: that header drags the
 * assembler ALIGN, linkage and other kernel vocabulary into the net80211 world,
 * where ALIGN already means "round a pointer".  snprintf/vsnprintf are exported
 * by the kernel's own lib/vsprintf.c; kprintf/vprintf are the port's log seam
 * (osal_cmsis2.c, Linux branch).
 */

#ifndef _WLAN_LINUX_STDIO_H_
#define _WLAN_LINUX_STDIO_H_

#include <linux/types.h>
#include <linux/stdarg.h>

int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int kprintf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);

/* the port's log seam under the libc name the imported sources use */
#define printf	kprintf

#endif /* _WLAN_LINUX_STDIO_H_ */
