/*
 * @file   usb_glue.h
 * @brief  F429 board glue for the imported CherryUSB DWC2 host port.
 *
 * The glue binds the board's OTG_HS controller to the stack: it powers and
 * pins the controller, binds the OTG_HS interrupt line to USBH_IRQHandler(0),
 * answers the register-base query and supplies the DWC2 user parameters.
 * Everything above that (hub thread, enumeration, MSC class) is the imported
 * stack's business.
 */

#ifndef FREEWIFICAM_STM32F429_USB_GLUE_H
#define FREEWIFICAM_STM32F429_USB_GLUE_H

#include <stdint.h>

/* Bring the one controller up: GPIOB/PB14/PB15, OTG_HS clock, NVIC line 77
 * (priority BOARD_IRQ_PRIORITY_API_CALL_RAW) and the stack's timer service.
 * Call before usbh_initialize().  Idempotent; returns 0 for busid 0 and -1
 * for any other busid. */
int usb_board_init(unsigned busid);

/* Post-start work: seed the root port's connect-change bit and wake the hub
 * thread, so a disk already attached at boot is enumerated even if DWC2's
 * power-on did not produce a fresh connect edge. */
void usb_board_after_start(unsigned busid);

/* Enumeration event sink passed to usbh_initialize(): one anchor per
 * milestone (init/configured/interface start/disconnect). */
void usb_board_event_handler(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
			     uint8_t intf, uint8_t event);

/* The controller's register base (0x40040000 for OTG_HS); 0 for a busid this
 * board does not wire out. */
uintptr_t usb_board_reg_base(unsigned busid);

/* How many times the OTG_HS line has been serviced; a completion that only
 * arrives through a timeout drain leaves this standing still. */
unsigned int usb_board_irq_count(unsigned int busid);

/* The host port status register (HPRT) at the moment of the read.  A failed
 * enumeration prints it: PPWR set with PCSTS clear is "powered, no device",
 * which separates a wiring/jumper problem from a class-driver problem. */
uint32_t usb_board_port_status(void);

/* Start the stack's deferred-timer service thread (defined by the shared
 * CMSIS-RTOS2 OSAL).  Idempotent; called by usb_board_init(). */
void usb_osal_timer_service_start(void);

#endif /* FREEWIFICAM_STM32F429_USB_GLUE_H */
