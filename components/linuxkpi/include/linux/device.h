#ifndef _LINUXKPI_LINUX_DEVICE_H_
#define _LINUXKPI_LINUX_DEVICE_H_
#include "../../linuxkpi_core.h"
#include <linux/sysfs.h>
/* [LNX] include/linux/device.h:25 includes <linux/pm.h>; the power-management table a
 * driver hangs off its bus driver record reaches a unit through it. */
#include <linux/pm.h>
/* [LNX] include/linux/device.h:24 includes <linux/mutex.h>; the device model reaches
 * the mutex vocabulary (and its static-initialiser spelling) a driver's registration
 * lines use through the device header. */
#include <linux/mutex.h>
/* [LNX] include/linux/device.h:18 includes <linux/kobject.h>; the device embeds a
 * sysfs object, and the uevent vocabulary reaches a unit through this include. */
#include <linux/kobject.h>

/* device_attribute / DEVICE_ATTR_RO: the sysfs attribute shape rtw89's
 * usb.c declares (serial_number/uuid show callbacks).  `struct device`
 * gains the `.groups` slot the driver assigns; nothing walks it. */
struct device_attribute {
	struct attribute	attr;
	ssize_t			(*show)(struct device *dev,
					struct device_attribute *attr,
					char *buf);
	ssize_t			(*store)(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count);
};

#define	__DEVICE_ATTR_RO(_name)					\
	{ .attr = { .name = __stringify(_name), .mode = 0444 },	\
	  .show = _name##_show }

#define	DEVICE_ATTR_RO(_name)					\
	struct device_attribute dev_attr_##_name = __DEVICE_ATTR_RO(_name)

#define	DEVICE_ATTR_RW(_name)					\
	struct device_attribute dev_attr_##_name = {		\
		.attr = { .name = __stringify(_name), .mode = 0644 }, \
		.show = _name##_show, .store = _name##_store }

/* the driver's group-visibility callback receives the kobject that the
 * `struct device` would embed in Linux; here the wiphy's dev IS the
 * top of that chain, so the cast is the identity */
#define	kobj_to_dev(kobj)	((struct device *)(kobj))

/* [LNX] include/linux/device.h: the bus a device sits on. The interface's record
 * is wide (the match/probe/shutdown callbacks and the device lists); this face
 * carries the name a firmware-dump line reads off the device. */
struct bus_type {
	const char	*name;
};

#endif /* _LINUXKPI_LINUX_DEVICE_H_ */
