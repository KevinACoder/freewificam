/*
 * @file   usb_domain.c
 * @brief  RK3568 USB power/PHY/reset domain bring-up (the one place these
 *         registers are written).
 *
 * This is the board half of USB support: it takes the controllers from
 * whatever state the firmware left them in to "registers answer", and hands
 * the result to whichever host controller driver runs on top. The sequence
 * is order-dependent and once-guarded for reasons that are all visible in
 * the register behaviour - see the rationale in usb_domain.h and the notes
 * at each step below. The two rules that break the board when violated:
 *
 *   1. the shared soft-reset pulses reset whole controllers, so the whole
 *      sequence finishes before the first HCD init and never runs twice;
 *   2. the usb2phy POR/port-reset bits are released, never asserted - one
 *      PHY block serves two controller roots.
 *
 * Everything here is bare MMIO on this board's register blocks; the arch
 * layer supplies the timer used for the settle delays, and nothing else.
 *
 * @author zhugengyu
 * @date   07.10.2026
 * @design register sequence - a fixed-write sequence with read-back
 *         handshakes, the established shape for bringing a vendor power/PHY
 *         block up (the console UART's programming is the same shape at a
 *         smaller scale: board/rk3568-itx/drivers/uart.c:95-117, fixed
 *         writes in a load-bearing order). The once-guard is state
 *         initialisation - a static flag read at the top of the entry point,
 *         the same discipline the interrupt controller bring-up uses
 *         (arch/aarch64/gicv3.c:287-291).
 */

#include <stdint.h>

#include "generic_timer.h"
#include "usb_domain.h"
#include "usb_dwc3.h"

/* Domain state: one flag per once-guarded step. Boot-time only, so a plain
 * bool is enough (no interrupt ever reads it). */
static int s_bus_domain_done;
static int s_usb2phy1_domain_done;
static int s_usb3_domain_done;

/* --- shared helpers ------------------------------------------------------- */

/* Microsecond busy-wait on the architectural counter. The counter runs at a
 * fixed frequency (gtimer_freq_get), so the tick count is exact; a requested
 * delay always costs at least one tick, which keeps a zero-length request
 * from degenerating into "no wait at all". */
void usb_udelay(uint32_t usec)
{
	uint64_t freq = (uint64_t)gtimer_freq_get();
	uint64_t ticks = (freq * (uint64_t)usec) / 1000000ULL;
	uint64_t start;

	if (ticks == 0ULL) {
		ticks = 1ULL;
	}

	start = gtimer_count_get();
	while ((gtimer_count_get() - start) < ticks) {
		/* spin */
	}
}

/* Wait for `bits` in the register at `reg` to read as `want`, polling in
 * 100 us steps. Returns once the condition holds or the attempt budget is
 * exhausted; the caller decides whether a timeout is fatal (the callers
 * below treat it as advisory - the following reads expose a block that did
 * not come up, and a sequence that halts here would turn a hard failure
 * into a silent one). */
static void usb_wait_bits(const volatile uint32_t *reg, uint32_t bits,
			  uint32_t want)
{
	uint32_t i;

	for (i = 0U; i < 100U; i++) {
		if ((*reg & bits) == want) {
			return;
		}
		usb_udelay(100U);
	}
}

/* --- the shared power island ---------------------------------------------- */

