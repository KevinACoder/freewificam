/*
 * @file    uvc_app.h
 * @brief   The camera cases' shared face: bring-up, the class instance and the
 *          open policy (see uvc_app.c for the contract).
 */

#ifndef FREEWIFICAM_CHERRYUSB_UVC_APP_H
#define FREEWIFICAM_CHERRYUSB_UVC_APP_H

#include <stdint.h>

struct usbh_video;

/* The controller the camera sits on (the profile's UVC_USB_BUS, default 0). */
unsigned uvc_app_bus(void);

/* The class instance the run hook captured, or 0 before enumeration. */
struct usbh_video *uvc_app_class(void);

/* Bring the controller up once and wait (60 s) for the class instance.
 * Returns 0 when the camera is known, a negative value otherwise. */
int uvc_app_ensure_up(void);

/* Open the camera at 640x480 MJPEG alt4 if it is not open yet.  Idempotent;
 * returns 0 when the camera is open, a negative value otherwise. */
int uvc_app_ensure_open(void);

#endif /* FREEWIFICAM_CHERRYUSB_UVC_APP_H */
