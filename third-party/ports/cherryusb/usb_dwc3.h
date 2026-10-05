/*
 * @file   usb_dwc3.h
 * @brief  The DWC3 global register facts the xHCI line needs (the
 *         DesignWare core the RK3568 USB3 socket group wraps).
 *
 * Offsets are relative to the controller base, whose xHCI register
 * aperture sits at +0 and whose core globals start at +0xC100
 * (usb_board.h).  The values are the standard DesignWare definitions,
 * carried board-proven through the net_80211 line's xHCI port
 * (DESIGN D38): the soft reset that must be closed with the
 * GCTL.CORESOFTRESET clear, the PHY quirk set, and the OTG instance's
 * forced PRTCAP=host.
 *
 * @date 25.09.2026
 * @author zhugengyu
 */

#ifndef USB_DWC3_H
#define USB_DWC3_H

/* --- global register offsets (relative to the controller base) --- */
#define DWC3_GCTL		0xC110U
#define DWC3_GSNPSID		0xC120U
#define DWC3_GUCTL1		0xC11CU
#define DWC3_GUSB2PHYCFG	0xC200U
#define DWC3_GUSB3PIPECTL	0xC2C0U
#define DWC3_DCFG		0xC700U

#define DWC3_GSNPSID_REV_MASK	0xFFFFU

/* GCTL */
#define DWC3_GCTL_CORESOFTRESET	(1U << 11)
#define DWC3_GCTL_PRTCAPDIR(x)	((x) << 12)
#define DWC3_GCTL_PRTCAP_MASK	(3U << 12)
#define DWC3_GCTL_PRTCAP_HOST	1U

/* GUSB2PHYCFG */
#define DWC3_GUSB2PHYCFG_PHYSOFTRST	(1U << 31)
#define DWC3_GUSB2PHYCFG_U2_FREECLK_EXISTS (1U << 30)
#define DWC3_GUSB2PHYCFG_ENBLSLPM	(1U << 8)	/* bit8, not bit0 */
#define DWC3_GUSB2PHYCFG_SUSPHY		(1U << 6)
#define DWC3_GUSB2PHYCFG_PHYIF		(1U << 3)
#define DWC3_GUSB2PHYCFG_USBTRDTIM(x)	((x) << 10)
#define DWC3_GUSB2PHYCFG_USBTRDTIM_MASK	DWC3_GUSB2PHYCFG_USBTRDTIM(0xFU)
#define DWC3_GUSB2PHYCFG_USBTRDTIM_16BIT DWC3_GUSB2PHYCFG_USBTRDTIM(0x5U)

/* GUSB3PIPECTL */
#define DWC3_GUSB3PIPECTL_PHYSOFTRST	(1U << 31)
#define DWC3_GUSB3PIPECTL_DISRXDETINP3	(1U << 28)
#define DWC3_GUSB3PIPECTL_UX_EXIT_PX	(1U << 27)
#define DWC3_GUSB3PIPECTL_DEPOCHANGE	(1U << 18)
#define DWC3_GUSB3PIPECTL_SUSPENDUSB3	(1U << 17)

/* DCFG */
#define DWC3_DCFG_SPEED_MASK	(7U << 0)

/* GUCTL1 */
#define DWC3_GUCTL1_DEV_FORCE_20_CLK_FOR_30_CLK	(1U << 26)

#endif /* USB_DWC3_H */
