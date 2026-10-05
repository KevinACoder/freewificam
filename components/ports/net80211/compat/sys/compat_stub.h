/*
 * @file
 * @brief compat_stub(9) hooks: the usb event-device paths call the
 * usb30 compat hooks; no compat module registers on this carrier, so
 * the objects stay unhooked (bsd_autoconf.c defines them) and the
 * calls fall to the enosys default.
 */

#ifndef _COMPAT_SYS_COMPAT_STUB_H_
#define _COMPAT_SYS_COMPAT_STUB_H_

#include <sys/cdefs.h>
#include <stdbool.h>

struct localcount;
struct usbd_device;
struct usb_device_info30;
struct usb_event;
struct uio;

#define MODULE_HOOK(hook, type, args) \
extern struct hook ## _t { \
	struct localcount *	lc; \
	type			(*f)args; \
	bool			hooked; \
} hook

#define MODULE_HOOK_CALL(hook, args, default, ret) \
	((hook).hooked ? ((ret) = (hook).f args) : ((ret) = (default)))

static inline int enosys(void) {
	return ENOSYS;
}

MODULE_HOOK(usb_subr_fill_30_hook, int,
    (struct usbd_device *, struct usb_device_info30 *, int,
      void (*)(struct usbd_device *, char *, size_t, char *, size_t, int, int),
      int (*)(char *, size_t, int)));

MODULE_HOOK(usb_subr_copy_30_hook, int,
    (struct usb_event *, struct usb_event30 *, struct uio *));

#endif /* _COMPAT_SYS_COMPAT_STUB_H_ */
