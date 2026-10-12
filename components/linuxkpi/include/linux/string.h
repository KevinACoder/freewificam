/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/string.h
 * @brief   The string helpers the interface declares that this component's C surface
 *          does not already carry.
 *
 * @design declaration-only compatibility face, grown per consumer. The interface
 *         (include/linux/string.h) owns the kernel's whole string vocabulary; this
 *         component's own string surface is the C library the contract header includes,
 *         so what is stated here is only the interface's additions to it - the helpers a
 *         freestanding C library does not have to provide. `strsep()` is the one the
 *         compiled set names (the vendored parsing paths split a comma/space separated
 *         value with it): it is a C-library function on a hosted build but not part of
 *         the freestanding C surface this project compiles against, so the interface's
 *         declaration is stated here, and its body belongs to the port's C library
 *         rather than being written a second time. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_STRING_H_
#define _LINUXKPI_LINUX_STRING_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/string.h:207: extern char * strsep(char **, const char *); */
extern char *strsep(char **s, const char *ct);

/* [LNX] include/linux/string.h:167: the case-insensitive comparison the compiled set
 * names (the vendored parsing paths use it). */
extern int strncasecmp(const char *s1, const char *s2, size_t n);

/* [LNX] include/linux/string.h:390: the bounded copy that pads the tail, declared as
 * the interface declares it; its body belongs to the port's C library. */
void memcpy_and_pad(void *dest, size_t dest_len, const void *src, size_t count,
		    int pad);

/*
 * [LNX] include/linux/string.h:126-146: strscpy_pad(). The interface spells it as a
 * macro chain that dispatches on the argument count (the two-argument form sizes the
 * destination from the array); the compiled set only names the explicit-count form, so
 * that one is stated here, over this component's strscpy, with the interface's
 * semantics: the destination is always NUL-terminated and the tail is zero-filled, and
 * a truncation answers -E2BIG.
 */
static __always_inline __unused ssize_t
strscpy_pad(char *dst, const char *src, size_t count)
{
	size_t srclen = strlen(src);
	size_t n;

	if (count == 0)
		return -E2BIG;
	n = srclen < count - 1 ? srclen : count - 1;
	memcpy(dst, src, n);
	for (; n < count; n++)
		dst[n] = '\0';

	return srclen < count ? (ssize_t)srclen : -E2BIG;
}

#endif /* _LINUXKPI_LINUX_STRING_H_ */
