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
/* [LNX] include/linux/device.h:33 includes <linux/cleanup.h>, which includes
 * <linux/err.h> (include/linux/cleanup.h:9). This face's cleanup.h does not carry
 * that include, so the device-model header names err.h itself: it is the path a
 * compiled consumer reaches the error-pointer helpers through (net/cfg80211.h
 * includes linux/device.h). */
#include <linux/err.h>
/* [LNX] include/linux/interrupt.h:211-215 declares the device-managed threaded
 * interrupt request over irq_handler_t; the type and the request/release pair live
 * in that header, so the devm_* declaration below reaches its handler type here. */
#include <linux/interrupt.h>

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

/* ------------------------------------------------------------------ */
/* The device-model entry points the compiled set names. Each is the  */
/* interface's own signature; the bodies are the PCI engine's         */
/* (components/linuxkpi/pci/linuxkpi_pci.c), because this port's only */
/* device model is the PCI one and the engine owns the device table.  */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/device.h:1312,1316: the manual bind/unbind pair. The
 * interface's device_release_driver unbinds a device from its driver and
 * device_reprobe re-runs the bus match; a driver's recovery path names both. */
void device_release_driver(struct device *dev);
int __must_check device_reprobe(struct device *dev);

/* [LNX] include/linux/device.h:1360-1361: the reference pair. The interface takes
 * and drops a reference on the device; this port's device records live in the
 * engine's table for the life of the image (see pci_dev_put's note), so both are
 * the identity here - the signatures are the interface's. */
struct device *get_device(struct device *dev);
void put_device(struct device *dev);

/* [LNX] include/linux/device/devres.h:93-94 (reached from the interface's
 * include/linux/device.h:31): the device-managed format-into-memory helper. */
char * __printf(3, 4)
devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...);

/* [LNX] include/linux/interrupt.h:211-215: the device-managed threaded interrupt
 * request. The interface declares it in the interrupt header; this face states it
 * with the rest of the devm_* family, because the device-managed lifetime is what
 * the call means and the interrupt header is a pinned face this round does not
 * extend. handler is the hard-irq half and thread_fn the threaded half. */
int __must_check
devm_request_threaded_irq(struct device *dev, unsigned int irq,
			  irq_handler_t handler, irq_handler_t thread_fn,
			  unsigned long irqflags, const char *devname,
			  void *dev_id);

#endif /* _LINUXKPI_LINUX_DEVICE_H_ */
