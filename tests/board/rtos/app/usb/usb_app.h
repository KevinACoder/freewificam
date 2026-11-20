/*
 * @file   usb_app.h
 * @brief  One-time bring-up of a controller for the application halves.
 *
 * The UVC and UAC applications share one OTG_HS host.  Each image can run
 * cases from both halves (the video + microphone image does), so neither
 * application may call usb_board_init()/usbh_initialize() on its own: the
 * second call would re-initialise a live controller.  This helper is the one
 * place that brings a bus up, once per image.
 */

#ifndef FREEWIFICAM_USB_APP_H
#define FREEWIFICAM_USB_APP_H

#include <stdint.h>

/* Bring `busid` up if it is not up yet.  Idempotent; returns 0 on success. */
int usb_app_bus_ensure(unsigned busid);

/* 1 when this image has already brought `busid` up. */
int usb_app_bus_is_up(unsigned busid);

#endif /* FREEWIFICAM_USB_APP_H */