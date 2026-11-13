/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_usbh_class.h
 * @brief   The class hook's observable face: which hubport/interface a driver
 *          currently holds, so diagnostics and tests can act on the same claim
 *          the drivers were probed against.
 *
 * The getters must be declared where their callers can see the real prototype:
 * without one, a call site defaults to an int return and the hubport pointer
 * truncates on a 64-bit target - a defect this header exists to prevent.
 */

#ifndef LKPI_USBH_CLASS_H
#define LKPI_USBH_CLASS_H

#include <stdint.h>

struct usbh_hubport;

/* The hubport a usb_driver currently claimed (NULL while none), and the
 * interface index within it. */
struct usbh_hubport	*lkpi_usbh_claimed_hport(void);
uint8_t			lkpi_usbh_claimed_intf(void);

#endif /* LKPI_USBH_CLASS_H */
