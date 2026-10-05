#ifndef _LINUXKPI_LINUX_AVERAGE_H_
#define _LINUXKPI_LINUX_AVERAGE_H_
#include "../../linuxkpi_core.h"

#ifndef READ_ONCE
#define	READ_ONCE(x)		(x)
#endif
#ifndef WRITE_ONCE
#define	WRITE_ONCE(x, v)	do { (x) = (v); } while (0)
#endif

/* DECLARE_EWMA(name, precision, frac_bits) - Linux's fixed-point EWMA.
 * The internal value is (value << precision); each add() mixes in
 * 1/2^frac_bits of the new sample. */
#define	DECLARE_EWMA(name, _precision, _frac)				\
struct ewma_##name {							\
	unsigned long	internal;					\
};									\
static __always_inline __unused void					\
ewma_##name##_init(struct ewma_##name *e)				\
{									\
	e->internal = 0;						\
}									\
static __always_inline __unused unsigned long				\
ewma_##name##_read(struct ewma_##name *e)				\
{									\
	return READ_ONCE(e->internal) >> (_precision);			\
}									\
static __always_inline __unused void					\
ewma_##name##_add(struct ewma_##name *e, unsigned long val)		\
{									\
	unsigned long internal = READ_ONCE(e->internal);		\
									\
	internal = internal ?						\
	    (((internal << (_frac)) - internal +			\
	      (val << (_precision))) >> (_frac)) :			\
	    (val << (_precision));					\
	WRITE_ONCE(e->internal, internal);				\
}

#endif
