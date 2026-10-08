/*
 * @file   usb_dwc3.h
 * @brief  DesignWare DWC3 core-global register facts (board constants).
 *
 * The RK3568's two USB3 instances wrap a DWC3 core whose xHCI host
 * interface is at the controller base and whose core-global registers start
 * at +0xC100 (the offsets below are relative to the controller base). The
 * values are the DesignWare reference definitions, verified against the core
 * ID this board reports: the soft reset that must be closed with the
 * GCTL.CORESOFTRESET clear (a core still in soft reset reads zero through
 * its whole aperture), the PHY quirk set, the High-Speed-only clock routing
 * workaround, and the forced device role the OTG instance needs.
 *
 * Only usb_domain.c consumes these; they live here so the register facts of
 * one IP block stay in one place.
 */

#ifndef FREEWEBCAM_RK3568_USB_DWC3_H
#define FREEWEBCAM_RK3568_USB_DWC3_H

/* --- global register offsets (relative to the controller base) ------------- */

#define DWC3_GCTL		0xC110U
#define DWC3_GSNPSID		0xC120U
#define DWC3_GUCTL1		0xC11CU
#define DWC3_GUSB2PHYCFG	0xC200U
#define DWC3_GUSB3PIPECTL	0xC2C0U
#define DWC3_DCFG		0xC700U

/* The core ID register carries the release number in its low half word; the
 * High-Speed routing workaround below is gated on it. */
#define DWC3_GSNPSID_REV_MASK	0xFFFFU

/* GCTL */
#define DWC3_GCTL_CORESOFTRESET		(1U << 11)
#define DWC3_GCTL_PRTCAP_DIR(x)		((x) << 12)
#define DWC3_GCTL_PRTCAP_MASK		(3U << 12)
#define DWC3_GCTL_PRTCAP_HOST		1U

/* GUSB2PHYCFG */
#define DWC3_GUSB2PHYCFG_PHYSOFTRST	(1U << 31)
#define DWC3_GUSB2PHYCFG_U2_FREECLK_EXISTS	(1U << 30)
#define DWC3_GUSB2PHYCFG_ENBLSLPM	(1U << 8)	/* bit 8, not bit 0 */
#define DWC3_GUSB2PHYCFG_SUSPHY		(1U << 6)
#define DWC3_GUSB2PHYCFG_PHYIF		(1U << 3)
#define DWC3_GUSB2PHYCFG_USBTRDTIM(x)	((x) << 10)
#define DWC3_GUSB2PHYCFG_USBTRDTIM_MASK	DWC3_GUSB2PHYCFG_USBTRDTIM(0xFU)
/* 16-bit PHY interface with the matching turnaround time; the PHY block on
 * this board is wired for the 16-bit UTMI width. */
#define DWC3_GUSB2PHYCFG_USBTRDTIM_16BIT	DWC3_GUSB2PHYCFG_USBTRDTIM(0x5U)

/* GUSB3PIPECTL */
#define DWC3_GUSB3PIPECTL_PHYSOFTRST	(1U << 31)
#define DWC3_GUSB3PIPECTL_DISRXDETINP3	(1U << 28)
#define DWC3_GUSB3PIPECTL_UX_EXIT_PX	(1U << 27)
#define DWC3_GUSB3PIPECTL_DEPOCHANGE	(1U << 18)
#define DWC3_GUSB3PIPECTL_SUSPENDUSB3	(1U << 17)

/* DCFG: bits 2:0 select the device speed; 0 = High-Speed. */
#define DWC3_DCFG_SPEED_MASK	(7U << 0)

/* GUCTL1 bit 26: force the USB2 clock into the USB3 clock routing. Required
 * on this part for High-Speed operation; without it link training never
 * completes and nothing enumerates. Documented upstream as applying from
 * release 0x290a on. */
#define DWC3_GUCTL1_DEV_FORCE_20_CLK_FOR_30_CLK	(1U << 26)

#endif /* FREEWEBCAM_RK3568_USB_DWC3_H */