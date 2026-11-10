/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    asm/div64.h
 * @brief   The include arm64 code names for 64-bit division; the family is
 *          the contract header's.
 *
 * @design declaration-only compatibility face.  On a 64-bit target the
 *         interface's arm64 asm/div64.h is the compiler's own divide, and the
 *         component's linux/math64.h face already states div64_u64(),
 *         div64_s64() and do_div(); this header is the include-path spelling
 *         the stack's headers use.  Restating the family here made a
 *         redefinition against linuxkpi_core.h's copy.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_ASM_DIV64_H_
#define _LINUXKPI_ASM_DIV64_H_

#include <linux/math64.h>

#endif /* _LINUXKPI_ASM_DIV64_H_ */
