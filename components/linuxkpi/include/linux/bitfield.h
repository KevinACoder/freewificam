/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/bitfield.h
 * @brief   The bitfield accessors under the name a driver includes them.
 *
 * @design declaration-only compatibility face, and a pure forward. The whole surface a
 *         driver names - the masked FIELD_{GET,PREP,PREP_CONST} forms with FIELD_MAX and
 *         FIELD_FIT, and the type-directed encode/get/replace family for the
 *         little-endian, big-endian and host-order 8/16/32/64-bit widths - already lives
 *         in the contract header (components/linuxkpi/linuxkpi_core.h, whose bitfield
 *         section is the single definition of them). The interface keeps this header for
 *         the same names; this face keeps one mechanism, so the interface's name reaches
 *         the contract header and no generator is restated here. A name of the family the
 *         contract header does not carry yet arrives in that section, next to its own
 *         comment, rather than as a second definition.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_BITFIELD_H_
#define _LINUXKPI_LINUX_BITFIELD_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_BITFIELD_H_ */
