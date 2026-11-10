/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/wait.h
 * @brief   The wait-queue object and the wait-for-condition entry points a driver
 *          blocks on.
 *
 * @design declaration-only compatibility face. The wait-queue record and the
 *         wait_event family are part of the core contract every file of this face
 *         includes, and their bodies are the core's
 *         (components/linuxkpi/linuxkpi_core.c, on the CMSIS-RTOS2 condition
 *         shell). This header exists because the kernel's own spelling is what its
 *         sources include, so a consumer that reaches struct wait_queue_head,
 *         wait_event() or wait_event_timeout() through it names the same
 *         declarations the core publishes. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_WAIT_H_
#define _LINUXKPI_LINUX_WAIT_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_WAIT_H_ */
