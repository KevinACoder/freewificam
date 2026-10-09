/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/sort.h
 * @brief   The sorting entry points a driver names for in-place array ordering.
 *
 * @design declaration-only compatibility face. The interface's bodies (the heapsort
 *         implementation and its optional swap callback) belong to the kernel's own
 *         library and have no counterpart on this port yet; what is stated here is the
 *         interface's signature, so driver code that orders a table reads unchanged and
 *         the port that provides the body decides the algorithm. A caller must not
 *         assume stability either way - the interface does not promise it, and the
 *         recorded signatures carry the same freedom.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SORT_H_
#define _LINUXKPI_LINUX_SORT_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/sort.h:22-25 */
void sort(void *base, size_t num, size_t size,
	  int (*cmp)(const void *, const void *),
	  void (*swap)(void *, void *, int));

/* [LNX] include/linux/sort.h:17-20: the reentrant form, whose comparator receives the
 * caller's private pointer. */
void sort_r(void *base, size_t num, size_t size,
	    int (*cmp)(const void *, const void *, const void *),
	    void (*swap)(void *, void *, int),
	    const void *priv);

#endif /* _LINUXKPI_LINUX_SORT_H_ */
