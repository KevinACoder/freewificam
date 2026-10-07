/*
 * @file
 * @brief USB host controller glue shell.
 *
 * The imported drivers only reference the opaque object types and the
 * request constants; every usbd_* operation is implemented by the port
 * over its host stack.
 */

#ifndef _COMPAT_DEV_USB_USBDIVAR_H_
#define _COMPAT_DEV_USB_USBDIVAR_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

struct usbd_bus;
struct usbd_device;
struct usbd_interface;
struct usbd_pipe;
struct usbd_xfer;

/* Host task queue identifiers (usbdi.h usb_task.queue values). */
#define USB_TASKQ_GENERIC 0
#define USB_TASKQ_DRIVER 1
#define USB_NUM_TASKQS 2

typedef void (*usb_taskq_fn)(struct usb_task *);

usbd_status usbd_delay_ms(struct usbd_device *, unsigned int);

/*
 * Port addition: NetBSD keeps the speed in the usbd_device it hands the
 * driver (udev->ud_speed), but the shell here is private to the shim, so
 * the one field drivers branch on is exported as an accessor instead.
 * The value uses the USB_SPEED_* encoding of usb.h.
 */
uint8_t usbd_get_speed(struct usbd_device *);

int usb_add_task_init(void);
void usb_add_task_fini(void);

#endif /* _COMPAT_DEV_USB_USBDIVAR_H_ */
