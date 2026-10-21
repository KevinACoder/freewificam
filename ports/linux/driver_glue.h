/*
 * @file
 * @brief Driver registration glue for the kernelmod line's single module.
 *
 * One module carries the stack, the bridge, the mac80211 substitute and the
 * verbatim rtl8xxxu driver. The driver source ends with
 *     module_usb_driver(rtl8xxxu_driver);
 * and a module can define init_module/cleanup_module only once, so that macro is
 * redirected here: it keeps the registration/deregistration bodies but exposes
 * them as plain functions. The module entry calls wlan_driver_register() after
 * its own setup and wlan_driver_unregister() on exit. The driver's source bytes
 * are untouched; only the kernel-header macro expansion changes.
 *
 * The header is force-included (-include) for the driver translation units, and
 * it includes <linux/usb.h> first so the source's later include is the guarded
 * no-op and this definition survives.
 */

#ifndef _WLAN_LINUX_DRIVER_GLUE_H_
#define _WLAN_LINUX_DRIVER_GLUE_H_

#include <linux/usb.h>

#undef module_usb_driver
#define module_usb_driver(__usb_driver)					\
	static int __init __usb_driver##_init(void)			\
	{								\
		return usb_register_driver(&(__usb_driver), THIS_MODULE,	\
		    KBUILD_MODNAME);					\
	}								\
	static void __exit __usb_driver##_exit(void)			\
	{								\
		usb_deregister(&(__usb_driver));				\
	}								\
	int wlan_driver_register(void)					\
	{								\
		return __usb_driver##_init();				\
	}								\
	void wlan_driver_unregister(void)				\
	{								\
		__usb_driver##_exit();					\
	}

#endif /* _WLAN_LINUX_DRIVER_GLUE_H_ */
