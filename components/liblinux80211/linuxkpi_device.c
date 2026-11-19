/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_device.c
 * @brief   The device-model bodies cfg80211's wiphy registration calls.
 *
 * @design this port has no driver core, no sysfs and no devtmpfs.  A `struct
 *         device` here is a name plus links (parent/class/bus), and the entry
 *         points below are the interface's signatures with the answers of a
 *         system that has no user-visible device tree: initialize/register
 *         succeed, names are recorded, and the sysfs/link calls do nothing.
 *
 *         That is enough for cfg80211's registration path, which uses the
 *         device as an anchor for `dev_name(&wiphy->dev)` and for error
 *         messages, not as a sysfs object.  A caller that needs a real kobject
 *         (a userspace-visible attribute) would find nothing here; that is a
 *         recorded divergence, not a hidden success.
 * @date    10.11.2026
 */

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/device/faux.h>
#include <linux/sysfs.h>

/* [LNX] drivers/base/core.c: the name is allocated and owned by the device
 * model.  This port's devices have image lifetime, so the name is a second
 * allocation that is never freed (the device is never destroyed either); the
 * few callers are the wiphy and the regulatory faux device. */
/* The name is a second allocation with image lifetime; kstrdup is not part of
 * this component's face, so the copy is local. */
static char *
lkpi_dev_strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = kmalloc(n, GFP_KERNEL);

	if (p != NULL)
		memcpy(p, s, n);
	return p;
}

int
dev_set_name(struct device *dev, const char *fmt, ...)
{
	char buf[64];
	va_list ap;
	int n;

	if (dev == NULL || fmt == NULL)
		return -EINVAL;
	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return -EINVAL;
	buf[sizeof(buf) - 1] = '\0';
	dev->name = lkpi_dev_strdup(buf);
	return dev->name != NULL ? 0 : -ENOMEM;
}

void
device_initialize(struct device *dev)
{
	/* The device record is caller-allocated (usually zeroed); the model only
	 * has to keep whatever parent/class the caller already set. */
	(void)dev;
}

int
device_add(struct device *dev)
{
	if (dev == NULL)
		return -EINVAL;
	if (dev->name == NULL) {
		int r = dev_set_name(dev, "device%u", 0U);

		if (r != 0)
			return r;
	}
	return 0;
}

void
device_del(struct device *dev)
{
	(void)dev;
}

int
device_rename(struct device *dev, const char *new_name)
{
	if (dev == NULL || new_name == NULL)
		return -EINVAL;
	dev->name = lkpi_dev_strdup(new_name);
	return dev->name != NULL ? 0 : -ENOMEM;
}

void
device_enable_async_suspend(struct device *dev)
{
	(void)dev;
}

void
dev_set_uevent_suppress(struct device *dev, int val)
{
	(void)dev;
	(void)val;
}

int
class_register(const struct class *cls)
{
	(void)cls;
	return 0;
}

void
class_unregister(const struct class *cls)
{
	(void)cls;
}

int
sysfs_create_link(struct kobject *kobj, struct kobject *target, const char *name)
{
	(void)kobj;
	(void)target;
	(void)name;
	return 0;
}

void
sysfs_remove_link(struct kobject *kobj, const char *name)
{
	(void)kobj;
	(void)name;
}

/* [LNX] drivers/base/faux.c: a device that exists only to hang data off.  The
 * regulatory core creates one to own the regdb firmware request. */
struct faux_device *
faux_device_create(const char *name, struct device *parent,
		   const struct faux_device_ops *faux_ops)
{
	return faux_device_create_with_groups(name, parent, faux_ops, NULL);
}

struct faux_device *
faux_device_create_with_groups(const char *name, struct device *parent,
			       const struct faux_device_ops *faux_ops,
			       const struct attribute_group **groups)
{
	struct faux_device *faux;
	int r;

	faux = kzalloc(sizeof(*faux), GFP_KERNEL);
	if (faux == NULL)
		return NULL;
	device_initialize(&faux->dev);
	faux->dev.parent = parent;
	faux->dev.groups = groups;
	r = dev_set_name(&faux->dev, "%s", name != NULL ? name : "faux");
	if (r != 0) {
		kfree(faux);
		return NULL;
	}
	if (faux_ops != NULL && faux_ops->probe != NULL &&
	    faux_ops->probe(faux) != 0) {
		kfree(faux);
		return NULL;
	}
	return faux;
}

