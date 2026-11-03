/*
 * @file   usb_glue.h
 * @brief  Board glue between the imported USB host stack and the firefly v2
 *         board's controller facts (port interface).
 *
 * The same contract as the RK3568 board's usb_glue.h: this layer binds one
 * host controller instance per bus (driver registration, interrupt arming,
 * register-base queries) and implements the driver-facing symbols the
 * imported controller drivers require. The controller facts themselves
 * live in platform/board/firefly/v2/usb/usb_domain.h; this board has no
 * USB domain bring-up sequence of its own (the firmware leaves the xHCI
 * instances running - see that header).
 */

#ifndef FREEWIFICAM_CHERRYUSB_FIREFLY_V2_USB_GLUE_H
#define FREEWIFICAM_CHERRYUSB_FIREFLY_V2_USB_GLUE_H

#include <stdint.h>

/* Bring one controller instance up: the OSAL services the stack needs, the
 * xHCI driver registration for `busid` and the interrupt binding for that
 * bus. Idempotent. Call it before the stack's usbh_initialize() for that
 * bus. Returns 0 on success. */
int usb_board_init(unsigned busid);

/* The controller's register window for `busid` (0 for an unknown busid). */
uintptr_t usb_board_reg_base(unsigned busid);

/* The stack's enumeration-event sink handed to usbh_initialize(); the glue
 * logs each event and keeps the xHCI slot state consistent across a
 * modeswitch flip. */
void usb_board_event_handler(uint8_t busid, uint8_t hub_index,
			     uint8_t hub_port, uint8_t intf, uint8_t event);

/* Post-usbh_initialize() board step. The xHCI roots on this board need
 * nothing here (no port power registers to raise), so the firefly body is
 * a no-op kept for the case code's shared shape. */
void usb_board_after_start(unsigned busid);

/* How many times the bus's interrupt line was serviced - the counter that
 * separates "the transfer is slow" from "the completion interrupt stopped
 * arriving" (the W2-01 lesson). */
unsigned int usb_board_irq_count(unsigned int busid);

#endif /* FREEWIFICAM_CHERRYUSB_FIREFLY_V2_USB_GLUE_H */
