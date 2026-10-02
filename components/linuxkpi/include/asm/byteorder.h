#ifndef _LINUXKPI_ASM_BYTEORDER_H_
#define _LINUXKPI_ASM_BYTEORDER_H_
/* [S6] B8: the vendor byteorder stack (rtw_byteorder.h -> byteorder/*
 * -> swab.h) routes through here first (osdep funnel <asm/byteorder.h>,
 * included before drv_types.h pulls rtw_byteorder.h).  The swab face is
 * pre-empted with constant-folding builtins so `case htons(ETH_P_IP):`
 * stays an integer-constant expression (swab.h's own #ifndef guards
 * skip; the vendor's __fswab16 inline-function form would not fold). */
#include "../linuxkpi_core.h"

/* the endianness stamps the vendor byteorder stack tests (rtw_byteorder.h
 * and the duplicated ip.h/mlme_ext.h checks) */
#ifndef __LITTLE_ENDIAN
#define	__LITTLE_ENDIAN	1234
#endif
#ifndef __BIG_ENDIAN
#define	__BIG_ENDIAN	4321
#endif
#ifndef __BYTE_ORDER
#define	__BYTE_ORDER	__LITTLE_ENDIAN
#endif

#ifndef __swab16
#define	__swab16(x)	((__u16)__builtin_bswap16((__u16)(x)))
#endif
#ifndef __swab32
#define	__swab32(x)	((__u32)__builtin_bswap32((__u32)(x)))
#endif
#ifndef __swab64
#define	__swab64(x)	((__u64)__builtin_bswap64((__u64)(x)))
#endif

#ifndef ___constant_swab16
#define	___constant_swab16(x)	((__u16)(					\
	(((__u16)(x) & (__u16)0x00ffU) << 8) |				\
	(((__u16)(x) & (__u16)0xff00U) >> 8)))
#endif
#ifndef ___constant_swab32
#define	___constant_swab32(x)	((__u32)(					\
	(((__u32)(x) & 0x000000ffUL) << 24) |				\
	(((__u32)(x) & 0x0000ff00UL) << 8) |				\
	(((__u32)(x) & 0x00ff0000UL) >> 8) |				\
	(((__u32)(x) & 0xff000000UL) >> 24)))
#endif
#ifndef ___constant_swab64
#define	___constant_swab64(x)	((__u64)(					\
	(((__u64)(x) & 0x00000000000000ffULL) << 56) |			\
	(((__u64)(x) & 0x000000000000ff00ULL) << 40) |			\
	(((__u64)(x) & 0x0000000000ff0000ULL) << 24) |			\
	(((__u64)(x) & 0x00000000ff000000ULL) << 8) |			\
	(((__u64)(x) & 0x000000ff00000000ULL) >> 8) |			\
	(((__u64)(x) & 0x0000ff0000000000ULL) >> 24) |			\
	(((__u64)(x) & 0x00ff000000000000ULL) >> 40) |			\
	(((__u64)(x) & 0xff00000000000000ULL) >> 56)))
#endif
#endif