void usb_bus_domain_once(void)
{
	volatile uint32_t *pmu = (volatile uint32_t *)USB_BOARD_PMU_BASE;
	volatile uint32_t *pmucru = (volatile uint32_t *)USB_BOARD_PMUCRU_BASE;
	volatile uint32_t *gpio_ddr = (volatile uint32_t *)(USB_BOARD_GPIO3_BASE +
						USB_BOARD_GPIO3_SWPORT_DDR);
	volatile uint32_t *gpio_dr = (volatile uint32_t *)(USB_BOARD_GPIO3_BASE +
						USB_BOARD_GPIO3_SWPORT_DR);

	if (s_bus_domain_done != 0) {
		return;
	}

	/* Step 1: release the PD_PIPE bus idle request, then wait for the
	 * acknowledgement to clear. The handshake is a request/ack pair, so
	 * the ack register is what says the island accepted the request. */
	*(pmu + (USB_BOARD_PMU_BUS_IDLE_SFTCON0 / 4U)) =
		USB_BOARD_WE(USB_BOARD_PMU_PD_PIPE_IDLE_BIT, 0U);
	usb_wait_bits(pmu + (USB_BOARD_PMU_BUS_IDLE_ACK / 4U),
		      USB_BOARD_PMU_PD_PIPE_IDLE_BIT, 0U);

	/* Step 2: power the domain on and wait for the power-down status to
	 * clear (same shape: request, then read the status back). */
	*(pmu + (USB_BOARD_PMU_PWR_GATE_SFTCON / 4U)) =
		USB_BOARD_WE(USB_BOARD_PMU_PD_PIPE_BIT, 0U);
	usb_wait_bits(pmu + (USB_BOARD_PMU_PWR_DOWN_ST / 4U),
		      USB_BOARD_PMU_PD_PIPE_BIT, 0U);

	/* Step 3: open the PHY reference clock gates (data 0 = enabled on the
	 * CRU/PMUCRU clock-gate polarity). Both PHY blocks and the 24 MHz
	 * reference are gated off after a cold power-on. */
	*(pmucru + (USB_BOARD_PMUCRU_CLKGATE_CON2 / 4U)) =
		USB_BOARD_WE(USB_BOARD_PMUCRU_USBPHY_GATES, 0U);

	/* Step 4: enable VBUS. Direction first, then data - the GPIO data and
	 * direction registers take the same upper-half write-enable form as
	 * the GRF banks, so both writes set the two pins in one access.
	 * Driving the pins from here rather than inheriting a live line gives
	 * every port a cold VBUS ramp, which is what makes a device that was
	 * already plugged in at boot raise a connect event. */
	*gpio_ddr = USB_BOARD_WE(USB_BOARD_GPIO_VBUS_PINS,
				 USB_BOARD_GPIO_VBUS_PINS);
	*gpio_dr = USB_BOARD_WE(USB_BOARD_GPIO_VBUS_PINS,
				USB_BOARD_GPIO_VBUS_PINS);

	s_bus_domain_done = 1;
}

/* --- the USB2 host roots' PHY block (usb2phy1) ---------------------------- */

