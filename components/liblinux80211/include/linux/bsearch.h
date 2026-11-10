/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/bsearch.h
 * @brief   The binary-search entry point a driver names to look a key up in a
 *          sorted table.
 *
 * @design declaration-only compatibility face. The interface splits the surface
 *         into an inline form (__inline_bsearch, include/linux/bsearch.h:6) and
 *         the extern entry point (include/linux/bsearch.h:33) whose body is a thin
 *         call into the inline; the comparator type the signature is written with
 *         is the interface's own (include/linux/types.h:277). Both halves are
 *         carried: the inline is transcribed here, and bsearch()'s body
 *         (linuxkpi_compat.c) is the interface's own call into it. Reference for
 *         the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_BSEARCH_H_
#define _LINUXKPI_LINUX_BSEARCH_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/types.h:277 */
typedef int (*cmp_func_t)(const void *a, const void *b);

/* [LNX] include/linux/bsearch.h:6-28: the interface's inline binary search, which
 * bsearch() below is a call into. Transcribed verbatim (the halving walk over the
 * comparator); nothing about it is platform-specific. */
static __always_inline __unused void *
__inline_bsearch(const void *key, const void *base, size_t num, size_t size,
		 cmp_func_t cmp)
{
	const char *pivot;
	int result;

	while (num > 0) {
		pivot = (const char *)base + (num >> 1) * size;
		result = cmp(key, pivot);

		if (result == 0)
			return (void *)pivot;

		if (result > 0) {
			base = pivot + size;
			num--;
		}
		num >>= 1;
	}

	return NULL;
}

/* [LNX] include/linux/bsearch.h:33 */
void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      cmp_func_t cmp);

#endif /* _LINUXKPI_LINUX_BSEARCH_H_ */
