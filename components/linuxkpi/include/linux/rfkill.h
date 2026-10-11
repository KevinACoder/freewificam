/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/rfkill.h
 * @brief   The rfkill switch surface a wireless driver's platform layer names.
 *
 * @design declaration-only compatibility face. The switch record and the type
 *         enumeration are the interface's own (include/uapi/linux/rfkill.h): the values
 *         are the block reasons a driver reports, so they are contract. This port has no
 *         rfkill switch; the entries whose bodies the compiled set reaches are stated in
 *         the contract header as the answers of a port without one, and a caller keeps
 *         its default path.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_RFKILL_H_
#define _LINUXKPI_LINUX_RFKILL_H_

#include "../../linuxkpi_core.h"

struct rfkill;

/* [LNX] include/uapi/linux/rfkill.h:66-84 (the enumerators the compiled set names) */
enum rfkill_type {
	RFKILL_TYPE_ALL = 0,
	RFKILL_TYPE_WLAN,
	RFKILL_TYPE_BLUETOOTH,
	RFKILL_TYPE_UWB,
	RFKILL_TYPE_WIMAX,
	RFKILL_TYPE_WWAN,
	RFKILL_TYPE_GPS,
	RFKILL_TYPE_FM,
	RFKILL_TYPE_NFC,
	NUM_RFKILL_TYPES,
};

enum rfkill_operation {
	RFKILL_OP_ADD = 0,
	RFKILL_OP_DEL,
	RFKILL_OP_CHANGE,
	RFKILL_OP_CHANGE_ALL,
};

enum rfkill_hard_block_reasons {
	RFKILL_HARD_BLOCK_SIGNAL	= 1 << 0,
	RFKILL_HARD_BLOCK_NOT_OWNER	= 1 << 1,
};

#endif /* _LINUXKPI_LINUX_RFKILL_H_ */
