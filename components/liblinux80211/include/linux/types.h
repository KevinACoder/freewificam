/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/types.h
 * @brief   The kernel's scalar type spellings, for sources that include this header
 *          directly.
 *
 * @design declaration-only compatibility face. The scalar aliases (u8..u64, s8..s64,
 *         their little- and big-endian forms, dma_addr_t, gfp_t) live in the core header
 *         every file of this face includes; this header exists because the kernel's own
 *         spelling is what its sources include, and it adds the two internal size types
 *         the vendored driver's platform layer names. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_TYPES_H_
#define _LINUXKPI_LINUX_TYPES_H_

#include "../../linuxkpi_core.h"

/* the asm-generic/posix_types.h spellings the driver's Linux platform layer names
 * (its SIZE_T/SSIZE_T aliases are these two); the interface makes them the word and
 * signed-word types, which is what the 64-bit and 32-bit targets of this project both
 * mean by them */
typedef unsigned long	__kernel_size_t;
typedef long		__kernel_ssize_t;

/* [LNX] include/linux/types.h:100-107: the legacy BSD-style scalar spellings the
 * interface keeps for old sources; the vendored bridge extension's DHCP record is
 * written with them. */
typedef u8		u_int8_t;
typedef u16		u_int16_t;
typedef u32		u_int32_t;
typedef u64		u_int64_t;

#endif /* _LINUXKPI_LINUX_TYPES_H_ */
