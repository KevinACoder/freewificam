/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/fips.h
 * @brief   The FIPS-mode query a driver consults before it advertises a crypto
 *          capability.
 *
 * @design declaration-only compatibility face. The interface gates the whole
 *         header on CONFIG_CRYPTO_FIPS: with the option off the query is the
 *         constant 0 (include/linux/fips.h:14), and with it on the query is an
 *         exported flag plus a failure-notifier chain. This port does not select
 *         the option, so only the constant branch is reachable, and only the
 *         query itself is named by a compiled consumer; the notifier surface of
 *         the enabled branch is not stated. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_FIPS_H_
#define _LINUXKPI_LINUX_FIPS_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/fips.h:14: the CONFIG_CRYPTO_FIPS-off arm, which is the
 * one this port's configuration selects. */
#define	fips_enabled	0

#endif /* _LINUXKPI_LINUX_FIPS_H_ */
