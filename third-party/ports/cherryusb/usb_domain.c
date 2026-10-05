/*
 * @file
 * @brief The backend-neutral half of the RK3568 USB platform bring-up.
 *
 * Moved verbatim out of port/adapters/libbsd/usb_platform.c so the
 * CherryUSB backend can run the same sequence without linking the NetBSD
 * world (see usb_domain.h for the provenance and the ordering rules).
 *
 * @date 27.09.2026
 * @author zhugengyu
 */

#include <stdint.h>
#include <stdbool.h>

#include "usb_board.h"
#include "generic_timer.h"
#include "usb_domain.h"
#include "usb_dwc3.h"

static bool s_usb_bus_domain_done;
static bool s_usb2phy1_domain_done;
static bool s_usb3_domain_done;

/* Microsecond busy-wait off the ARM generic timer (always on at EL1). */
void usb_udelay(uint32_t usec)
{
	uint64_t start, now, ticks, freq;

	freq = gtimer_freq_get();
	ticks = (freq * (uint64_t)usec) / 1000000U;
	if (ticks == 0U) {
		ticks = 1U;
	}
	start = gtimer_count_get();
	for (;;) {
		now = gtimer_count_get();
		if (now - start >= ticks) {
			break;
		}
	}
}

/* PD_PIPE power island on + PHY reference clock gates + VBUS enable.
 * Bus island: request idle, wait for the ack, ungate the domain, wait
 * for the power-down status to clear (the standalone-line sequence).
 * Shared: the xHCI line's USB3 domain sequence rides on it. */
void usb_bus_domain_once(void)
{
	volatile uint32_t *pmu = (volatile uint32_t *)USBH_PMU_BASE;
	volatile uint32_t *pmucru = (volatile uint32_t *)USBH_PMUCRU_BASE;
	volatile uint32_t *ddr = (volatile uint32_t *)(USBH_GPIO3_BASE +
						       USBH_GPIO3_SWPORT_DDR);
	volatile uint32_t *dr = (volatile uint32_t *)(USBH_GPIO3_BASE +
						      USBH_GPIO3_SWPORT_DR);
	uint32_t i;

	if (s_usb_bus_domain_done) {
		return;
	}

	/* PD_PIPE bus idle request release (write-enable only, data 0),
	 * then wait for the ack to clear. */
	*(pmu + USBH_PMU_BUS_IDLE_SFTCON0 / 4U) =
		GRF_WR(USBH_PMU_PD_PIPE_IDLE_BIT, 0U);
	for (i = 0; i < 100U; i++) {
		if ((*(pmu + USBH_PMU_BUS_IDLE_ACK / 4U) &
		     USBH_PMU_PD_PIPE_IDLE_BIT) == 0U) {
			break;
		}
		usb_udelay(100);
	}

	/* Power the domain on and wait for the power-down status to clear. */
	*(pmu + USBH_PMU_PWR_GATE_SFTCON / 4U) =
		GRF_WR(USBH_PMU_PD_PIPE_BIT, 0U);
	for (i = 0; i < 100U; i++) {
		if ((*(pmu + USBH_PMU_PWR_DWN_ST / 4U) &
		     USBH_PMU_PD_PIPE_BIT) == 0U) {
			break;
		}
		usb_udelay(100);
	}

	/* PHY reference clocks (usbphy0 + usbphy1 + ref24m): CRU clkgate
	 * polarity is low-bit 1 = gate OFF, so data 0 turns them on. */
	*(pmucru + USBH_PMUCRU_CLKGATE_CON2 / 4U) =
		GRF_WR(USBH_PMUCRU_USBPHY_GATES, 0U);

	/* VBUS: GPIO3_A0/A1 as output-high (hi-word = direction write, low
	 * word = data write; both set the pin). Without the pull-up the
	 * ports have no power at all (KI-004), and a cold VBUS ramp is
	 * what makes pre-inserted devices enumerate (KI-006). */
	*ddr = GRF_WR(USBH_VBUS_PINS, USBH_VBUS_PINS);
	*dr = GRF_WR(USBH_VBUS_PINS, USBH_VBUS_PINS);

	s_usb_bus_domain_done = true;
}

/* The usb2phy1 domain for the panel EHCI roots (both roots share the
 * one PHY domain; runs once per boot). Register-exact net_80211 line. */
