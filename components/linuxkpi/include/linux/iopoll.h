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
 * attempts is a busy delay.  The budget accounting is the interface's: only the
 * delay is charged against the timeout, never the operation's own execution
 * time (upstream include/linux/iopoll.h poll_timeout_us_atomic: the budget is
 * `__left_ns -= __delay_ns` per round plus one nanosecond, and the expiry is
 * decided from the value the previous round left).  Charging wall-clock time
 * here made a slow register read look like an expired budget: a real answer
 * that took longer than the window was reported as -ETIMEDOUT (the rtw89 W2
 * board round read a C2H answer in 5.2-6.1 ms against a 4 ms window and lost
 * it).  The expiry is read before the operation and the operation runs before
 * the decision, so the last round still gets its answer.
 */
#define	read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us,	\
				 delay_before_read, args...)		\
({									\
	u64 __timeout_us = (timeout_us);				\
	s64 __left_ns = (s64)__timeout_us * 1000LL;			\
	unsigned long __delay_us = (delay_us);				\
	s64 __delay_ns = (s64)__delay_us * 1000LL;			\
	int __ret;							\
									\
	if (delay_before_read && __delay_us) {				\
		udelay(__delay_us);					\
		if (__timeout_us)					\
			__left_ns -= __delay_ns;			\
	}								\
	for (;;) {							\
		int __expired = __timeout_us && __left_ns < 0;		\
									\
		cpu_relax();	/* the interface's barrier() */		\
		(val) = op(args);					\
		if (cond) {						\
			__ret = 0;					\
			break;						\
		}							\
		if (__expired) {					\
			__ret = -ETIMEDOUT;				\
			break;						\
		}							\
		if (__delay_us) {					\
			udelay(__delay_us);				\
			if (__timeout_us)				\
				__left_ns -= __delay_ns;		\
		}							\
		cpu_relax();						\
		if (__timeout_us)					\
			__left_ns--;					\
	}								\
	__ret;								\
})

/* The register-style spellings: the operation takes the address as its argument. */
#define	readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us)	\
	read_poll_timeout(op, val, cond, sleep_us, timeout_us, false, addr)

#define	readx_poll_timeout_atomic(op, addr, val, cond, delay_us, timeout_us) \
	read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, false, addr)

#endif /* _LINUXKPI_LINUX_IOPOLL_H_ */