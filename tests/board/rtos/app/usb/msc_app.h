/*
 * @file   msc_app.h
 * @brief  Application half of the mass-storage class (first-party port).
 *
 * The imported class driver matches the interface and calls these hooks; the
 * integrator implements them. This port keeps the ready device in a small
 * table so the rest of the image (and the board test rows) can reach it
 * without guessing the library's device-name convention.
 */

#ifndef FREEWIFICAM_CHERRYUSB_MSC_APP_H
#define FREEWIFICAM_CHERRYUSB_MSC_APP_H

struct usbh_msc;

/* The most recently ready disk, or 0 when none has completed its init
 * handshake. */
struct usbh_msc *usb_msc_instance(void);

/* Its device ordinal ('a' for the first disk), or -1 when none is ready. */
int usb_msc_ordinal(void);

#endif /* FREEWIFICAM_CHERRYUSB_MSC_APP_H */
