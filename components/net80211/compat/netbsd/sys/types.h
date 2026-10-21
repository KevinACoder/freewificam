/*
 * @file
 * @brief Basic types for the NetBSD-imported sources.
 */

#ifndef _COMPAT_SYS_TYPES_H_
#define _COMPAT_SYS_TYPES_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/cdefs.h>

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;
typedef uintptr_t vsize_t;

#endif /* _COMPAT_SYS_TYPES_H_ */

/*
 * When this header shadows the libc <sys/types.h> (FreeRTOS port: the
 * compat include path precedes the system directories), the libc
 * headers (unistd/stat) still expect its POSIX types. Fall through to
 * the libc types after the NetBSD ones; identical typedefs are legal
 * C11. In the embox build the host include tree resolves <sys/types.h>
 * first and this file is never processed.
 */
#ifndef _COMPAT_SYS_TYPES_LIBC_PASS_H_
#define _COMPAT_SYS_TYPES_LIBC_PASS_H_
#if defined(__NEWLIB__)
#include_next <sys/types.h>
#endif
#endif
