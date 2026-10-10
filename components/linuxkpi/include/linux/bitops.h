/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/bitops.h
 * @brief   The bit operations under the name a driver includes them.
 *
 * @design declaration-only compatibility face, and a pure forward. The whole surface a
 *         driver names - test_bit/set_bit/clear_bit and their test_and_* forms,
 *         __ffs/__fls/fls/ffs, the hweight family, the bitmap helpers and the
 *         for_each_set_bit iterator - already lives in the contract header
 *         (components/linuxkpi/linuxkpi_core.h, whose bit-operations section is the single
 *         definition of them). The interface keeps this header for the same names; this
 *         face keeps one mechanism, so the interface's name reaches the contract header
 *         and no operation is restated here. A name of the family the contract header does
 *         not carry yet arrives in that section, next to its own comment, rather than as a
 *         second definition.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_LINUX_BITOPS_H_
#define _LINUXKPI_LINUX_BITOPS_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_BITOPS_H_ */
