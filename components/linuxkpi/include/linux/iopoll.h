/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/iopoll.h
 * @brief   Poll a register (or any expression) until it satisfies a condition or a
 *          timeout expires - what drivers use instead of open-coded wait loops.
 *
 * @design declaration-only compatibility face; the macro is re-derived first-party with
 *         the interface's contract (include/linux/iopoll.h): evaluate the operation,
 *         test the condition, and either sleep (polling flavour) or spin with a
 *         relaxation hint (atomic flavour) between attempts, giving up after the
 *         timeout with -ETIMEDOUT. One deliberate difference: the relaxation is a
 *         compiler barrier rather than the architecture's yield instruction, because
 *         this project keeps architecture instructions inside the arch layer
 *         (arch/aarch64/arch_asm.h) instead of letting a portable header reach into
 *         one target. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_IOPOLL_H_
#define _LINUXKPI_LINUX_IOPOLL_H_

#include "../../linuxkpi_core.h"

/* A relax hint for a spin loop: no architecture instruction, just a compiler
 * barrier, so the loop cannot be hoisted. */
static inline void cpu_relax(void)
{
	__asm__ __volatile__("" ::: "memory");
}

/*
 * read_poll_timeout(op, val, cond, sleep_us, timeout_us, sleep_before_read, args...)
 *
 * Repeat `op(args)` into `val` until `cond` holds. With `timeout_us` non-zero the
 * loop gives up after that many microseconds and returns -ETIMEDOUT; with a non-zero
 * `sleep_us` the caller may sleep and each wait is that long (the first one may be
 * skipped unless `sleep_before_read` asks otherwise).
 */
#define	read_poll_timeout(op, val, cond, sleep_us, timeout_us,		\
			  sleep_before_read, args...)			\
({									\
	u64 __timeout_us = (timeout_us);				\
	unsigned long __sleep_us = (sleep_us);				\
	ktime_t __timeout = ktime_add_us(ktime_get(), __timeout_us);	\
	int __ret;							\
									\
	might_sleep_if((__sleep_us) != 0);				\
	if (sleep_before_read && __sleep_us)				\
		usleep_range((__sleep_us >> 2) + 1, __sleep_us);	\
	for (;;) {							\
		(val) = op(args);					\
		if (cond)						\
			break;						\
		if (__timeout_us &&					\
		    ktime_compare(ktime_get(), __timeout) > 0) {	\
			(val) = op(args);				\
			break;						\
		}							\
		if (__sleep_us)						\
			usleep_range((__sleep_us >> 2) + 1, __sleep_us); \
		cpu_relax();						\
	}								\
	__ret = (cond) ? 0 : -ETIMEDOUT;				\
	__ret;								\
})

/*
 * The atomic flavour: same contract, but it must not sleep, so the wait between
 * attempts is the relaxation alone. The timeout still bounds the loop.
 */
#define	read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us,	\
				 delay_before_read, args...)		\
({									\
	u64 __timeout_us = (timeout_us);				\
	unsigned long __delay_us = (delay_us);				\
	ktime_t __timeout = ktime_add_us(ktime_get(), __timeout_us);	\
	unsigned long __delay_loops = __delay_us * 10;			\
	int __ret;							\
									\
	if (delay_before_read && __delay_us) {				\
		udelay(__delay_us);					\
		__delay_loops -= __delay_us * 10;			\
	}								\
	for (;;) {							\
		(val) = op(args);					\
		if (cond)						\
			break;						\
		if (__timeout_us &&					\
		    ktime_compare(ktime_get(), __timeout) > 0) {	\
			(val) = op(args);				\
			break;						\
		}							\
		while (__delay_loops--)					\
			cpu_relax();					\
	}								\
	__ret = (cond) ? 0 : -ETIMEDOUT;				\
	__ret;								\
})

/* The register-style spellings: the operation takes the address as its argument. */
#define	readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us)	\
	read_poll_timeout(op, val, cond, sleep_us, timeout_us, false, addr)

#define	readx_poll_timeout_atomic(op, addr, val, cond, delay_us, timeout_us) \
	read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, false, addr)

#endif /* _LINUXKPI_LINUX_IOPOLL_H_ */