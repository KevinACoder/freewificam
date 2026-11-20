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
 * [LNX] include/linux/string.h:126-146: strscpy_pad() now lives in the contract
 * header (components/linuxkpi/linuxkpi_core.h, its misc-helpers section, next to
 * strscpy()), because a compiled translation unit reaches the string vocabulary
 * through the core rather than through this header. It is not restated here.
 */

#endif /* _LINUXKPI_LINUX_STRING_H_ */
