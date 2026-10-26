/*
 * @file   usb_glue.h
 * @brief  Board glue between the imported USB host stack and this board's
 *         controller facts (port interface).
 *
 * The glue binds one host controller instance per bus: it brings the board's
 * USB domain up, registers the right controller driver for the bus, arms the
 * bus's interrupt line and answers register-base queries. Everything below
 * that (the stack's hub thread, enumeration, class drivers) runs on top of
 * the imported stack and is not this file's business.
 *
 * The register facts themselves - bases, interrupt numbers, the domain
 * sequence - belong to the board layer under board/rk3568-itx/usb/; this
 * layer only binds them to the stack. The driver-facing symbols the
 * controller drivers require (the low-level hooks, the DMA-cache primitives
 * and the console sink) are implemented in usb_glue.c and declared by the
 * imported headers themselves.
 */

#ifndef FREEWIFICAM_CHERRYUSB_RK3568_USB_GLUE_H
#define FREEWIFICAM_CHERRYUSB_RK3568_USB_GLUE_H

#include <stdint.h>

/* Bring one controller instance up: the board's shared USB domain, the
 * controller driver registration for `busid` and the interrupt binding for
 * that bus. Idempotent - a second call for the same bus re-registers the
 * same driver and re-arms the same line. Call it before the stack's
 * usbh_initialize() for that bus.
 *
 * Returns 0, or a negative value when `busid` names no controller on this
 * board. */
int usb_board_init(unsigned busid);

/* Start the deferred-timer service thread the osal needs before any timer can
 * fire. The stack's timers must not run their callbacks on the kernel's timer
 * context (the hub's expiry callback suspends inside a command submit, which
 * that context may not do), so expiries are queued to this thread. Idempotent. */
void usb_osal_timer_service_start(void);

/* The stack's enumeration event sink: every milestone (init, configured,
 * interface start/unsupported, disconnect) becomes one anchor line. Pass it to
 * usbh_initialize(); without a handler the core installs a silent dummy and a
 * run cannot say where enumeration stopped. */
void usb_board_event_handler(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
			     uint8_t intf, uint8_t event);

/* Post-start board work, to be called right after usbh_initialize() for this
 * bus. On an EHCI bus it manufactures the connect-change edge a device that
 * was already attached at boot never produces: the root port's power is
 * cycled, the root hub's change bits are seeded and the hub thread is woken,
 * so enumeration starts. A no-op on the xHCI buses. */
void usb_board_after_start(unsigned busid);

/* The controller's register base, for usbh_initialize()'s reg_base argument.
 * Returns 0 for a busid this board does not wire out. */
uintptr_t usb_board_reg_base(unsigned busid);

/* How many times the controller's interrupt line has been serviced, per bus.
 * A transfer that completes only through the port's timeout drain leaves this
 * counter standing still - the diagnostic that names a lost completion
 * interrupt rather than a slow device. */
unsigned int usb_board_irq_count(unsigned int busid);

#endif /* FREEWIFICAM_CHERRYUSB_RK3568_USB_GLUE_H */