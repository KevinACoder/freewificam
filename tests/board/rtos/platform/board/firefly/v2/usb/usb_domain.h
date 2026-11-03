/*
 * @file   usb_domain.h
 * @brief  firefly v2 (Phytium-Pi, E2000-class SoC) USB controller facts
 *         (board interface).
 *
 * Unlike the RK3568 carrier, this board's USB host controllers need no
 * software power/clock/reset sequence: the two USB3 instances are plain
 * xHCI apertures that the board firmware (SBL/U-Boot) leaves running, and
 * the Linux device tree drives them through the generic xhci-plat driver
 * with no PHY or clock node of their own
 * (linux-upstream arch/arm64/boot/dts/firefly/firefly_v2-soc.dtsi:217-229,
 * commits b7d6f4ba72db / 7475fce2a618).  What remains board data is the
 * bus map below - nothing else, so this header has no .c sibling and no
 * bring-up sequence to get wrong.
 *
 * The three "firefly,usb2" instances in the same DTS are a different,
 * vendor dual-role IP (not EHCI); this project has no source driver for
 * it and the SDK ships it only as a binary blob, so this map deliberately
 * stops at the two xHCI instances.  The AIC8800 dongle for this line is
 * plugged into one of the USB3 ports (the U-Boot `usb tree` output shows
 * it under a 5 Gb/s xHCI root hub).
 *
 * INTID rule: the GIC SPI number from the device tree plus 32 (the GIC's
 * INTID base for SPIs).  The same arithmetic holds on the RK3568 carrier
 * (DTS SPI 130/133/169/170 -> glue 162/165/201/202), so the rule is not
 * a guess: usb3_0 SPI 16 -> 48, usb3_1 SPI 17 -> 49.
 */

#ifndef FREEWIFICAM_FIREFLY_V2_USB_DOMAIN_H
#define FREEWIFICAM_FIREFLY_V2_USB_DOMAIN_H

#include <stdint.h>

/* --- bus map -------------------------------------------------------------- */

/* Two host controllers are wired out and both are xHCI: the busid order is
 * the DTS order (usb3_0, usb3_1). The HCD drivers in the imported stack
 * index their static pools by this same number, so it is a contract, not
 * a convention. */
#define USB_BOARD_BUS_COUNT		2U

/* busid 0: usb3_0. busid 1: usb3_1. Register window sizes are the DTS's
 * (0x18000 each); the interrupt numbers follow the INTID rule above. */
#define USB_BOARD_XHCI0_REG_BASE	0x31A08000UL
#define USB_BOARD_XHCI0_INTID		48U
#define USB_BOARD_XHCI1_REG_BASE	0x31A28000UL
#define USB_BOARD_XHCI1_INTID		49U

/* Bus map accessors. The busid -> (base, INTID) pairing is data: it is read
 * by the HCD driver registration and the interrupt arming, never written. */
static inline int usb_board_bus_is_xhci(unsigned busid)
{
	(void)busid;
	return 1;
}

static inline uintptr_t usb_board_bus_reg_base(unsigned busid)
{
	switch (busid) {
	case 0U:
		return (uintptr_t)USB_BOARD_XHCI0_REG_BASE;
	case 1U:
		return (uintptr_t)USB_BOARD_XHCI1_REG_BASE;
	default:
		return (uintptr_t)0;
	}
}

static inline uint32_t usb_board_bus_intid(unsigned busid)
{
	switch (busid) {
	case 0U:
		return USB_BOARD_XHCI0_INTID;
	case 1U:
		return USB_BOARD_XHCI1_INTID;
	default:
		return 0U;
	}
}

/* The capability register's first dword: HCIVERSION in the upper half,
 * CAPLENGTH in the low byte. A live aperture reads a nonzero version
 * (0x0100-class); a dead or clock-gated one reads zero - the same
 * "aperture alive" anchor the RK3568 glue takes from the DWC3 core ID. */
static inline uint32_t usb_xhci_cap_id(uintptr_t reg_base)
{
	return *(volatile uint32_t *)(reg_base + 0U);
}

#endif /* FREEWIFICAM_FIREFLY_V2_USB_DOMAIN_H */
