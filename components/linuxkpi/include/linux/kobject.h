/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/kobject.h
 * @brief   The sysfs object and the uevent call a driver's firmware-event path
 *          names.
 *
 * @design declaration-only compatibility face. The interface (include/linux/
 *         kobject.h) owns the whole sysfs object model; the sysfs object a device
 *         embeds is already carried by the core contract
 *         (components/linuxkpi/linuxkpi_core.h, its device record). What this
 *         header adds is the uevent-action enumeration and the one entry point a
 *         compiled consumer names, whose body belongs to the driver core and has
 *         no counterpart on this port yet. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_KOBJECT_H_
#define _LINUXKPI_LINUX_KOBJECT_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/kobject.h:22 (the action the compiled set names, at its
 * computed position). */
enum kobject_action {
	KOBJ_ADD	= 0,
	KOBJ_REMOVE	= 1,
	KOBJ_CHANGE	= 2,
};

/* [LNX] include/linux/kobject.h: the uevent emitter. */
int kobject_uevent_env(struct kobject *kobj, enum kobject_action action,
		       char *envp[]);

#endif /* _LINUXKPI_LINUX_KOBJECT_H_ */
