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

#endif /* _LINUXKPI_LINUX_MMC_SDIO_IDS_H_ */
