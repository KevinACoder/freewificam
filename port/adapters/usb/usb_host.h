/*
 * @file   usb_host.h
 * @brief  USB host controller interface - a CMSIS-Driver gap.
 *
 * WHY THIS EXISTS
 *
 * This image has carried two USB host stacks' worth of drivers over time and
 * will carry a third (the RTOS stacks on the roadmap).  The imported NetBSD
 * usbdi world is what the wired, working drivers talk to today; the point of
 * this header is that nobody else has to know that.  It abstracts exactly the
 * request primitives the drivers need, so a second backend can sit behind the
 * same calls and the two can be compared on identical footing - which is the
 * stated purpose of the port line, not an aesthetic.
 *
 * CMSIS-Driver ships no USB host header (the CMSIS USB host interface is for
 * device class drivers, not a host controller abstraction).  This fills the
 * gap and keeps the CMSIS idiom exactly: an ops struct of function pointers
 * plus ARM_DRIVER_VERSION, so a future ARM-defined Driver_USB.h can be
 * switched over to rather than rewritten.  Same shape as include/pcie.h.
 *
 * THE REQUEST MODEL (what the layers must agree on)
 *
 *  - A device is addressed by its index in the controller's device table
 *    (0 = root hub), not a bus/addr pair: the enumeration is the backend's
 *    business, and a caller that only wants "the third device" should not
 *    have to rebuild the address arithmetic.
 *  - Control transfers are SYNCHRONOUS here.  That is deliberate: the class
 *    drivers on this line (probe/configure, register access, firmware
 *    download) are all task-context request/response, and a synchronous
 *    primitive is the only one whose error semantics can be specified without
 *    also specifying the concurrency model of every caller.  The callback
 *    form belongs to the bulk/isoc data paths, which are NOT in this
 *    interface yet - see SCOPE below.
 *  - `buf` is ordinary caller memory (cacheable, possibly stack).  The
 *    backend owns the DMA-visible staging and the cache maintenance; the
 *    caller never sees a DMA address.  This is the one rule the two stacks
 *    differ on most, and hiding it here is why the interface is worth having:
 *    the board's USB ports are NON-COHERENT, and a stack that assumes
 *    coherence fails silently, not loudly.
 *
 * SCOPE
 *
 *  - Implemented and used today: enumeration/inspection and synchronous
 *    control transfers (the `usbreq` command is the in-tree consumer).
 *  - Not in this interface: asynchronous bulk/interrupt submission with
 *    callbacks, pipe lifecycle (open/close/abort), isochronous transfers.
 *    Isochronous is out on purpose and permanently: this port's isoc work
 *    (UVC) lives on the imported stack where the frames are proven, and the
 *    candidate alternate stack's isoc support is not open source - wiring an
 *    "isoc" entry here would promise a capability no backend can honour.
 *    Bulk/interrupt arrive with the first line that needs a data path through
 *    this seam.
 *
 * CONTRACT
 *
 *  - All calls are task-context only; none is ISR-safe, and none may be
 *    called before the platform bring-up has attached at least one bus.
 *  - PowerControl is advisory: this board's VBUS is a fixed GPIO (see
 *    usb_board.h), so a backend may implement it as a no-op that reports
 *    ARM_DRIVER_OK.  It exists so a portable caller can express port power
 *    without #ifdef'ing the line out.
 *  - GetDeviceInfo returns a snapshot; a device may be unplugged the next
 *    instant.  Callers treat a failed request as "gone", not as a bug.
 */

#ifndef FREEWEBCAMERA_USB_HOST_H
#define FREEWEBCAMERA_USB_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "Driver_Common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HOST_API_VERSION ARM_DRIVER_VERSION_MAJOR_MINOR(0, 1)

/* How many device-table slots the caller may scan.  Matches the imported
 * stack's USB_TOTAL_DEVICES (root hub + 127 addresses); a backend with a
 * smaller table simply reports fewer non-NULL slots. */
#define USB_HOST_MAX_DEVICES 128

/* Transport speeds, as they appear in a device descriptor's context. */
#define USB_HOST_SPEED_LOW   0
#define USB_HOST_SPEED_FULL  1
#define USB_HOST_SPEED_HIGH  2
#define USB_HOST_SPEED_SUPER 3

/* Default control-transfer timeout.  Same 5 s the imported stack uses for its
 * own default: long enough for a device's slowest standard request, short
 * enough that a wedged endpoint surfaces before a caller loses patience. */
#define USB_HOST_DEFAULT_TIMEOUT_MS 5000

/* Events: link transitions and request-level failures worth surfacing when
 * there is no return value to carry them (future asynchronous paths). */
