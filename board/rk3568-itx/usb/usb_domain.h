/*
 * @file   usb_domain.h
 * @brief  RK3568 USB power/PHY domain bring-up and this board's controller
 *         register facts (board interface).
 *
 * The USB controllers only answer after a fixed power/clock/reset sequence:
 * the PD_PIPE power island, the PHY reference clock gates, the port VBUS
 * enables, the two usb2phy port blocks and the CRU soft-reset pulses that
 * take the DesignWare core out of the reset the firmware leaves it in. The
 * sequence is register-exact for this SoC, and the ORDER inside it is
 * load-bearing:
 *
 *  - Every shared-resource pulse resets whole controllers. The CRU
 *    soft-reset bits pulsed here cover the USB3 (OTG) core group AND both
 *    USB2 host roots, so the whole sequence must run to completion before
 *    any host controller driver is initialised. A pulse landing after an
 *    HCD init silently kills a controller that was already running (no
 *    interrupt, no error, just a dead port).
 *
 *  - The sequence is self-bootstrapping and never trusts the state the
 *    loader left behind: a controller that the loader had already started
 *    is taken back to a cold state and brought up again, so the image
 *    always boots into the same controller state.
 *
 *  - Every step is once-guarded. Re-running a soft reset over a live
 *    island drops controllers that are already attached; the guards are
 *    the only thing standing between a second `usb_board_init()` and a
 *    dead bus.
 *
 *  - The usb2phy POR/port-reset bits are RELEASE-ONLY and must never be
 *    asserted from here: asserting them drops a PHY that the other USB
 *    root of the same PHY block is using.
 *
 * Register access lives in usb_domain.c alone - this layer owns the MMIO.
 * Consumers (the CherryUSB board glue, and later any other host backend)
 * call the functions below and never touch a register themselves.
 *
 * The DWC3 core-global constants sit in usb_dwc3.h next to this file; the
 * controller bases and interrupt numbers are part of the bus map below.
 */

#ifndef FREEWEBCAM_RK3568_USB_DOMAIN_H
#define FREEWEBCAM_RK3568_USB_DOMAIN_H

#include <stdint.h>

/* --- bus map -------------------------------------------------------------- */

/* Four host controllers are wired out on this board, and the busid order is
 * fixed by the controller family: the two USB2 host roots first, then the
 * two USB3 (DWC3) instances. The HCD drivers in the imported stack index
 * their static pools by this same number, so it is a contract, not a
 * convention. */
#define USB_BOARD_BUS_COUNT		4U

/* busid 0/1: the USB2 host roots (EHCI). Each root leads to a four-port
 * on-board hub and then to the panel USB2.0 Type-A connectors; both roots
 * share the usb2phy1 block and the GPIO3_A0 VBUS enable. */
#define USB_BOARD_EHCI_BUS_COUNT	2U
#define USB_BOARD_EHCI0_REG_BASE	0xFD800000UL
#define USB_BOARD_EHCI0_INTID		162U
#define USB_BOARD_EHCI1_REG_BASE	0xFD880000UL
#define USB_BOARD_EHCI1_INTID		165U

/* busid 2/3: the USB3 (DWC3) instances, whose host interface is xHCI at the
 * controller base (the DesignWare core globals sit further in - usb_dwc3.h).
 * busid 2 drives its port natively as host; busid 3 comes out of reset in
 * the OTG role and has to be forced to host before anything enumerates.
 * Both are High-Speed only on this board: the SuperSpeed lanes' combo PHYs
 * are routed to SATA. */
#define USB_BOARD_XHCI0_REG_BASE	0xFD000000UL
#define USB_BOARD_XHCI0_INTID		202U
#define USB_BOARD_XHCI1_REG_BASE	0xFCC00000UL
#define USB_BOARD_XHCI1_INTID		201U

/* Bus map accessors. The busid -> (base, INTID) pairing is data: it is read
 * by the HCD driver registration and the interrupt arming, never written. */
static inline int usb_board_bus_is_xhci(unsigned busid)
{
	return (unsigned)busid >= USB_BOARD_EHCI_BUS_COUNT;
}

static inline uintptr_t usb_board_bus_reg_base(unsigned busid)
{
	switch (busid) {
	case 0U:
		return (uintptr_t)USB_BOARD_EHCI0_REG_BASE;
	case 1U:
		return (uintptr_t)USB_BOARD_EHCI1_REG_BASE;
	case 2U:
		return (uintptr_t)USB_BOARD_XHCI0_REG_BASE;
	case 3U:
		return (uintptr_t)USB_BOARD_XHCI1_REG_BASE;
	default:
		return (uintptr_t)0;
	}
}

static inline uint32_t usb_board_bus_intid(unsigned busid)
{
	switch (busid) {
	case 0U:
		return USB_BOARD_EHCI0_INTID;
	case 1U:
		return USB_BOARD_EHCI1_INTID;
	case 2U:
		return USB_BOARD_XHCI0_INTID;
	case 3U:
		return USB_BOARD_XHCI1_INTID;
	default:
		return 0U;
	}
}

