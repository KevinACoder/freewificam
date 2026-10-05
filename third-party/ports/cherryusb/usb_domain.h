/*
 * @file
 * @brief RK3568 USB power/PHY domain bring-up - the part of the platform
 * sequence that is the same whichever host stack drives the controllers.
 *
 * Moved out of port/adapters/libbsd/usb_platform.c (feat/cherryusb_ehci):
 * the register sequence is the register-exact NetBSD port of this board
 * (rk_usb2phy.c + dwc3_fdt.c, netbsd-11) and was proven from a cold USB
 * domain at every NetBSD and freewebcamera cold boot since 2026-09-03.
 * The CherryUSB backend needs exactly the same PD_PIPE + PHY reference
 * clock + VBUS + usb2phy1 sequence before its EHCI registers are touched,
 * and a second copy of these registers is a second place to get the CRU
 * write protocol wrong (high-half write enable; one bit per pin on GRF).
 *
 * Only the shared part lives here: the USB3 socket-group domain and the
 * DWC3 core reconfiguration stay in usb_xhci_platform.c, and the HCD
 * composition (ehci_softc + config_found, or CherryUSB's usbh_initialize)
 * stays with its backend.
 *
 * @date 27.09.2026
 * @author zhugengyu
 */

#ifndef USB_DOMAIN_H
#define USB_DOMAIN_H

#include <stdint.h>

/* Microsecond busy-wait off the ARM generic timer (always on at EL1). */
void usb_udelay(uint32_t usec);

/* PD_PIPE power island on + PHY reference clock gates + VBUS enable pins.
 * Runs once per boot: re-running a soft reset on a live island takes down
 * already-attached controllers (the rk_usb2phy once-guard lesson). */
void usb_bus_domain_once(void);

/* The usb2phy1 domain for the two panel EHCI roots (both roots share the
 * one PHY domain; the shared bus domain runs first). Runs once per boot. */
void usb_usb2phy1_domain_init(void);

/* The USB3 socket-group domain (once per boot): CRU clkgate_con10, the
 * con9+con14 SRST pulse that takes the ATF-held DWC3 cores to a cold state,
 * and the usb2phy0 port GRF.  Every shared-domain pulse resets whole USB
 * blocks (con14 bounces both EHCI roots too), so both domains must run to
 * completion BEFORE any HCD is initialized.  feat/cherryusb_xhci moved this
 * here from port/adapters/libbsd/usb_xhci_platform.c: the CherryUSB xHCI
 * glue runs the same sequence the NetBSD attach does. */
void usb_usb3_domain_init(void);

/* dwc3_fdt.c's core soft-reset + enable_phy + set_mode for one DWC3 core; base
 * is the controller base whose xHCI aperture sits at +0 and whose core
 * globals start at +0xC100 (usb_board.h).  Register-exact net_80211 line:
 * the soft reset MUST close with the GCTL.CORESOFTRESET clear, the OTG
 * instance needs the forced PRTCAP=host, HS-only needs GUCTL1 bit26. */
void usb_xhci_dwc3_host_init(uintptr_t base);

#endif /* USB_DOMAIN_H */
