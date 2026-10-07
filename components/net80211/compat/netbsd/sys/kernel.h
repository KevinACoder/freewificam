/*
 * @file
 * @brief Kernel-wide services: hz, ticks, config hooks.
 */

#ifndef _SYS_KERNEL_H_
#define _SYS_KERNEL_H_

#include "types.h"

extern int hz;

ticks_t getticks(void);
#define mstohz(ms) ((ms) * hz / 1000)

enum boottime_flags { BOOTtime_NONE = 0 };

#endif /* _SYS_KERNEL_H_ */
