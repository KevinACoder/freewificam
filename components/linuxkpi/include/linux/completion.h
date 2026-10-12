/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/completion.h
 * @brief   The completion object a driver embeds to wait for an event another
 *          context signals.
 *
 * @design declaration-only compatibility face. The completion record and every
 *         operation on it (init/reinit, complete/complete_all, the wait family,
 *         the try/done queries) are part of the core contract every file of this
 *         face includes, and the bodies are the core's
 *         (components/linuxkpi/linuxkpi_core.c, on the CMSIS-RTOS2 condition
 *         shell). This header exists because the kernel's own spelling is what
 *         its sources include, so a consumer that reaches struct completion,
 *         init_completion() or wait_for_completion() through it names the same
 *         declarations the core publishes. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_COMPLETION_H_
#define _LINUXKPI_LINUX_COMPLETION_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_COMPLETION_H_ */
