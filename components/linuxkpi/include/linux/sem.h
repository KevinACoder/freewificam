/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/sem.h
 * @brief   The include a driver's platform layer names for the System V semaphore
 *          undo API, which this project does not have.
 *
 * @design declaration-only compatibility face, deliberately empty of declarations. The
 *         interface (include/linux/sem.h) declares copy_semundo()/exit_sem() - the
 *         per-task System V semaphore-undo bookkeeping - and every one of them sits
 *         behind CONFIG_SYSVIPC, which is not a configuration this project's targets
 *         can have (there is no System V IPC, and no process to undo). Carrying a shape
 *         here would be a guess about a mechanism on neither side of the interface; the
 *         header exists because the vendored platform layer includes it by name.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SEM_H_
#define _LINUXKPI_LINUX_SEM_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_SEM_H_ */
