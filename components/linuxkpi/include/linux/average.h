/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/average.h
 * @brief   The exponentially weighted moving average a driver keeps of its RSSI
 *          readings.
 *
 * @design declaration-only compatibility face. The interface (include/linux/average.h)
 *         is a macro that stamps out a fixed-point accumulator on demand, and drivers
 *         rely on its exact arithmetic: the read value is the accumulator shifted back
 *         by the precision, and each sample is weighted 1/weight_rcp against the
 *         previous state. The macro is re-derived first-party with the same arithmetic
 *         and its compile-time invariants expressed through this project's BUILD_BUG_ON.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_AVERAGE_H_
#define _LINUXKPI_LINUX_AVERAGE_H_

#include "../../linuxkpi_core.h"

/*
 * DECLARE_EWMA(name, precision, weight_rcp)
 *
 * Declares `struct ewma_<name>` plus init/read/add for it. `precision` is the number
 * of fractional bits kept (an unsigned value, at most 30 so the whole part still has
 * room), and `weight_rcp` is the reciprocal of the weight given to a new sample: it
 * must be a power of two, because the update is a shift.
 */
#define	DECLARE_EWMA(name, _precision, _weight_rcp)			\
	struct ewma_##name {						\
		unsigned long internal;					\
	};								\
									\
	static inline void						\
	ewma_##name##_init(struct ewma_##name *e)			\
	{								\
		BUILD_BUG_ON(!__builtin_constant_p(_precision));	\
		BUILD_BUG_ON(!__builtin_constant_p(_weight_rcp));	\
		BUILD_BUG_ON((_precision) > 30);			\
		BUILD_BUG_ON((_weight_rcp) == 0);			\
		BUILD_BUG_ON((_weight_rcp) & ((_weight_rcp) - 1));	\
		e->internal = 0;					\
	}								\
									\
	static inline unsigned long					\
	ewma_##name##_read(struct ewma_##name *e)			\
	{								\
		BUILD_BUG_ON(!__builtin_constant_p(_precision));	\
		BUILD_BUG_ON(!__builtin_constant_p(_weight_rcp));	\
		BUILD_BUG_ON((_precision) > 30);			\
		BUILD_BUG_ON((_weight_rcp) == 0);			\
		BUILD_BUG_ON((_weight_rcp) & ((_weight_rcp) - 1));	\
		return e->internal >> (_precision);			\
	}								\
									\
	static inline void						\
	ewma_##name##_add(struct ewma_##name *e, unsigned long val)	\
	{								\
		unsigned long internal = READ_ONCE(e->internal);	\
		unsigned long weight = ilog2(_weight_rcp);		\
		unsigned long precision = _precision;			\
									\
		BUILD_BUG_ON(!__builtin_constant_p(_precision));	\
		BUILD_BUG_ON(!__builtin_constant_p(_weight_rcp));	\
		BUILD_BUG_ON((_precision) > 30);			\
		BUILD_BUG_ON((_weight_rcp) == 0);			\
		BUILD_BUG_ON((_weight_rcp) & ((_weight_rcp) - 1));	\
									\
		WRITE_ONCE(e->internal, internal ?			\
			(((internal << weight) - internal) +		\
				(val << precision)) >> weight :		\
			(val << precision));				\
	}

#endif /* _LINUXKPI_LINUX_AVERAGE_H_ */