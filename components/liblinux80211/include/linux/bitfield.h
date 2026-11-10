/* SPDX-License-Identifier: BSD-3-Clause */
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


/* [LNX] include/linux/bitfield.h: the field arithmetic.  The interface's
 * checks (_BF_FIELD_CHECK and the mask_is_constant assertions) are compile-time
 * diagnostics; the values below are the interface's own expressions. */
#ifndef __bf_shf
#define	__bf_shf(x)		(__builtin_ffsll(x) - 1)
#endif
#ifndef FIELD_GET
#define	FIELD_GET(_mask, _reg)						\
	((__force u64)((_reg) & (_mask)) >> __bf_shf(_mask))
#endif
#ifndef FIELD_PREP
#define	FIELD_PREP(_mask, _val)						\
	(((_mask) & ((typeof(_mask))(_val) << __bf_shf(_mask))))
#endif

#define	u8_encode_bits(val, mask)	((u8)FIELD_PREP((mask), (val)))
#define	u16_encode_bits(val, mask)	((u16)FIELD_PREP((mask), (val)))
#define	u32_encode_bits(val, mask)	((u32)FIELD_PREP((mask), (val)))
#define	u64_encode_bits(val, mask)	((u64)FIELD_PREP((mask), (val)))
#define	u8_get_bits(val, mask)		((u8)FIELD_GET((mask), (val)))
#define	u16_get_bits(val, mask)		((u16)FIELD_GET((mask), (val)))
#define	u32_get_bits(val, mask)		((u32)FIELD_GET((mask), (val)))
#define	u64_get_bits(val, mask)		((u64)FIELD_GET((mask), (val)))
#define	u8_replace_bits(dst, val, mask)					\
	((dst) = (u8)(((dst) & ~(mask)) | u8_encode_bits((val), (mask))))
#define	u16_replace_bits(dst, val, mask)				\
	((dst) = (u16)(((dst) & ~(mask)) | u16_encode_bits((val), (mask))))
#define	u32_replace_bits(dst, val, mask)				\
	((dst) = (u32)(((dst) & ~(mask)) | u32_encode_bits((val), (mask))))
#define	u64_replace_bits(dst, val, mask)				\
	((dst) = (u64)(((dst) & ~(mask)) | u64_encode_bits((val), (mask))))

#endif /* _LINUXKPI_LINUX_BITFIELD_H_ */
