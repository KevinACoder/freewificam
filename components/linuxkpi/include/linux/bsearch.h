/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/bsearch.h
 * @brief   The binary-search entry point a driver names to look a key up in a
 *          sorted table.
 *
 * @design declaration-only compatibility face. The interface splits the surface
 *         into an inline form (__inline_bsearch, include/linux/bsearch.h:6) and
 *         the extern entry point (include/linux/bsearch.h:33) whose body belongs
 *         to the kernel's library; the comparator type the signature is written
 *         with is the interface's own (include/linux/types.h:277). What is
 *         stated here is the comparator type and the extern signature, so a
 *         caller reads unchanged; the body has no counterpart on this port yet,
 *         exactly as the sort entry points have none. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_BSEARCH_H_
#define _LINUXKPI_LINUX_BSEARCH_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/types.h:277 */
typedef int (*cmp_func_t)(const void *a, const void *b);

/* [LNX] include/linux/bsearch.h:33 */
void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      cmp_func_t cmp);

#endif /* _LINUXKPI_LINUX_BSEARCH_H_ */
