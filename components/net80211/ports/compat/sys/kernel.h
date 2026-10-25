/*
 * @file
 * @brief Kernel-wide services: tick rate, ticks, millisecond conversion.
 *
 * The stack schedules its node timers and scan dwell in ticks; the port maps
 * ticks onto the RTOS tick (osal/cmsis2/osal_cmsis2.c) and fixes hz once, so
 * every consumer agrees on the unit.
 *
 * @design kernel-services shim: same declaration face as the component's compat
 * header; the tick source is the CMSIS-RTOS2 kernel clock.
 */

#ifndef _WLAN_SYS_KERNEL_H_
#define _WLAN_SYS_KERNEL_H_

#include <sys/types.h>

#ifndef ticks_t
typedef unsigned long ticks_t;
#endif

extern int hz;

ticks_t getticks(void);
#define mstohz(ms) ((ms) * hz / 1000)

#endif /* _WLAN_SYS_KERNEL_H_ */
