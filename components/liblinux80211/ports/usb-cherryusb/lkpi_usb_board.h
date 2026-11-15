/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   lkpi_usb_board.h
 * @brief  The board facts the CherryUSB-backed linuxkpi URB engine needs,
 *         named without naming a board.
 *
 * The adapter used to include the RK3568 board's `usb_domain.h` directly,
 * which tied a component port to one board.  The two things it actually wants
 * from a board are:
 *
 *   - how many host buses the board wires out (`USB_BOARD_BUS_COUNT`), where
 *     the RK3568 USB power/PHY domain header carries it and the F429's single
 *     OTG_HS controller states it in its glue;
 *   - a poll entry that runs the controller's interrupt handler from the
 *     engine's drain thread (`usb_board_irq_dispatch`).  The controller is
 *     the board's: RK3568's multi-controller HCD takes the bus id, the F429's
 *     single-controller DWC2 port does not, and the board glue is the only
 *     place that knows the difference.
 *
 * The adapter includes this header instead of `usb_domain.h`; a board that
 * supplies `usb_domain.h` still gets it, because the RK3568 domain header is
 * where its bus map lives.
 */

#ifndef LKPI_USB_BOARD_H
#define LKPI_USB_BOARD_H

#include <stdint.h>

/* The board layer: usb_board_init/after_start/reg_base/event_handler, and the
 * board header that names its controller(s). */
#include "usb_glue.h"

#if defined(__has_include)
# if __has_include("usb_domain.h")
#  include "usb_domain.h"
# endif
#endif

#ifndef USB_BOARD_BUS_COUNT
#define USB_BOARD_BUS_COUNT 1
#endif

/* 1 when the board has started this bus and the controller answers. */
int usb_board_bus_ready(unsigned int busid);

/* 1 for an xHCI bus (its readiness gate differs); 0 for EHCI/DWC2. */
int usb_board_bus_is_xhci(unsigned int busid);

/* Run the controller's interrupt handler once, from the caller's context.
 * The caller holds the stack's critical section. */
void usb_board_irq_dispatch(unsigned int busid);

#endif /* LKPI_USB_BOARD_H */