void usb_usb2phy1_domain_init(void)
{
	volatile uint32_t *cru = (volatile uint32_t *)USBH_CRU_BASE;
	volatile uint32_t *pmucru = (volatile uint32_t *)USBH_PMUCRU_BASE;
	volatile uint32_t *grf =
		(volatile uint32_t *)USBH_USB2PHY1_GRF_BASE;

	if (s_usb2phy1_domain_done) {
		return;
	}
	if (!s_usb_bus_domain_done) {
		usb_bus_domain_once();
	}

	/* PMUCRU: the usb2phy1 reference bit on top of the bus domain
	 * ungates (clkgate data 0 = clock enabled). */
	*(pmucru + USBH_PMUCRU_CLKGATE_CON2 / 4U) =
		GRF_WR(USBH_PMUCRU_USBPHY_GATES, 0U);

	/* USB2HOST0/1 + their ARB/UTMI interfaces: assert, settle, release. */
	*(cru + USBH_CRU_SOFTRST_CON14 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON14_BITS,
		       USBH_CRU_SOFTRST_CON14_BITS);
	usb_udelay(100);
	*(cru + USBH_CRU_SOFTRST_CON14 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON14_BITS, 0U);
	usb_udelay(100);

	/* PHY1 POR/port reset and the GRF pclk: release only, never
	 * assert - asserting the POR would drop a PHY the other bus uses. */
	*(cru + USBH_CRU_SOFTRST_CON29 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON29_BITS, 0U);
	usb_udelay(100);
	*(cru + USBH_CRU_SOFTRST_CON28 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON28_BITS, 0U);
	usb_udelay(100);

	/* Port GRF: otg-port then host-port suspend release, then open the
	 * 480m UTMI clock output; 2 ms settle per write (NetBSD
	 * rk_usb2phy_port_write cadence). */
	*(grf + USBH_USB2PHY_GRF_OTG_CON0 / 4U) =
		GRF_WR(USBH_USB2PHY_OTG_SUS_MASK, USBH_USB2PHY_OTG_SUS_VAL);
	usb_udelay(2000);
	*(grf + USBH_USB2PHY_GRF_HOST_CON1 / 4U) =
		GRF_WR(USBH_USB2PHY_HOST_SUS_MASK, USBH_USB2PHY_HOST_SUS_VAL);
	usb_udelay(2000);
	*(grf + USBH_USB2PHY_GRF_CLKOUT_CON2 / 4U) =
		GRF_WR(USBH_USB2PHY_CLKOUT_WE, 0U);
	usb_udelay(2000);

	s_usb2phy1_domain_done = true;
}

/* The USB3 socket-group domain (once per boot).  Register-exact move of the
 * sequence usb_xhci_platform.c ran on the NetBSD side; the diagnostic
 * printfs stayed behind there (the CherryUSB glue prints its own GSNPSID
 * anchor after usb_xhci_dwc3_host_init). */
void usb_usb3_domain_init(void)
{
	volatile uint32_t *cru = (volatile uint32_t *)USBH_CRU_BASE;
	volatile uint32_t *grf0 =
		(volatile uint32_t *)USBH_USB2PHY0_GRF_BASE;

	if (s_usb3_domain_done) {
		return;
	}

	/* PD_PIPE bus island + the PHY reference clocks + VBUS (the
	 * USB3 socket group hangs off GPIO3_A1; a cold VBUS ramp is
	 * what makes pre-inserted devices enumerate, KI-006). */
	usb_bus_domain_once();

	/* USB3OTG clock gates (con10: otg0 + otg1 + the pipe family, in
	 * case the SATA combphy line has not run yet).  Gate polarity:
	 * low bit 1 = off, data 0 = enabled. */
	*(cru + USBH_CRU_CLKGATE_CON10 / 4U) =
		GRF_WR(USBH_CRU_CLKGATE_CON10_BITS, 0U);

	/* Assert + release the SRST pulse: con9 (USB3OTG0/1) plus the
	 * same con14 (USB2HOST) pulse the usb2phy1 line uses - run
	 * here, ahead of both attaches, it bounces nothing (D50). */
	*(cru + USBH_CRU_SOFTRST_CON9 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON9_BITS,
		       USBH_CRU_SOFTRST_CON9_BITS);
	*(cru + USBH_CRU_SOFTRST_CON14 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON14_BITS,
		       USBH_CRU_SOFTRST_CON14_BITS);
	usb_udelay(100);
	*(cru + USBH_CRU_SOFTRST_CON9 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON9_BITS, 0U);
	*(cru + USBH_CRU_SOFTRST_CON14 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON14_BITS, 0U);
	usb_udelay(100);

	/* Release-only: usb2phy1 POR/port resets + its GRF pclk. */
	*(cru + USBH_CRU_SOFTRST_CON29 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON29_BITS, 0U);
	*(cru + USBH_CRU_SOFTRST_CON28 / 4U) =
		GRF_WR(USBH_CRU_SOFTRST_CON28_BITS, 0U);

	/* usb2phy0 port GRF: 480m clock output, otg-port host role,
	 * host-port suspend release - the KI-006 measured working
	 * values, 2 ms settle per port write (the NetBSD
	 * rk_usb2phy_port_write cadence).  Both usb2phy0 lanes are
	 * shared domain resources: the otg-port lane feeds the
	 * 0xFCC00000 instance, the host-port lane the 0xFD000000 one. */
	*(grf0 + USBH_USB2PHY_GRF_CLKOUT_CON2 / 4U) =
		GRF_WR(USBH_USB2PHY_CLKOUT_WE, 0U);
	usb_udelay(100);
	*(grf0 + USBH_USB2PHY_GRF_OTG_CON0 / 4U) =
		GRF_WR(USBH_USB2PHY_OTG_SUS_MASK, USBH_USB2PHY_OTG_SUS_VAL);
	usb_udelay(2000);
	*(grf0 + USBH_USB2PHY_GRF_HOST_CON1 / 4U) =
		GRF_WR(USBH_USB2PHY_HOST_SUS_MASK, USBH_USB2PHY_HOST_SUS_VAL);
	usb_udelay(2000);

	s_usb3_domain_done = true;
}