/* --- board fact: the write-enable register protocol ----------------------- */

/* The SoC's GRF/PMU-style register banks take a "field write enable" in the
 * upper half word: a bit set there enables the matching bit in the lower
 * half to be written, and the bits left clear in the upper half stay
 * untouched. Every write below goes through this helper so a field write
 * cannot silently clobber the neighbouring bits of the same register. The
 * CRU clock-gate and reset registers share this form, and so do the GPIO
 * data/direction registers. */
#define USB_BOARD_WE(bits, val)		(((bits) << 16) | (val))

/* --- register blocks ------------------------------------------------------ */

#define USB_BOARD_CRU_BASE		0xFDD20000UL
#define USB_BOARD_PMUCRU_BASE		0xFDD00000UL
#define USB_BOARD_PMU_BASE		0xFDD90000UL
#define USB_BOARD_GPIO3_BASE		0xFE760000UL
#define USB_BOARD_USB2PHY0_GRF_BASE	0xFDCA0000UL	/* syscon, WE protocol */
#define USB_BOARD_USB2PHY1_GRF_BASE	0xFDCA8000UL	/* syscon, WE protocol */

/* PMU: the PD_PIPE power domain. Rising the domain is a two-step handshake -
 * release the bus idle request, wait for the acknowledgement to clear, then
 * power the domain on and wait for the power-down status to clear. Reading
 * the status back (rather than a fixed delay) is what makes the step
 * deterministic across cold and warm boots. */
#define USB_BOARD_PMU_BUS_IDLE_SFTCON0	0x050U
#define USB_BOARD_PMU_BUS_IDLE_ACK	0x060U
#define USB_BOARD_PMU_BUS_IDLE_ST	0x068U
#define USB_BOARD_PMU_PD_PIPE_IDLE_BIT	(1U << 11)
#define USB_BOARD_PMU_PWR_GATE_SFTCON	0x0A0U
#define USB_BOARD_PMU_PWR_DOWN_ST	0x098U
#define USB_BOARD_PMU_PD_PIPE_BIT	(1U << 8)

/* PMUCRU clock gate con2: bit0 = the 24 MHz reference, bit1 = the usb2phy0
 * oscillator gate, bit2 = the usb2phy1 one. CRU clock-gate polarity is
 * active-low on the data bits (1 = clock gated off), so the enable write
 * carries data 0. All three gates are off after a cold power-on, and the
 * PHY blocks stay mute until they are opened. */
#define USB_BOARD_PMUCRU_CLKGATE_CON2	0x188U
#define USB_BOARD_PMUCRU_USBPHY_GATES	0x00000007U

/* CRU: clock gates at +0x300 and soft resets at +0x400, "conN" = four bytes
 * per index. */
/* con14 bits 4-9: H_USB2HOST0 + its arbiter/UTMI, and H_USB2HOST1 + its
 * arbiter/UTMI. Assert-and-release: the pulse is what takes the USB2 host
 * roots out of the state the loader left them in. The HCLK gates for the
 * two roots are on by default (no clock id exists for them), so this is the
 * only reset-side step the USB2 path needs. */
#define USB_BOARD_CRU_SOFTRST_CON14	0x438U
#define USB_BOARD_CRU_SOFTRST_CON14_BITS	0x03F0U
/* con28 bit11: the P_USB2PHY1_GRF pclk. Release-only. */
#define USB_BOARD_CRU_SOFTRST_CON28	0x470U
#define USB_BOARD_CRU_SOFTRST_CON28_BITS	0x0800U
/* con29 bits 3-5: the usb2phy1 POR and port resets. Release-only. */
#define USB_BOARD_CRU_SOFTRST_CON29	0x474U
#define USB_BOARD_CRU_SOFTRST_CON29_BITS	0x0038U
/* con10: the USB3OTG clock gates - bits 8-10 gate otg0, bits 12-14 gate
 * otg1; bits 0-1 belong to the same clock family and are opened with them
 * in case a sibling driver has not run yet. Data 0 = clock enabled. */
#define USB_BOARD_CRU_CLKGATE_CON10	0x328U
#define USB_BOARD_CRU_CLKGATE_CON10_BITS	0x7303U
/* con9 bits 4-5: the two USB3OTG core soft resets. Firmware hands over with
 * both cores held in reset, and a core left in reset reads zero through its
 * whole register aperture - including the core ID register, which is why
 * the assert-and-release pulse here is not optional. */
#define USB_BOARD_CRU_SOFTRST_CON9	0x424U
#define USB_BOARD_CRU_SOFTRST_CON9_BITS	0x0030U

/* The two usb2phy blocks share one register layout. Their port-control words
 * are the same three on both (offsets identical), and the values below are
 * the measured working set for this board: the OTG lane forced to the host
 * role, the host lane out of suspend, and the 480 MHz UTMI clock output
 * opened (the USB2 root's controller clock comes from there). Each port
 * write is followed by a settle delay - the PHY block samples its port
 * configuration on its own clock, and two back-to-back writes with no
 * settle in between lose the first one. */
