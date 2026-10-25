/*
 * @file
 * @brief Fixed-width integer names for the kernel build.
 *
 * The port's host build takes <stdint.h> from the C library.  The kernel build
 * runs with -nostdinc and has no libc <stdint.h>, but <linux/types.h> already
 * carries the compiler's integer model (s8/u8/... are the interface's own
 * spellings).  This header only adds the standard names the imported NetBSD
 * sources use, so a translation unit keeps seeing <stdint.h>.
 */

#ifndef _WLAN_LINUX_STDINT_H_
#define _WLAN_LINUX_STDINT_H_

#include <linux/types.h>

typedef __s8	int8_t;
typedef __u8	uint8_t;
typedef __s16	int16_t;
typedef __u16	uint16_t;
typedef __s32	int32_t;
typedef __u32	uint32_t;
typedef __s64	int64_t;
typedef __u64	uint64_t;

typedef long		intptr_t;
typedef unsigned long	uintptr_t;
typedef long long	intmax_t;
typedef unsigned long long uintmax_t;

#define INT8_MIN	(-128)
#define INT8_MAX	127
#define UINT8_MAX	255
#define INT16_MIN	(-32768)
#define INT16_MAX	32767
#define UINT16_MAX	65535
#define INT32_MIN	(-2147483647 - 1)
#define INT32_MAX	2147483647
#define UINT32_MAX	4294967295U
#define INT64_MIN	(-9223372036854775807LL - 1)
#define INT64_MAX	9223372036854775807LL
#define UINT64_MAX	18446744073709551615ULL

#define INT8_C(x)	(x)
#define UINT8_C(x)	(x)
#define INT16_C(x)	(x)
#define UINT16_C(x)	(x)
#define INT32_C(x)	(x)
#define UINT32_C(x)	(x##U)
#define INT64_C(x)	(x##LL)
#define UINT64_C(x)	(x##ULL)

#endif /* _WLAN_LINUX_STDINT_H_ */
