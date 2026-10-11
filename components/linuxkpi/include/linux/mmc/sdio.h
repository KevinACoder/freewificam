/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mmc/sdio.h
 * @brief   The SDIO register map and command numbers the interface publishes.
 *
 * @design declaration-only compatibility face, grown per driver. The numbers are
 *         the SDIO specification's own (SDIO Simplified Specification: the CCCR
 *         at function 0, the FBR at each function's own block, the two I/O
 *         commands and the R5 status bits); the interface carries them under
 *         these names, so this face carries the same names and values and
 *         nothing else. Shape reference for a constant-only face:
 *         components/linuxkpi/include/linux/usb.h:28-91.
 *
 *         The engine is the first consumer: it composes the function-enable,
 *         block-size and interrupt-enable sequences from these registers. A
 *         driver that names them reaches the same header.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MMC_SDIO_H_
#define _LINUXKPI_LINUX_MMC_SDIO_H_

/* the two I/O commands (the interface's own numbers, SDIO spec table 5-1) */
#define	SD_IO_SEND_OP_COND		5	/* bcr  [23:0] OCR        R4 */
#define	SD_IO_RW_DIRECT			52	/* ac   [31:0] see below  R5 */
#define	SD_IO_RW_EXTENDED		53	/* adtc [31:0] see below  R5 */

/*
 * SD_IO_RW_DIRECT argument layout, most significant bit first:
 *   [31] write flag, [30:28] function, [27] raw, [25:9] address, [7:0] data.
 * SD_IO_RW_EXTENDED adds [27] block mode and [26] increment address in place of
 * raw, and its low nine bits are a byte count or a block count.
 */
#define	R4_18V_PRESENT			(1u << 24)
#define	R4_MEMORY_PRESENT		(1u << 27)

/* the R5 response's status bits (SDIO spec table 5-1) */
#define	R5_COM_CRC_ERROR		(1u << 15)
#define	R5_ILLEGAL_COMMAND		(1u << 14)
#define	R5_ERROR			(1u << 11)
#define	R5_FUNCTION_NUMBER		(1u << 9)
#define	R5_OUT_OF_RANGE			(1u << 8)
#define	R5_STATUS(x)			((x) & 0xCB00u)
#define	R5_IO_CURRENT_STATE(x)		(((x) & 0x3000u) >> 12)

/* the card common control registers (CCCR, function 0's own space) */
#define	SDIO_CCCR_CCCR			0x00
#define	SDIO_CCCR_REV_1_00		0
#define	SDIO_CCCR_REV_1_10		1
#define	SDIO_CCCR_REV_1_20		2
#define	SDIO_CCCR_REV_3_00		3
#define	SDIO_SDIO_REV_1_00		0
#define	SDIO_SDIO_REV_1_10		1
#define	SDIO_SDIO_REV_1_20		2
#define	SDIO_SDIO_REV_2_00		3
#define	SDIO_SDIO_REV_3_00		4

#define	SDIO_CCCR_SD			0x01
#define	SDIO_SD_REV_1_01		0
#define	SDIO_SD_REV_1_10		1
#define	SDIO_SD_REV_2_00		2
#define	SDIO_SD_REV_3_00		3

#define	SDIO_CCCR_IOEx			0x02	/* I/O function enable */
#define	SDIO_CCCR_IORx			0x03	/* I/O function ready */
#define	SDIO_CCCR_IENx			0x04	/* interrupt enable (master + functions) */
#define	SDIO_CCCR_INTx			0x05	/* interrupt pending */
#define	SDIO_CCCR_ABORT			0x06
#define	SDIO_CCCR_IF			0x07	/* bus interface control */

#define	SDIO_BUS_WIDTH_MASK		0x03
#define	SDIO_BUS_WIDTH_1BIT		0x00
#define	SDIO_BUS_WIDTH_RESERVED		0x01
#define	SDIO_BUS_WIDTH_4BIT		0x02
#define	SDIO_BUS_ECSI			0x20
#define	SDIO_BUS_SCSI			0x40
#define	SDIO_BUS_ASYNC_INT		0x20
#define	SDIO_BUS_CD_DISABLE		0x80