#define USB_BOARD_USB2PHY_GRF_OTG_CON0		0x0000U
#define USB_BOARD_USB2PHY_GRF_HOST_CON1		0x0004U
#define USB_BOARD_USB2PHY_GRF_CLKOUT_CON2	0x0008U
#define USB_BOARD_USB2PHY_OTG_SUS_VAL		0x0C00U
#define USB_BOARD_USB2PHY_OTG_SUS_MASK		0x0FFFU
#define USB_BOARD_USB2PHY_HOST_SUS_VAL		0x01D2U
#define USB_BOARD_USB2PHY_HOST_SUS_MASK		0x01FFU
/* CLKOUT_CON2 bit4 = the write enable for the 480 MHz output, data 0 =
 * output open. */
#define USB_BOARD_USB2PHY_CLKOUT_WE		0x0010U

/* VBUS enables on GPIO3: A0 powers the panel USB2.0 group (both USB2
 * roots), A1 the USB3 socket group. Both are outputs driven high. A port
 * without VBUS has no power at all - the device is invisible, with no error
 * anywhere - and a cold VBUS ramp (drive the pin from here rather than
 * inheriting a live line) is also what makes a device that was already
 * plugged in at boot enumerate. */
#define USB_BOARD_GPIO3_SWPORT_DDR	0x0000U
#define USB_BOARD_GPIO3_SWPORT_DR	0x0008U
#define USB_BOARD_GPIO_VBUS_PINS	((1U << 0) | (1U << 1))

/* Offsets used by the small accessors below. The USB2 root's port status
 * array starts at 0x44 into the operational register block (EHCI 1.0: the
 * operational registers run USBCMD..CONFIGFLAG at 0x00..0x40, then the port
 * array), one 32-bit word per port. */
#define USB_BOARD_EHCI_PORTSC_OFFSET	0x44U

/* --- interface ------------------------------------------------------------ */

/* Microsecond busy-wait on the architectural timer (always running at EL1).
 * Used for the PHY/CRU settle times inside the sequence below; it makes no
 * claim about being usable with interrupts masked for long stretches. */
void usb_udelay(uint32_t usec);

/* Bring up every shared USB domain resource, once per boot, in the fixed
 * order: the USB3 socket group first, then the usb2phy1 block of the USB2
 * roots. Call it in task context and let it finish before the first host
 * controller driver is initialised - the soft-reset pulses inside reset
 * whole controllers, so they must not land between two HCD initialisations.
 * Idempotent. */
void usb_domain_up(void);

/* PD_PIPE power island on, PHY reference clock gates open, port VBUS
 * enabled. Shared by every controller; once per boot. */
void usb_bus_domain_once(void);

/* The usb2phy1 block (both USB2 host roots share it): reference gates,
 * USB2HOST0/1 soft-reset pulse, release-only POR/pclk bits and the port
 * controls. Runs the bus domain first. Once per boot. */
void usb_usb2phy1_domain_init(void);

/* The USB3 socket-group domain: USB3OTG clock gates, the con9 + con14
 * soft-reset pulse that takes both DWC3 cores to a cold state, and the
 * usb2phy0 port controls (its two lanes feed the two xHCI instances).
 * Runs the bus domain first. Once per boot. */
void usb_usb3_domain_init(void);

/* One DWC3 core: close the soft reset the domain leaves open, open both
 * PHYs, apply the PHY quirk set, select High-Speed and force the host role
 * (the OTG instance does not enumerate otherwise). `base` is the controller
 * register base from the bus map; the xHCI aperture sits at +0 and the core
 * globals at +0xC100 (usb_dwc3.h). Must run after usb_usb3_domain_init(). */
void usb_xhci_dwc3_host_init(uintptr_t base);

/* The DWC3 core ID register of an initialised core (GSNPSID, usb_dwc3.h).
 * The bring-up anchor: a core whose soft reset was never closed reads zero
 * here. Read through this layer so no register access has to appear outside
 * it. */
uint32_t usb_xhci_core_id(uintptr_t base);

/* One USB2 root port's status/control word, raw. `opbase` is the root's
 * operational register base (controller base + the capability length the
 * controller reports); `port_index` is zero-based. The word is returned
 * undecoded - translating it into a USB speed is the host stack's contract,
 * not a board fact - but the register window stays owned here. */
/* EHCI 1.0 PORTSC bits the pre-plugged-device kick needs (hardware facts):
 * CCS = current connect status (bit 0), PP = port power (bit 12). */
#define USB_BOARD_EHCI_PORTSC_CCS	0x00000001U
#define USB_BOARD_EHCI_PORTSC_PP	0x00001000U

/* Write accessor. The power cycle manufactures the connect-change edge a
 * device that was already attached at boot never produces; returns the port
 * status after the cycle. */
uint32_t usb_ehci_port_power_cycle(uintptr_t opbase, unsigned port_index);

uint32_t usb_ehci_port_status(uintptr_t opbase, unsigned port_index);

#endif /* FREEWEBCAM_RK3568_USB_DOMAIN_H */