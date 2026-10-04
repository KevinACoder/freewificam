/*
 * @file
 * @brief Bit twiddling inlines the NetBSD headers provide.
 *
 * NetBSD declares these as static inlines on top of the compiler
 * builtins; the imported drivers (and the rtw88 compat layer, whose
 * fls() maps onto fls32()) expect exactly that, so no libkern symbols
 * are involved.
 */

#ifndef _COMPAT_SYS_BITOPS_H_
#define _COMPAT_SYS_BITOPS_H_

#include <sys/cdefs.h>
#include <sys/types.h>

__BEGIN_DECLS

/* index of the highest set bit, 1-based; 0 for an argument of 0 */
static __inline int
fls32(uint32_t v)
{

	return v == 0 ? 0 : 32 - __builtin_clz(v);
}

static __inline int
fls64(uint64_t v)
{

	return v == 0 ? 0 : 64 - __builtin_clzll(v);
}

/* index of the lowest set bit, 1-based; 0 for an argument of 0 */
static __inline int
ffs32(uint32_t v)
{

	return v == 0 ? 0 : __builtin_ctz(v) + 1;
}

static __inline int
ffs64(uint64_t v)
{

	return v == 0 ? 0 : __builtin_ctzll(v) + 1;
}

static __inline int
clz32(uint32_t v)
{

	return v == 0 ? 32 : __builtin_clz(v);
}

static __inline int
ctz32(uint32_t v)
{

	return v == 0 ? 32 : __builtin_ctz(v);
}

/* floor(log2(v)) for v != 0 */
static __inline int
ilog2(uint32_t v)
{

	return 31 - __builtin_clz(v);
}

__END_DECLS

#endif /* _COMPAT_SYS_BITOPS_H_ */