#define	SDIO_CCCR_CAPS			0x08
#define	SDIO_CCCR_CAP_SDC		0x01	/* CMD52 during a data transfer */
#define	SDIO_CCCR_CAP_SMB		0x02	/* multi-block transfers */
#define	SDIO_CCCR_CAP_SRW		0x04	/* read-wait protocol */
#define	SDIO_CCCR_CAP_SBS		0x08	/* suspend/resume */
#define	SDIO_CCCR_CAP_S4MI		0x10	/* interrupt during 4-bit CMD53 */
#define	SDIO_CCCR_CAP_E4MI		0x20	/* enable that interrupt */
#define	SDIO_CCCR_CAP_LSC		0x40	/* low-speed card */
#define	SDIO_CCCR_CAP_4BLS		0x80	/* 4-bit low-speed card */

#define	SDIO_CCCR_CIS			0x09	/* common CIS pointer (3 bytes) */

#define	SDIO_CCCR_SUSPEND		0x0c
#define	SDIO_CCCR_SELx			0x0d
#define	SDIO_CCCR_EXECx			0x0e
#define	SDIO_CCCR_READYx		0x0f

#define	SDIO_CCCR_BLKSIZE		0x10
#define	SDIO_CCCR_POWER			0x12
#define	SDIO_POWER_SMPC			0x01
#define	SDIO_POWER_EMPC			0x02

#define	SDIO_CCCR_SPEED			0x13
#define	SDIO_SPEED_SHS			0x01
#define	SDIO_SPEED_BSS_SHIFT		1
#define	SDIO_SPEED_BSS_MASK		(7u << SDIO_SPEED_BSS_SHIFT)
#define	SDIO_SPEED_SDR12		(0u << SDIO_SPEED_BSS_SHIFT)
#define	SDIO_SPEED_SDR25		(1u << SDIO_SPEED_BSS_SHIFT)
#define	SDIO_SPEED_SDR50		(2u << SDIO_SPEED_BSS_SHIFT)
#define	SDIO_SPEED_SDR104		(3u << SDIO_SPEED_BSS_SHIFT)
#define	SDIO_SPEED_DDR50		(4u << SDIO_SPEED_BSS_SHIFT)
#define	SDIO_SPEED_EHS			SDIO_SPEED_SDR25

#define	SDIO_CCCR_UHS			0x14
#define	SDIO_UHS_SDR50			0x01
#define	SDIO_UHS_SDR104			0x02
#define	SDIO_UHS_DDR50			0x04

#define	SDIO_CCCR_DRIVE_STRENGTH	0x15
#define	SDIO_SDTx_MASK			0x07
#define	SDIO_DRIVE_SDTA			(1u << 0)
#define	SDIO_DRIVE_SDTC			(1u << 1)
#define	SDIO_DRIVE_SDTD			(1u << 2)
#define	SDIO_DRIVE_DTSx_MASK		0x03
#define	SDIO_DRIVE_DTSx_SHIFT		4
#define	SDIO_DTSx_SET_TYPE_B		(0u << SDIO_DRIVE_DTSx_SHIFT)
#define	SDIO_DTSx_SET_TYPE_A		(1u << SDIO_DRIVE_DTSx_SHIFT)
#define	SDIO_DTSx_SET_TYPE_C		(2u << SDIO_DRIVE_DTSx_SHIFT)
#define	SDIO_DTSx_SET_TYPE_D		(3u << SDIO_DRIVE_DTSx_SHIFT)

#define	SDIO_CCCR_INTERRUPT_EXT		0x16
#define	SDIO_INTERRUPT_EXT_SAI		(1u << 0)
#define	SDIO_INTERRUPT_EXT_EAI		(1u << 1)

/*
 * The function basic registers (FBR): each function's own 256-byte block, so a
 * function's FBR address is its base plus the register offset.
 */
#define	SDIO_FBR_BASE(f)		((f) * 0x100)

#define	SDIO_FBR_STD_IF			0x00
#define	SDIO_FBR_SUPPORTS_CSA		0x40
#define	SDIO_FBR_ENABLE_CSA		0x80
#define	SDIO_FBR_STD_IF_EXT		0x01
#define	SDIO_FBR_POWER			0x02
#define	SDIO_FBR_POWER_SPS		0x01
#define	SDIO_FBR_POWER_EPS		0x02
#define	SDIO_FBR_CIS			0x09	/* CIS pointer (3 bytes) */
#define	SDIO_FBR_CSA			0x0c	/* CSA pointer (3 bytes) */
#define	SDIO_FBR_CSA_DATA		0x0f
#define	SDIO_FBR_BLKSIZE		0x10	/* block size (2 bytes) */

#endif /* _LINUXKPI_LINUX_MMC_SDIO_H_ */