/* dwc3_fdt.c's core soft-reset + enable_phy + set_mode for one core (moved from
 * usb_xhci_platform.c; osDelay became the shared usb_udelay). */
void usb_xhci_dwc3_host_init(uintptr_t base)
{
	volatile uint32_t *dwc3 = (volatile uint32_t *)base;
	uint32_t gctl, guctl1, g2phy, g3pipe, dcfg, rev;

	/* Core, then both PHYs, into reset; settle 100 ms each way.
	 * Safe here (and was not in the D36 experiments) because the
	 * CRU SRST pulse has already taken the core to a cold state -
	 * the NetBSD order, not a reset of a running controller. */
	dwc3[DWC3_GCTL / 4U] |= DWC3_GCTL_CORESOFTRESET;
	dwc3[DWC3_GUSB3PIPECTL / 4U] |= DWC3_GUSB3PIPECTL_PHYSOFTRST;
	dwc3[DWC3_GUSB2PHYCFG / 4U] |= DWC3_GUSB2PHYCFG_PHYSOFTRST;
	usb_udelay(100000);
	dwc3[DWC3_GUSB3PIPECTL / 4U] &= ~DWC3_GUSB3PIPECTL_PHYSOFTRST;
	dwc3[DWC3_GUSB2PHYCFG / 4U] &= ~DWC3_GUSB2PHYCFG_PHYSOFTRST;
	usb_udelay(100000);

	/* Take the core out of reset - dwc3_fdt's soft reset closing
	 * clear.  Skipped, the whole xHCI register window stays inside
	 * the soft reset and reads zero. */
	dwc3[DWC3_GCTL / 4U] &= ~DWC3_GCTL_CORESOFTRESET;

	rev = dwc3[DWC3_GSNPSID / 4U] & DWC3_GSNPSID_REV_MASK;

	/* utmi_wide: 16-bit PHYIF + TRDTIM 5; dis_enblslpm (the KI-012
	 * fix: ENBLSLPM is bit8, not bit0) / dis-u2-freeclk /
	 * dis_u2_susphy. */
	g2phy = dwc3[DWC3_GUSB2PHYCFG / 4U];
	g2phy |= DWC3_GUSB2PHYCFG_PHYIF;
	g2phy &= ~DWC3_GUSB2PHYCFG_USBTRDTIM_MASK;
	g2phy |= DWC3_GUSB2PHYCFG_USBTRDTIM_16BIT;
	g2phy &= ~DWC3_GUSB2PHYCFG_ENBLSLPM;
	g2phy &= ~DWC3_GUSB2PHYCFG_U2_FREECLK_EXISTS;
	g2phy &= ~DWC3_GUSB2PHYCFG_SUSPHY;
	dwc3[DWC3_GUSB2PHYCFG / 4U] = g2phy;

	/* GUSB3PIPECTL: clear UX_EXIT_PX; dis_u3_susphy; dis-del-phy-
	 * power-chg; dis-rxdet-inp3. */
	g3pipe = dwc3[DWC3_GUSB3PIPECTL / 4U];
	g3pipe &= ~DWC3_GUSB3PIPECTL_UX_EXIT_PX;
	g3pipe &= ~DWC3_GUSB3PIPECTL_SUSPENDUSB3;
	g3pipe &= ~DWC3_GUSB3PIPECTL_DEPOCHANGE;
	g3pipe |= DWC3_GUSB3PIPECTL_DISRXDETINP3;
	dwc3[DWC3_GUSB3PIPECTL / 4U] = g3pipe;

	/* HS-only: force the USB2 clock into the USB3 routing, or link
	 * training never completes (the KI-012 root cause).  NetBSD
	 * gates on rev >= 0x290a && maximum-speed = high-speed; this IP
	 * is 0x300a. */
	if (rev >= 0x290AU) {
		guctl1 = dwc3[DWC3_GUCTL1 / 4U];
		guctl1 |= DWC3_GUCTL1_DEV_FORCE_20_CLK_FOR_30_CLK;
		dwc3[DWC3_GUCTL1 / 4U] = guctl1;
	}

	/* DCFG speed = high-speed (0). */
	dcfg = dwc3[DWC3_DCFG / 4U];
	dcfg &= ~DWC3_DCFG_SPEED_MASK;
	dwc3[DWC3_DCFG / 4U] = dcfg;

	/* dwc3_fdt set_mode(host): the upper-port instance is the OTG
	 * controller - without the forced PRTCAP nothing enumerates. */
	gctl = dwc3[DWC3_GCTL / 4U];
	gctl &= ~DWC3_GCTL_PRTCAP_MASK;
	gctl |= DWC3_GCTL_PRTCAPDIR(DWC3_GCTL_PRTCAP_HOST);
	dwc3[DWC3_GCTL / 4U] = gctl;
}
