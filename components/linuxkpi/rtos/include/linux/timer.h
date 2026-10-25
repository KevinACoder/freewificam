/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/timer.h
 * @brief   The kernel-timer vocabulary a driver's watchdog and session timers are
 *          written with.
 *
 * @design declaration-only compatibility face. The timer record and every
 *         operation on it (timer_setup, mod_timer, the delete family, the pending
 *         query, from_timer) are part of the core contract every file of this face
 *         includes, and their bodies are the core's
 *         (components/linuxkpi/linuxkpi_core.c, on the CMSIS-RTOS2 callout
 *         shell). This header exists because the kernel's own spelling is what its
 *         sources include. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_TIMER_H_
#define _LINUXKPI_LINUX_TIMER_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_TIMER_H_ */
