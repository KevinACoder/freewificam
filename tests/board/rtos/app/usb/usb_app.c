/*
 * @file   usb_app.c
 * @brief  One-time bring-up of a USB host controller for the application
 *         halves (UVC video, UAC microphone) that share it.
 *
 * The board glue owns the controller's power/pins/IRQ; the imported stack
 * owns the hub thread.  This file only makes the sequence idempotent across
 * the cases of one image: the first caller initialises, the rest observe.
 */

#include <stdint.h>

#include "usb_glue.h"

#include "usbh_core.h"

#include "usb_app.h"

static uint32_t usb_app_buses;

int usb_app_bus_ensure(unsigned busid)
{
	if (busid >= 32U) {
		return -1;
	}
	if ((usb_app_buses & (1UL << busid)) != 0U) {
		return 0;
	}
	if (usb_board_init(busid) != 0) {
		return -1;
	}
	if (usbh_initialize((uint8_t)busid, usb_board_reg_base(busid),
			    usb_board_event_handler) != 0) {
		return -1;
	}
	usb_board_after_start((uint8_t)busid);
	usb_app_buses |= (1UL << busid);
	return 0;
}

int usb_app_bus_is_up(unsigned busid)
{
	if (busid >= 32U) {
		return 0;
	}
	return (usb_app_buses & (1UL << busid)) != 0U;
}