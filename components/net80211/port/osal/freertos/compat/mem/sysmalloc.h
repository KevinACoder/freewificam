/*
 * @file
 * @brief sysmalloc shell for the FreeRTOS port.
 *
 * Implemented in port/osal/freertos/osal_freertos.c on the FreeRTOS
 * heap (heap_4); declared here so the shared mbuf/ifnet/usbdi shells
 * compile unmodified.
 *
 * @author zhugengyu
 * @date 10.09.2026
 */

#ifndef _COMPAT_MEM_SYSMALLOC_H_
#define _COMPAT_MEM_SYSMALLOC_H_

#include <stddef.h>

void *sysmalloc(size_t size);
void sysfree(void *p);
void *sysmemalign(size_t align, size_t size);
void sysfree(void *p);

#endif /* _COMPAT_MEM_SYSMALLOC_H_ */
