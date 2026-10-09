/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/leds.h
 * @brief   The LED class surface under the name a driver includes it.
 *
 * @design declaration-only compatibility face, and a pure forward: the brightness enum,
 *         the class-device record and the register/unregister pair already live in the
 *         contract header (components/linuxkpi/linuxkpi_core.h, whose LED section is the
 *         single definition of them). The interface keeps this header for the same names;
 *         this face keeps one mechanism, so the interface's name reaches the contract
 *         header and nothing is stated twice. The multicolor companion record - which the
 *         driver's device struct embeds next to the plain one - is
 *         components/linuxkpi/include/linux/led-class-multicolor.h.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_LEDS_H_
#define _LINUXKPI_LINUX_LEDS_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_LEDS_H_ */
