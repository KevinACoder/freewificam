#ifndef _LINUXKPI_LINUX_SYSFS_H_
#define _LINUXKPI_LINUX_SYSFS_H_
#include "../../linuxkpi_core.h"

/* sysfs - compile-face only.  The one consumer is rtw89/usb.c, which
 * registers two read-only attributes (serial_number/uuid) on the wiphy
 * device.  Nothing walks the groups on this image, so the attribute
 * plumbing is types + the DEVICE_ATTR_RO macro shape and the group
 * visibility helper; no kobject machinery exists. */

typedef unsigned short	umode_t;

struct kobject;

struct attribute {
	const char	*name;
	umode_t		mode;
};

struct attribute_group {
	const char	*name;
	umode_t		(*is_visible)(struct kobject *,
				      struct attribute *, int);
	struct attribute	**attrs;
};

#define	SYSFS_GROUP_INVISIBLE	020000
#define	SYSFS_GROUP_VISIBLE(fn)	sysfs_group_visible_##fn

/* pairs with the driver's `<name>_group_visible(struct kobject *)` */
#define	DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(name)				\
	static inline umode_t sysfs_group_visible_##name(		\
		struct kobject *kobj, struct attribute *a, int n)	\
	{								\
		if (n == 0 && !name##_group_visible(kobj))		\
			return SYSFS_GROUP_INVISIBLE;			\
		return a->mode;						\
	}

#define	__ATTRIBUTE_GROUPS(_name)					\
	static const struct attribute_group *_name##_groups[] = {	\
		&_name##_group,						\
		NULL,							\
	}

#define	__stringify_1(x...)	#x
#define	__stringify(x...)	__stringify_1(x)

#define	__ATTR(_name, _mode, _show, _store) {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show = _show,						\
	.store = _store,					\
}

#define	__ATTR_RO(_name)					\
	{ .attr = { .name = __stringify(_name), .mode = 0444 } }

#define	__ATTR_RW(_name)					\
	__ATTR(_name, 0644, _name##_show, _name##_store)

#define	ATTR_RO(_name)						\
	struct attribute attr_##_name = __ATTR_RO(_name)

/* sysfs_emit: the show callbacks format into the page buffer.
 * A variadic function can never be an inline - real function in
 * linuxkpi_core.c (like scnprintf). */
int sysfs_emit(char *buf, const char *fmt, ...);

#endif /* _LINUXKPI_LINUX_SYSFS_H_ */
