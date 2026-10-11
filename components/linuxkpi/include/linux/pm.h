/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/pm.h
 * @brief   The power-management message a device's suspend callback is handed.
 *
 * @design declaration-only compatibility face. The interface (include/linux/pm.h)
 *         carries the runtime-power-management surface - the message type the suspend
 *         and resume callbacks carry, the event numbers it encodes, the per-device
 *         power state and the dev_pm_ops table. What the compiled set names is the
 *         message type, in the driver's suspend/resume callback signatures, and it is
 *         transcribed exactly (`struct pm_message { int event; }` behind a typedef, so
 *         the callbacks' parameter type reads as the interface spells it). The event
 *         numbers, the power states and the callback table are not carried: no compiled
 *         unit names one, and the code that consumes the message (the bus-level
 *         suspend path) is the port's business when it has one. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_PM_H_
#define _LINUXKPI_LINUX_PM_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/pm.h:60-62 */
typedef struct pm_message {
	int event;
} pm_message_t;

#endif /* _LINUXKPI_LINUX_PM_H_ */
