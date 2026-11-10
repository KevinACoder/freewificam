/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/init.h
 * @brief   The section-annotation macros a driver's init and exit functions wear.
 *
 * @design declaration-only compatibility face. The annotations exist so a function lands
 *         in the right linker section of a Linux kernel image; this project links one
 *         flat image and frees nothing, so every annotation of the interface's init
 *         header is inert here. The two the core already carries (__init, __exit) are not
 *         repeated; the rest is stated once, here, because sources include this header
 *         directly and use them. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_INIT_H_
#define _LINUXKPI_LINUX_INIT_H_

#include "../../linuxkpi_core.h"

/*
 * The initcall levels.  Linux orders built-in initialisation by level
 * (early/core/postcore/arch/subsys/fs/device/late) and a driver or a stack
 * unit lands its entry point with one of these spellings.  This port runs a
 * single flat walk: `lkpi_module_init_run()` (linuxkpi_core.c) executes the
 * `.lkpi_init` section once, in link order.  Mapping every level to
 * `module_init` keeps the source unchanged and makes the order a property of
 * the source list (`sources.mk` / the image's link order), which is visible
 * and auditable; it is not the kernel's level ordering.  The two levels the
 * imported stack names are `fs_initcall(cfg80211_init)` and
 * `subsys_initcall(ieee80211_init)`; `late_initcall` carries cfg80211's
 * regulatory-db loader.
 */
#define	early_initcall(fn)	module_init(fn)
#define	core_initcall(fn)	module_init(fn)
#define	postcore_initcall(fn)	module_init(fn)
#define	arch_initcall(fn)	module_init(fn)
#define	subsys_initcall(fn)	module_init(fn)
#define	fs_initcall(fn)		module_init(fn)
#define	device_initcall(fn)	module_init(fn)
#define	late_initcall(fn)	module_init(fn)

#ifndef __initdata
#define	__initdata
#endif
#ifndef __initconst
#define	__initconst
#endif
#ifndef __exitdata
#define	__exitdata
#endif
#ifndef __meminit
#define	__meminit
#endif
#ifndef __read_mostly
#define	__read_mostly
#endif
#ifndef __ro_after_init
#define	__ro_after_init
#endif

#endif /* _LINUXKPI_LINUX_INIT_H_ */
