/*
 * @file   uvc_app.h
 * @brief  Application half of the CherryUSB video class on the F429.
 *
 * The imported class driver owns matching and the control/URB transport; this
 * file owns the board policy: which controller to bring up, which camera mode
 * to ask for, and how the four camera cases share one class instance.  The
 * camera is never closed after a stream (the old line's Logitech refused the
 * video-streaming requests for the rest of the boot after a stop); a stop
 * leaves it in its streaming setting.
 */

#ifndef FREEWIFICAM_CHERRYUSB_UVC_APP_H
#define FREEWIFICAM_CHERRYUSB_UVC_APP_H

#include <stdint.h>

struct usbh_video;

/* The controller the camera sits on: the F429 has one OTG_HS host. */
unsigned uvc_app_bus(void);

/* The class instance the run hook captured, or 0 before enumeration. */
struct usbh_video *uvc_app_class(void);

/* Bring the controller up once and wait (60 s) for the class instance. */
int uvc_app_ensure_up(void);

/* Print the camera's format/frame table, the streaming interface's alternate
 * settings, and the raw frame descriptors' interval tables.  Used by
 * uvc_detect; it does not open or commit the camera. */
void uvc_app_mode_dump(void);

/* Probe the 720-line modes (and the profile target) without committing them:
 * for every candidate interval ask the camera for its required payload
 * (dwMaxPayloadTransferSize) and compare it with the streaming interface's
 * full-speed endpoint bound.  This is the 720p feasibility census. */
void uvc_app_probe_census(void);

/* Open the camera at CONFIG_UVC_TARGET_WIDTH/HEIGHT MJPEG, picking the
 * smallest alternate setting that carries the camera's requested payload.
 * Idempotent; returns 0 when open, a negative value otherwise. */
int uvc_app_ensure_open(void);

#endif /* FREEWIFICAM_CHERRYUSB_UVC_APP_H */
