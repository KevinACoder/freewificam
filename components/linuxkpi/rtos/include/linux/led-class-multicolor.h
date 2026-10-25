/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/led-class-multicolor.h
 * @brief   The multicolor LED record a driver embeds beside the plain LED class device.
 *
 * @design declaration-only compatibility face. The two records are transcribed with the
 *         interface's member order and types (include/linux/led-class-multicolor.h): the
 *         driver's device struct embeds the class-device record by value, and the
 *         sub-LED table is data it submits by value, so the layout is contract. The
 *         colour-component calculation entry point is not carried - nothing the compiled
 *         set names calls it, and it arrives with the consumer that does.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_LED_CLASS_MULTICOLOR_H_
#define _LINUXKPI_LINUX_LED_CLASS_MULTICOLOR_H_

#include "../../linuxkpi_core.h"
#include <linux/leds.h>

/* [LNX] include/linux/led-class-multicolor.h:32-38 */
struct mc_subled {
	unsigned int	color_index;
	unsigned int	brightness;
	unsigned int	intensity;
	unsigned int	max_intensity;
	unsigned int	channel;
};

/* [LNX] include/linux/led-class-multicolor.h:40-47 */
struct led_classdev_mc {
	/* led class device */
	struct led_classdev	led_cdev;
	unsigned int		num_colors;

	struct mc_subled	*subled_info;
};

#endif /* _LINUXKPI_LINUX_LED_CLASS_MULTICOLOR_H_ */