void
faux_device_destroy(struct faux_device *faux_dev)
{
	if (faux_dev == NULL)
		return;
	if (faux_dev->dev.name != NULL)
		kfree(faux_dev->dev.name);
	kfree(faux_dev);
}

/* [LNX] net/core/net_namespace.c: the network namespace type's operations.  This
 * port has one namespace (init_net) and no kobject namespace switching; the
 * record exists because wiphy_class.ns_type points at it. */
const struct kobj_ns_type_operations net_ns_type_operations = {
	.type = KOBJ_NS_TYPE_NET,
};

/* [LNX] fs/debugfs/inode.c: there is no debugfs in this image; the creation
 * side is an inline NULL in the face, so the removal side matches it. */
void
debugfs_remove(struct dentry *dentry)
{
	(void)dentry;
}

/* [LNX] drivers/base/core.c: the reference drop.  This port's device records
 * have image lifetime, so the drop is the identity, exactly like get_device().
 * It lives here rather than in the PCI engine because the stack names it in
 * images that carry no PCI engine (C2). */
void
put_device(struct device *dev)
{
	(void)dev;
}

/* ------------------------------------------------------------------ */
/* runtime power management                                            */
/* ------------------------------------------------------------------ */

/*
 * [LNX] include/linux/pm_runtime.h:84-85 (drivers/base/power/runtime.c's
 * pm_runtime_allow/pm_runtime_forbid): the user-space suspend permission switch,
 * which in the interface increments or decrements the device's runtime-PM
 * disable count. brcmfmac calls forbid() around its SDIO probe (bcmsdh.c:895,
 * "runtime-pm powers off the device") and allow() when the bus goes away
 * (bcmsdh.c:888).
 *
 * This port has no runtime power management: nothing ever asks a device to
 * suspend, and the device record carries no disable count (linuxkpi_core.h's
 * struct dev_pm_info is the one `is_prepared` flag a cfg80211 consumer reads).
 * Both calls are therefore empty - there is no suspend opportunity to forbid and
 * no count to move. They are not a success answer to anything: the interface
 * declares them void, and the only effect they have upstream is to change a state
 * machine this image does not run.
 */
void
pm_runtime_allow(struct device *dev)
{
	(void)dev;
}

void
pm_runtime_forbid(struct device *dev)
{
	(void)dev;
}

/* ------------------------------------------------------------------ */
/* the platform bus                                                    */
/* ------------------------------------------------------------------ */

/*
 * [LNX] include/linux/platform_device.h:307-315 (drivers/base/platform.c): the
 * platform bus registers the driver and then tries to bind it to the devices the
 * board described statically. This port has no platform bus and no such devices
 * (its one device model is the PCI engine; the SDIO card is enumerated by the
 * port's own engine), so the probe request has nothing to match against. The
 * answer is the interface's own "no device" value, -ENODEV, which is exactly what
 * brcmfmac's common.c:610 tests for before it prints "No platform data
 * available." and continues with module parameters.
 *
 * Nothing was registered, so the unregister call has nothing to undo; it is empty
 * rather than a fake teardown. The one caller (common.c:621,631) only reaches it
 * when a platform-data probe succeeded, which cannot happen here.
 */
int
__platform_driver_probe(struct platform_driver *driver,
			int (*probe)(struct platform_device *),
			struct module *module, const char *modname)
{
	(void)driver;
	(void)probe;
	(void)module;
	(void)modname;
	return -ENODEV;
}

void
platform_driver_unregister(struct platform_driver *drv)
{
	(void)drv;
}

/*
 * [LNX] drivers/base/dd.c: the unbind entry.  This port's device model has no
 * driver core to release a device from: a device is bound once by the SDIO /
 * PCI / USB engine's probe and never hot-unplugged, so the remove path's
 * "release the current driver" has nothing to do.  brcmfmac names it in the
 * bus-remove and firmware-callback failure arms, which an image does not run.
 */
void
device_release_driver(struct device *dev)
{
	(void)dev;
}
