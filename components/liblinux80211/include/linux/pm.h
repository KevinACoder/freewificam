/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/pm.h
 * @brief   The power-management message a device's suspend callback is handed.
 *
 * @design declaration-only compatibility face. The interface (include/linux/pm.h)
 *         carries the runtime-power-management surface - the message type the suspend
 *         and resume callbacks carry, the event numbers it encodes, the per-device
 *         power state and the dev_pm_ops table. What the compiled set names is the
 *         message type, in the driver's suspend/resume callback signatures, and the
 *         dev_pm_ops callback table a bus driver hangs off its driver record; both are
 *         transcribed exactly (`struct pm_message { int event; }` behind a typedef, so
 *         the callbacks' parameter type reads as the interface spells it, and the
 *         callback table member for member). The event numbers and the per-device power
 *         state are not carried: no compiled unit names one, and the code that consumes
 *         them (the bus-level suspend path) is the port's business when it has one.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_PM_H_
#define _LINUXKPI_LINUX_PM_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/pm.h:60-62 */
typedef struct pm_message {
	int event;
} pm_message_t;

/*
 * [LNX] include/linux/pm.h: the device power-management operation table a driver hangs
 * off its bus driver record. The compiled set defines one with `.suspend` and
 * `.resume`; the whole table is carried because it is a fixed callback record, not a
 * stateful one, and a partial copy would renumber the members after the cut.
 */
struct device;

struct dev_pm_ops {
	int (*prepare)(struct device *dev);
	void (*complete)(struct device *dev);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
	int (*freeze)(struct device *dev);
	int (*thaw)(struct device *dev);
	int (*poweroff)(struct device *dev);
	int (*restore)(struct device *dev);
	int (*suspend_late)(struct device *dev);
	int (*resume_early)(struct device *dev);
	int (*freeze_late)(struct device *dev);
	int (*thaw_early)(struct device *dev);
	int (*poweroff_late)(struct device *dev);
	int (*restore_early)(struct device *dev);
	int (*suspend_noirq)(struct device *dev);
	int (*resume_noirq)(struct device *dev);
	int (*freeze_noirq)(struct device *dev);
	int (*thaw_noirq)(struct device *dev);
	int (*poweroff_noirq)(struct device *dev);
	int (*restore_noirq)(struct device *dev);
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
};

/*
 * [LNX] include/linux/pm.h:1483 `SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn)`:
 * the shorthand a driver uses when it has exactly the two system-sleep
 * callbacks and nothing else. It DEFINES the table - the interface's macro
 * expands to `const struct dev_pm_ops name = { ... }` - so a driver that ends its
 * suspend section with it owns the object, while its header only declares it.
 *
 * That difference is why this macro is not optional: rtw88/sdio.c:1122 defines
 * `rtw_sdio_pm_ops` this way while sdio.h declares the same object extern and
 * rtw8821cs.c:33 takes its address for the driver record's `.drv.pm`. Without the
 * macro the definition is silently absent (the compiler only warns), and the
 * address becomes an undefined symbol at link time.
 *
 * The interface's expansion also wraps the table in `pm_ptr()`, which is the
 * "make this table disappear when PM is off" annotation. This port has no
 * CONFIG_PM to switch on and no section for the pointer to land in, so the
 * table is defined unconditionally - the same object the interface produces
 * with PM enabled, which is the build these images are.
 */
#define	SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn)			\
	const struct dev_pm_ops name = {				\
		.suspend = suspend_fn,					\
		.resume = resume_fn,					\
	}

#endif /* _LINUXKPI_LINUX_PM_H_ */
