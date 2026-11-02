/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/sizes.h
 * @brief   The power-of-two byte-count constants a driver's buffer arithmetic
 *          names.
 *
 * @design declaration-only compatibility face, grown per driver.  The interface
 *         (include/linux/sizes.h) is a flat list of literals and nothing else -
 *         no type, no function, no state.  This face carries the same list for
 *         the same reason and reaches it the way the interface does: the
 *         interface's io.h includes it (include/linux/io.h:9), and the MMC host
 *         header reaches io.h through iopoll.h, so a driver that names SZ_64K
 *         while talking about a transfer size needs no include of its own
 *         (rtw88/sdio.c:1074 does exactly that).
 *
 *         Two deliberate differences from the interface: the constants above
 *         4 GiB are spelled with a ULL suffix instead of _AC(), because this
 *         project has no linux/const.h and a self-contained literal is the
 *         same value; and the list stops at the sizes a compiled unit names
 *         today, since a constant with no consumer is not a contract - it grows
 *         with the next driver that asks, exactly like this face's sibling
 *         headers.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    02.11.2026
 */

#ifndef _LINUXKPI_LINUX_SIZES_H_
#define _LINUXKPI_LINUX_SIZES_H_

#define	SZ_1				0x00000001
#define	SZ_2				0x00000002
#define	SZ_4				0x00000004
#define	SZ_8				0x00000008
#define	SZ_16				0x00000010
#define	SZ_32				0x00000020
#define	SZ_64				0x00000040
#define	SZ_128				0x00000080
#define	SZ_256				0x00000100
#define	SZ_512				0x00000200

#define	SZ_1K				0x00000400
#define	SZ_2K				0x00000800
#define	SZ_4K				0x00001000
#define	SZ_8K				0x00002000
#define	SZ_16K				0x00004000
#define	SZ_32K				0x00008000
#define	SZ_64K				0x00010000
#define	SZ_128K				0x00020000
#define	SZ_256K				0x00040000
#define	SZ_512K				0x00080000

#define	SZ_1M				0x00100000
#define	SZ_2M				0x00200000
#define	SZ_4M				0x00400000
#define	SZ_8M				0x00800000
#define	SZ_16M				0x01000000
#define	SZ_32M				0x02000000
#define	SZ_64M				0x04000000

#define	SZ_1G				0x40000000
#define	SZ_2G				0x80000000
#define	SZ_4G				0x100000000ULL
#define	SZ_8G				0x200000000ULL
#define	SZ_16G				0x400000000ULL

#endif /* _LINUXKPI_LINUX_SIZES_H_ */
