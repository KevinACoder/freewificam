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

/* SysV-compat spelling upstream sys/types.h carries and audiodef.h's
 * rwlock shape uses */
typedef unsigned int uint;

/* upstream sys/types.h's boolean spellings (uaudio's format walk is the
 * consumer of these) */
#ifndef TRUE
#define TRUE	1
#endif
#ifndef FALSE
#define FALSE	0
#endif

typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;
typedef uintptr_t vsize_t;

/*
 * inttypes vocabulary.  NetBSD reaches the PRI and SCN macros through
 * the sys/types.h -> machine/int_types.h -> sys/inttypes.h chain; here
 * they have to come from somewhere, and sys/types.h is the include every
 * imported source shares.  AArch64 is LP64, so the 64-bit forms are the
 * 'l' ones (which this image's vsnprintf handles).  Only the names the
 * compiled set uses are defined.
 */
#ifndef PRId64
#define PRId64		"ld"
#endif
#ifndef PRIi64
#define PRIi64		"li"
#endif
#ifndef PRIu64
#define PRIu64		"lu"
#endif
#ifndef PRIx64
#define PRIx64		"lx"
#endif
#ifndef PRIX64
#define PRIX64		"lX"
#endif
#ifndef PRIo64
#define PRIo64		"lo"
#endif
#ifndef PRId32
#define PRId32		"d"
#endif
#ifndef PRIu32
#define PRIu32		"u"
#endif
#ifndef PRIx32
#define PRIx32		"x"
#endif
#ifndef PRIX32
#define PRIX32		"X"
#endif
#ifndef PRId16
#define PRId16		"d"
#endif
#ifndef PRIu16
#define PRIu16		"u"
#endif
#ifndef PRIx16
#define PRIx16		"x"
#endif
#ifndef PRIdPTR
#define PRIdPTR		"ld"
#endif
#ifndef PRIuPTR
#define PRIuPTR		"lu"
#endif
#ifndef PRIxPTR
#define PRIxPTR		"lx"
#endif
#ifndef PRIdMAX
#define PRIdMAX		"ld"
#endif
#ifndef PRIuMAX
#define PRIuMAX		"lu"
#endif
#ifndef PRIxMAX
#define PRIxMAX		"lx"
#endif
/* the imported drivers reach this through <sys/types.h> on LP64;
 * xhci.c concatenates it into printf formats ("%" __PRIxBITS) */
#ifndef __PRIxBITS
#define __PRIxBITS	PRIx64
#endif

#endif /* _COMPAT_SYS_TYPES_H_ */

/*
 * When this header shadows the libc <sys/types.h> (CMSIS port: the
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

#include_next <sys/types.h>
