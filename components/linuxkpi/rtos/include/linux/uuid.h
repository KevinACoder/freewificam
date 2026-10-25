/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/uuid.h
 * @brief   The 128-bit identifier records a driver passes to firmware and platform
 *          interfaces.
 *
 * @design declaration-only compatibility face. Both records are byte arrays of the
 *         interface's size (include/linux/uuid.h), and the two initializers differ in the
 *         byte order of their first three fields - GUID_INIT writes them little-endian,
 *         UUID_INIT big-endian - which is why both spellings exist and why they are
 *         transcribed with the interface's exact arithmetic: these bytes are compared
 *         against identifiers firmware hands back, so the layout is contract, not
 *         cosmetics.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_UUID_H_
#define _LINUXKPI_LINUX_UUID_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/uuid.h:11-21 */
#define	UUID_SIZE	16

typedef struct {
	__u8 b[UUID_SIZE];
} guid_t;

typedef struct {
	__u8 b[UUID_SIZE];
} uuid_t;

/* [LNX] include/linux/uuid.h:23-28 */
#define	GUID_INIT(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)		\
((guid_t)								\
{{ (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
   (b) & 0xff, ((b) >> 8) & 0xff,					\
   (c) & 0xff, ((c) >> 8) & 0xff,					\
   (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }})

/* [LNX] include/linux/uuid.h:30-36 */
#define	UUID_INIT(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)		\
((uuid_t)								\
{{ ((a) >> 24) & 0xff, ((a) >> 16) & 0xff, ((a) >> 8) & 0xff, (a) & 0xff, \
   ((b) >> 8) & 0xff, (b) & 0xff,					\
   ((c) >> 8) & 0xff, (c) & 0xff,					\
   (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }})

/* [LNX] include/linux/uuid.h:41 */
#define	UUID_STRING_LEN		36

#endif /* _LINUXKPI_LINUX_UUID_H_ */
