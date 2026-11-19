/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mmc/sdio_ids.h
 * @brief   The SDIO standard-interface classes and vendor codes.
 *
 * @design declaration-only compatibility face, grown per driver. The class
 *         values are the SDIO specification's standard-interface codes (SDIO
 *         Simplified Specification table 6-1) and the vendor code is the SDIO
 *         vendor number (the manufacturer code the CIS reports). A driver's id
 *         table names these and otherwise carries its own part numbers, which is
 *         what the target driver does; this face therefore publishes the classes
 *         and the one vendor code the line was grown for, and grows again when a
 *         driver names another.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MMC_SDIO_IDS_H_
#define _LINUXKPI_LINUX_MMC_SDIO_IDS_H_

/* standard interface classes (CIS function-extension tuple, TPLFE_STD_IF) */
#define	SDIO_CLASS_NONE		0x00	/* not a standard interface */
#define	SDIO_CLASS_UART		0x01
#define	SDIO_CLASS_BT_A		0x02
#define	SDIO_CLASS_BT_B		0x03
#define	SDIO_CLASS_GPS		0x04
#define	SDIO_CLASS_CAMERA	0x05
#define	SDIO_CLASS_PHS		0x06
#define	SDIO_CLASS_WLAN		0x07
#define	SDIO_CLASS_ATA		0x08
#define	SDIO_CLASS_BT_AMP	0x09

/* the vendor code of the parts this face was grown for */
#define	SDIO_VENDOR_ID_REALTEK	0x024c

/*
 * The part numbers of that vendor's SDIO parts. [LNX] include/linux/mmc/sdio_ids.h:131
 * carries the whole family; this face grows one entry per driver that registers
 * an id table, which is what the header's own @design note says it does. The
 * value is the card's manufacturer information from its CIS, and it is the
 * second half of the `024c:c821` pair this board's socket enumerates
 * (rtw88/rtw8821cs.c:15 names it in the driver's id table).
 */
#define	SDIO_DEVICE_ID_REALTEK_RTW8821CS	0xc821

/*
 * [LNX] include/linux/mmc/sdio_ids.h:56-83: the Broadcom vendor code and the parts
 * brcmfmac's SDIO id table names. The numbers are the manufacturer/product codes the
 * cards report in their CIS; the table is transcribed whole for this vendor because a
 * driver's id table matches on it (brcmfmac/bcmsdh.c's brcm_sdmmc_ids) and a missing
 * entry would silently leave a supported part unbound. The line's own board is the
 * AP6181, which is the BCM43362 (0xa962).
 */
#define	SDIO_VENDOR_ID_BROADCOM			0x02d0
/* [LNX] include/linux/mmc/sdio_ids.h:82: the Cypress vendor code; the wireless parts
 * that moved to Cypress keep their Broadcom product numbers, and brcmfmac's id table
 * matches them under this vendor (bcmsdh.c:975 names a CYPRESS part). */
#define	SDIO_VENDOR_ID_CYPRESS			0x04b4
#define	SDIO_DEVICE_ID_BROADCOM_43241		0x4324
#define	SDIO_DEVICE_ID_BROADCOM_4329		0x4329
#define	SDIO_DEVICE_ID_BROADCOM_4330		0x4330
#define	SDIO_DEVICE_ID_BROADCOM_4334		0x4334
#define	SDIO_DEVICE_ID_BROADCOM_4335_4339	0x4335
#define	SDIO_DEVICE_ID_BROADCOM_4339		0x4339
#define	SDIO_DEVICE_ID_BROADCOM_4345		0x4345
#define	SDIO_DEVICE_ID_BROADCOM_4354		0x4354
#define	SDIO_DEVICE_ID_BROADCOM_CYPRESS_89359	0x4355
#define	SDIO_DEVICE_ID_BROADCOM_4356		0x4356
#define	SDIO_DEVICE_ID_BROADCOM_4359		0x4359
#define	SDIO_DEVICE_ID_BROADCOM_CYPRESS_4373	0x4373
#define	SDIO_DEVICE_ID_BROADCOM_CYPRESS_43012	0xa804
#define	SDIO_DEVICE_ID_BROADCOM_43143		0xa887
#define	SDIO_DEVICE_ID_BROADCOM_43340		0xa94c
#define	SDIO_DEVICE_ID_BROADCOM_43341		0xa94d
#define	SDIO_DEVICE_ID_BROADCOM_43362		0xa962
#define	SDIO_DEVICE_ID_BROADCOM_43364		0xa9a4
#define	SDIO_DEVICE_ID_BROADCOM_43430		0xa9a6
#define	SDIO_DEVICE_ID_BROADCOM_43439		0xa9af
#define	SDIO_DEVICE_ID_BROADCOM_43455		0xa9bf
#define	SDIO_DEVICE_ID_BROADCOM_43751		0xaae7
#define	SDIO_DEVICE_ID_BROADCOM_43752		0xaae8
#define	SDIO_DEVICE_ID_BROADCOM_CYPRESS_43439	0xbd3d

#endif /* _LINUXKPI_LINUX_MMC_SDIO_IDS_H_ */