#define USB_HOST_EVENT_DEVICE_CONNECT    (1UL << 0)
#define USB_HOST_EVENT_DEVICE_DISCONNECT (1UL << 1)
#define USB_HOST_EVENT_ERROR             (1UL << 2)

typedef void (*USB_HOST_SignalEvent_t)(uint32_t event, uint32_t index);

/* Status codes the control-transfer call answers with, on top of the
 * ARM_DRIVER_* family.  Every one of them is a distinct observed condition on
 * this board; "it failed" is not actionable during bring-up, so the mapping
 * from the backend's own error space keeps the distinction. */
#define USB_HOST_OK               ARM_DRIVER_OK
#define USB_HOST_ERROR            ARM_DRIVER_ERROR          /* unspecified */
#define USB_HOST_ERROR_PARAMETER  ARM_DRIVER_ERROR_PARAMETER
#define USB_HOST_ERROR_UNSUPPORTED ARM_DRIVER_ERROR_UNSUPPORTED
#define USB_HOST_ERROR_TIMEOUT    ARM_DRIVER_ERROR_TIMEOUT
#define USB_HOST_ERROR_STALL      (ARM_DRIVER_ERROR_SPECIFIC - 0) /* endpoint refused */
#define USB_HOST_ERROR_NAK        (ARM_DRIVER_ERROR_SPECIFIC - 1) /* retries exhausted */
#define USB_HOST_ERROR_IO         (ARM_DRIVER_ERROR_SPECIFIC - 2) /* bus/HC error */
#define USB_HOST_ERROR_SHORT      (ARM_DRIVER_ERROR_SPECIFIC - 3) /* short xfer, no error */

/* A control request, the USB spec's setup packet plus the direction implied
 * by bmRequestType.  Same field names as the spec and the imported stack's
 * usb_device_request_t, so a caller porting between them reads the same
 * names for the same things. */
typedef struct _USB_HOST_REQUEST {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} USB_HOST_REQUEST;

/* Device snapshot, filled by GetDeviceInfo.  vendor/product are the
 * descriptor's idVendor/idProduct (no string lookup - strings need a request
 * of their own and are not part of a cheap enumeration walk). */
typedef struct _USB_HOST_DEVICE {
    uint8_t  index;              /* slot this snapshot came from */
    uint8_t  address;            /* USB address on the bus */
    uint8_t  speed;              /* USB_HOST_SPEED_* */
    uint8_t  device_class;
    uint16_t vendor;
    uint16_t product;
    uint16_t bcd_device;
    uint8_t  num_configurations;
    bool     configured;
} USB_HOST_DEVICE;

typedef struct _USB_HOST_CAPABILITIES {
    uint8_t  max_devices;        /* entries in the device table */
    uint8_t  high_speed;         /* controller can do 480 Mb/s */
    uint8_t  super_speed;        /* controller can do 5 Gb/s */
    uint8_t  isochronous;        /* reserved: 0 in every backend of this API */
} USB_HOST_CAPABILITIES;

typedef struct _ARM_DRIVER_USB_HOST {
    ARM_DRIVER_VERSION (*GetVersion)(void);
    USB_HOST_CAPABILITIES (*GetCapabilities)(void);

    int32_t (*Initialize)(USB_HOST_SignalEvent_t cb_event);
    int32_t (*Uninitialize)(void);
    int32_t (*PowerControl)(uint32_t index, uint32_t state);

    /* Device table walk.  GetDeviceCount answers "how many slots are
     * populated right now"; the caller iterates 0..count-1 and calls
     * GetDeviceInfo, which fails (ARM_DRIVER_ERROR_PARAMETER) on an empty or
     * out-of-range slot. */
    int32_t (*GetDeviceCount)(uint32_t *count);
    int32_t (*GetDeviceInfo)(uint32_t index, USB_HOST_DEVICE *info);

    /* Synchronous control transfer on the device's default pipe.
     *
     *   index   device slot (0 = root hub)
     *   req     setup packet; wLength is the payload length
     *   data    caller buffer (in or out per bmRequestType direction bit)
     *   actlen  out: bytes actually transferred (may be NULL)
     *   timeout_ms  0 = no timeout; the backend's default otherwise
     *
     * Returns USB_HOST_OK, USB_HOST_ERROR_SHORT (transfer completed but
     * shorter than requested, USBD_SHORT_XFER_OK semantics), or one of the
     * error codes above.  On any error actlen is 0. */
    int32_t (*ControlTransfer)(uint32_t index, const USB_HOST_REQUEST *req,
                               void *data, uint32_t *actlen,
                               uint32_t timeout_ms);
} ARM_DRIVER_USB_HOST;

#ifdef __cplusplus
}
#endif

#endif /* FREEWEBCAMERA_USB_HOST_H */
