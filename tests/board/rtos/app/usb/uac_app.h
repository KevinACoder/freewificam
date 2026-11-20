/*
 * @file   uac_app.h
 * @brief  Application half of the first-party UAC1 microphone class.
 *
 * The class driver (third-party/cherryusb/class/audio/usbh_uac.c) owns the
 * descriptor parse and the class requests; this file owns the board policy:
 * which controller, which sample rate, which alternate setting, and the
 * census/capture lines the cases print.  The streaming interface is left at
 * its alternate setting after a capture (the camera family can refuse class
 * requests after a leave), so open is idempotent across cases.
 */

#ifndef FREEWIFICAM_UAC_APP_H
#define FREEWIFICAM_UAC_APP_H

#include <stdint.h>

struct usbh_uac;

/* The controller the camera sits on: the F429 has one OTG_HS host. */
unsigned uac_app_bus(void);

/* The class instance the run hook captured, or 0 before enumeration. */
struct usbh_uac *uac_app_class(void);

/* Bring the controller up once (shared with the UVC half) and wait 60 s for
 * the audio class instance. */
int uac_app_ensure_up(void);

/* Print the descriptor census: the one-line summary, the terminal/feature
 * unit line and the raw AC/AS class-specific descriptors. */
void uac_app_descriptor_dump(void);

/* Open the streaming interface at the profile's target rate (or the device's
 * first rate when the target is absent), picking the smallest alternate
 * setting whose endpoint carries the rate's bytes per millisecond.  Idempotent;
 * returns 0 when open. */
int uac_app_ensure_open(void);

/* Read the feature unit's mute state and clear it when set.  Returns 1 when
 * this call unmuted the microphone, 0 when it was already unmuted or has no
 * mute control, -1 when the control request failed. */
int uac_app_unmute(void);

#endif /* FREEWIFICAM_UAC_APP_H */