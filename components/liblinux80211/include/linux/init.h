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
