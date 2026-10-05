/*
 * @file
 * @brief Kernel-wide services: hz, ticks, config hooks.
 */

#ifndef _SYS_KERNEL_H_
#define _SYS_KERNEL_H_

#include <stddef.h>

#include "types.h"
#include "time.h"   /* ticks_t */

/* cast a member of a structure out to the containing structure
 * (xhci.c's pipe/slot back-pointing) */
#define container_of(ptr, type, member) \
	((type *) (void *) ((char *) (ptr) - offsetof(type, member)))

extern int hz;

ticks_t getticks(void);
#define mstohz(ms) ((ms) * hz / 1000)

enum boottime_flags { BOOTtime_NONE = 0 };

/* 1 while boot-time attach runs: drivers pick DELAY over sleeps */
extern int cold;
extern const char ostype[];
extern const char osrelease[];
extern const char version[];

#endif /* _SYS_KERNEL_H_ */
