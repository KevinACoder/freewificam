/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    net/regulatory.h
 * @brief   The regulatory flag bits a driver sets on its wiphy.
 *
 * @design declaration-only compatibility face: the flags are transcribed with the
 *         interface's values (include/net/regulatory.h) because the stack compares them
 *         against the values a driver submits; the request records and the hint entry
 *         point arrive with the consumer that names them.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_NET_REGULATORY_H_
#define _LINUXKPI_NET_REGULATORY_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/net/regulatory.h:160-169 (the enumerators the compiled set names, at
 * their computed positions) */
enum ieee80211_regulatory_flags {
	REGULATORY_STRICT_REG = 2,
	REGULATORY_COUNTRY_IE_IGNORE = 16
};

#endif /* _LINUXKPI_NET_REGULATORY_H_ */
