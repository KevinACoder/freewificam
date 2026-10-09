/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/usb/ch9.h
 * @brief   The include a driver names for the USB chapter 9 types; in this face those
 *          types are stated in linux/usb.h, which the compiled set includes first.
 *
 * @design declaration-only compatibility face with one indirection, and the reason is
 *         the single-definition rule: the interface (include/linux/usb/ch9.h) is where
 *         the descriptor structs and the standard request constants live, and this
 *         component's USB face states exactly those in linux/usb.h - the header the
 *         vendored platform layer includes immediately before this one - because the
 *         same definitions also have to be guarded against the USB stack's own
 *         descriptor header (see the USB_DEF_H block there). Restating them here would
 *         be the second definition of one wire format; this header exists so a source
 *         that includes the interface's ch9 spelling reaches that same set, and it does
 *         so by including the face's USB header. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_USB_CH9_H_
#define _LINUXKPI_LINUX_USB_CH9_H_

#include "../../../linuxkpi_core.h"

#include <linux/usb.h>

#endif /* _LINUXKPI_LINUX_USB_CH9_H_ */
