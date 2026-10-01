#ifndef _LINUXKPI_LINUX_IOPOLL_H_
#define _LINUXKPI_LINUX_IOPOLL_H_
#include "../../linuxkpi_core.h"

/*
 * readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us, args...)
 * Busy-polls with the chip's own udelay pacing: the drivers only poll
 * registers here, never shared buses, and the timeout is the failure
 * signal ( callers warn and bail - no completion path hides behind it).
 * IMPORTANT: never call this while holding a lock another context needs;
 * the 0026 lesson (serializer held across a DLFW poll) is the reason the
 * lkpi work model keeps register polling out of net80211's serializer.
 */
#define	readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us, args...) \
({									\
	unsigned long __timeout = jiffies + usecs_to_jiffies(timeout_us); \
	for (;;) {							\
		(val) = op(args);					\
		if (cond)						\
			break;						\
		if (time_after(jiffies, __timeout)) {			\
			(val) = op(args);				\
			break;						\
		}							\
		if ((sleep_us) > 0)					\
			udelay(MIN((unsigned int)(sleep_us), 200));	\
	}								\
	(cond) ? 0 : -ETIMEDOUT;					\
})

#define	readx_poll_timeout_atomic(op, addr, val, cond, sleep_us, timeout_us, args...) \
	readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us, ##args)

#endif