void usb_usb2phy1_domain_init(void)
{
	volatile uint32_t *cru = (volatile uint32_t *)USB_BOARD_CRU_BASE;
	volatile uint32_t *pmucru = (volatile uint32_t *)USB_BOARD_PMUCRU_BASE;
	volatile uint32_t *grf =
		(volatile uint32_t *)USB_BOARD_USB2PHY1_GRF_BASE;

	if (s_usb2phy1_domain_done != 0) {
		return;
	}
	if (s_bus_domain_done == 0) {
		usb_bus_domain_once();
	}

	/* The PHY block sits behind the same reference gates as the power
	 * island; the write is repeated here so the domain is self-contained
	 * (the earlier one may have been skipped when this runs first). */
	*(pmucru + (USB_BOARD_PMUCRU_CLKGATE_CON2 / 4U)) =
		USB_BOARD_WE(USB_BOARD_PMUCRU_USBPHY_GATES, 0U);

	/* USB2HOST0/1 and their arbiter/UTMI interfaces: assert the soft
	 * reset, let it settle, release it. The pulse is what clears a root
	 * that the loader had already started; a 100 us hold is the value
	 * this block's reset synchroniser needs. */
	*(cru + (USB_BOARD_CRU_SOFTRST_CON14 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON14_BITS,
			     USB_BOARD_CRU_SOFTRST_CON14_BITS);
	usb_udelay(100U);
	*(cru + (USB_BOARD_CRU_SOFTRST_CON14 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON14_BITS, 0U);
	usb_udelay(100U);

	/* POR and port reset, and the PHY's GRF pclk: RELEASE ONLY. Asserting
	 * the POR would drop a PHY whose other lane carries the USB3 group. */
	*(cru + (USB_BOARD_CRU_SOFTRST_CON29 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON29_BITS, 0U);
	usb_udelay(100U);
	*(cru + (USB_BOARD_CRU_SOFTRST_CON28 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON28_BITS, 0U);
	usb_udelay(100U);

	/* Port controls, in the order the PHY block expects: the OTG lane into
	 * the host role, the host lane out of suspend, then the 480 MHz UTMI
	 * clock output opened (the USB2 root controller is clocked from it).
	 * The 2 ms settle after each port write is the PHY's own port-write
	 * cadence; without it the next write can be sampled before the
	 * previous one has propagated. */
	*(grf + (USB_BOARD_USB2PHY_GRF_OTG_CON0 / 4U)) =
		USB_BOARD_WE(USB_BOARD_USB2PHY_OTG_SUS_MASK,
			     USB_BOARD_USB2PHY_OTG_SUS_VAL);
	usb_udelay(2000U);
	*(grf + (USB_BOARD_USB2PHY_GRF_HOST_CON1 / 4U)) =
		USB_BOARD_WE(USB_BOARD_USB2PHY_HOST_SUS_MASK,
			     USB_BOARD_USB2PHY_HOST_SUS_VAL);
	usb_udelay(2000U);
	*(grf + (USB_BOARD_USB2PHY_GRF_CLKOUT_CON2 / 4U)) =
		USB_BOARD_WE(USB_BOARD_USB2PHY_CLKOUT_WE, 0U);
	usb_udelay(2000U);

	s_usb2phy1_domain_done = 1;
}

/* --- the USB3 socket group's domain -------------------------------------- */

void usb_usb3_domain_init(void)
{
	volatile uint32_t *cru = (volatile uint32_t *)USB_BOARD_CRU_BASE;
	volatile uint32_t *grf0 =
		(volatile uint32_t *)USB_BOARD_USB2PHY0_GRF_BASE;

	if (s_usb3_domain_done != 0) {
		return;
	}

	/* Power island, reference gates and VBUS first - the USB3 socket
	 * group's VBUS pin is GPIO3_A1, which step 4 above drives with A0. */
	usb_bus_domain_once();

	/* USB3OTG clock gates, then the soft-reset pulse. con9 holds both
	 * USB3OTG cores in reset until firmware releases them (the vendor
	 * boot chain leaves them held); con14 is pulsed here as well so the
	 * whole USB block comes out of this sequence in one known state
	 * rather than two - it is the same pulse the USB2 path would issue,
	 * and running it here, before anything is initialised, bounces
	 * nothing. Held 100 us, then released. */
	*(cru + (USB_BOARD_CRU_CLKGATE_CON10 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_CLKGATE_CON10_BITS, 0U);
	*(cru + (USB_BOARD_CRU_SOFTRST_CON9 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON9_BITS,
			     USB_BOARD_CRU_SOFTRST_CON9_BITS);
	*(cru + (USB_BOARD_CRU_SOFTRST_CON14 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON14_BITS,
			     USB_BOARD_CRU_SOFTRST_CON14_BITS);
	usb_udelay(100U);
	*(cru + (USB_BOARD_CRU_SOFTRST_CON9 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON9_BITS, 0U);
	*(cru + (USB_BOARD_CRU_SOFTRST_CON14 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON14_BITS, 0U);
	usb_udelay(100U);

	/* Release-only again: usb2phy1's POR/port resets and its GRF pclk
	 * (shared with the USB2 roots, so a pulse here would drop them). */
	*(cru + (USB_BOARD_CRU_SOFTRST_CON29 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON29_BITS, 0U);
	*(cru + (USB_BOARD_CRU_SOFTRST_CON28 / 4U)) =
		USB_BOARD_WE(USB_BOARD_CRU_SOFTRST_CON28_BITS, 0U);

	/* usb2phy0's port controls. Its two lanes are shared between the two
	 * xHCI instances (the OTG lane feeds the 0xFCC00000 controller, the
	 * host lane the 0xFD000000 one), so this runs once for both. The
	 * clock output opens first (with its own settle), then the two port
	 * writes with the PHY's 2 ms cadence. */
	*(grf0 + (USB_BOARD_USB2PHY_GRF_CLKOUT_CON2 / 4U)) =
		USB_BOARD_WE(USB_BOARD_USB2PHY_CLKOUT_WE, 0U);
	usb_udelay(100U);
	*(grf0 + (USB_BOARD_USB2PHY_GRF_OTG_CON0 / 4U)) =
		USB_BOARD_WE(USB_BOARD_USB2PHY_OTG_SUS_MASK,
			     USB_BOARD_USB2PHY_OTG_SUS_VAL);
	usb_udelay(2000U);
	*(grf0 + (USB_BOARD_USB2PHY_GRF_HOST_CON1 / 4U)) =
		USB_BOARD_WE(USB_BOARD_USB2PHY_HOST_SUS_MASK,
			     USB_BOARD_USB2PHY_HOST_SUS_VAL);
	usb_udelay(2000U);

	s_usb3_domain_done = 1;
}

/* --- the shared order ----------------------------------------------------- */

void usb_domain_up(void)
{
	/* USB3 socket group first, then the USB2 roots' PHY block: any order
	 * works for the registers, but this one keeps the two con14 pulses of
	 * a warm re-entry together and mirrors the sequence the vendor boot
	 * chain uses. Both calls are once-guarded. */
	usb_usb3_domain_init();
	usb_usb2phy1_domain_init();
}

/* --- one DWC3 core -------------------------------------------------------- */

void usb_xhci_dwc3_host_init(uintptr_t base)
{
	extern void uart_early_puts(const char *);
	volatile uint32_t *dwc3 = (volatile uint32_t *)base;
	uint32_t gctl;
	uint32_t g2phy;
	uint32_t g3pipe;
	uint32_t rev;

	/* Take the core AND both PHYs into soft reset, settle, then release
	 * them. Safe here because the CRU pulse above already left the core
	 * in a cold state - this is the tail of the bring-up, not a reset of
	 * something that is running. 100 ms each way is the PHY block's
	 * reset settling time. */
	dwc3[DWC3_GCTL / 4U] |= DWC3_GCTL_CORESOFTRESET;
	dwc3[DWC3_GUSB3PIPECTL / 4U] |= DWC3_GUSB3PIPECTL_PHYSOFTRST;
	dwc3[DWC3_GUSB2PHYCFG / 4U] |= DWC3_GUSB2PHYCFG_PHYSOFTRST;
	uart_early_puts("dwc3: reset asserted\r\n");
	usb_udelay(100000U);
	uart_early_puts("dwc3: 100ms 1\r\n");
	dwc3[DWC3_GUSB3PIPECTL / 4U] &= ~DWC3_GUSB3PIPECTL_PHYSOFTRST;
	dwc3[DWC3_GUSB2PHYCFG / 4U] &= ~DWC3_GUSB2PHYCFG_PHYSOFTRST;
	usb_udelay(100000U);
	uart_early_puts("dwc3: 100ms 2\r\n");

	/* Close the soft reset. Skipping this clear leaves the whole register
	 * aperture (xHCI and core globals alike) reading zero, and the
	 * controller looks like absent hardware. */
	dwc3[DWC3_GCTL / 4U] &= ~DWC3_GCTL_CORESOFTRESET;

	rev = dwc3[DWC3_GSNPSID / 4U] & DWC3_GSNPSID_REV_MASK;

	/* USB2 PHY: 16-bit UTMI width with the matching turnaround time, and
	 * the low-power/suspend handshakes off. ENBLSLPM is bit 8 on this IP
	 * (writing bit 0 instead leaves the PHY in sleep signalling and every
	 * transfer stalls). */
	g2phy = dwc3[DWC3_GUSB2PHYCFG / 4U];
	g2phy |= DWC3_GUSB2PHYCFG_PHYIF;
	g2phy &= ~DWC3_GUSB2PHYCFG_USBTRDTIM_MASK;
	g2phy |= DWC3_GUSB2PHYCFG_USBTRDTIM_16BIT;
	g2phy &= ~DWC3_GUSB2PHYCFG_ENBLSLPM;
	g2phy &= ~DWC3_GUSB2PHYCFG_U2_FREECLK_EXISTS;
	g2phy &= ~DWC3_GUSB2PHYCFG_SUSPHY;
	dwc3[DWC3_GUSB2PHYCFG / 4U] = g2phy;

	/* USB3 PIPE: no Ux exit handshake (these lanes run USB2 traffic), no
	 * suspend, no delayed PHY power change, and receiver detection left
	 * to the PHY. */
	g3pipe = dwc3[DWC3_GUSB3PIPECTL / 4U];
	g3pipe &= ~DWC3_GUSB3PIPECTL_UX_EXIT_PX;
	g3pipe &= ~DWC3_GUSB3PIPECTL_SUSPENDUSB3;
	g3pipe &= ~DWC3_GUSB3PIPECTL_DEPOCHANGE;
	g3pipe |= DWC3_GUSB3PIPECTL_DISRXDETINP3;
	dwc3[DWC3_GUSB3PIPECTL / 4U] = g3pipe;

	/* High-Speed only: route the USB2 clock into the USB3 clock domain.
	 * Without it link training never completes and the port stays mute.
	 * The core release reported in GSNPSID gates the workaround. */
	if (rev >= 0x290AU) {
		dwc3[DWC3_GUCTL1 / 4U] |=
			DWC3_GUCTL1_DEV_FORCE_20_CLK_FOR_30_CLK;
	}

	/* Speed selection: High-Speed (field value 0), which is the fastest
	 * this board's USB3 connectors carry. */
	dwc3[DWC3_DCFG / 4U] &= ~DWC3_DCFG_SPEED_MASK;

	/* Force the host role. The instance that comes out of reset in the
	 * OTG role never enumerates without it, and leaving the other one on
	 * its default costs nothing. */
	gctl = dwc3[DWC3_GCTL / 4U];
	gctl &= ~DWC3_GCTL_PRTCAP_MASK;
	gctl |= DWC3_GCTL_PRTCAP_DIR(DWC3_GCTL_PRTCAP_HOST);
	dwc3[DWC3_GCTL / 4U] = gctl;
}

/* --- small register readers (the window stays owned here) ----------------- */

uint32_t usb_xhci_core_id(uintptr_t base)
{
	return *(volatile uint32_t *)(base + DWC3_GSNPSID);
}

/* Port power off, settle, power on, settle: the attach of a device already
 * plugged at boot leaves CCS set without ever raising a connect change, so the
 * root hub has nothing to report and enumeration never starts. Dropping and
 * restoring port power forces the edge. */
/* Raise port power and let VBUS ramp: the controller comes out of reset with
 * PP low, and an unpowered port reports no connection at all, so nothing can
 * enumerate until this has run. */
uint32_t usb_ehci_port_power_on(uintptr_t opbase, unsigned port_index)
{
	volatile uint32_t *portsc = (volatile uint32_t *)(opbase +
			USB_BOARD_EHCI_PORTSC_OFFSET + 4U * (uintptr_t)port_index);

	*portsc = (*portsc & ~USB_BOARD_EHCI_PORTSC_PP) | USB_BOARD_EHCI_PORTSC_PP;
	usb_udelay(100000U);
	return *portsc;
}

uint32_t usb_ehci_port_power_cycle(uintptr_t opbase, unsigned port_index)
{
	volatile uint32_t *portsc = (volatile uint32_t *)(opbase +
			USB_BOARD_EHCI_PORTSC_OFFSET + 4U * (uintptr_t)port_index);
	uint32_t regval = *portsc;

	*portsc = regval & ~USB_BOARD_EHCI_PORTSC_PP;
	usb_udelay(30000U);
	*portsc = regval | USB_BOARD_EHCI_PORTSC_PP;
	usb_udelay(30000U);
	return *portsc;
}

uint32_t usb_ehci_port_status(uintptr_t opbase, unsigned port_index)
{
	return *(volatile uint32_t *)(opbase + USB_BOARD_EHCI_PORTSC_OFFSET +
				      (4U * port_index));
}